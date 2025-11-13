module;

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <vector>

#include "tskv/common/logging.hpp"

export module tskv.net.reactor;

import tskv.common.logging;
import tskv.common.metrics;
import tskv.net.channel;
import tskv.net.socket;

namespace metrics = tskv::common::metrics;

// https://copyconstruct.medium.com/the-method-to-epolls-madness-d9d2d6378642

/*
int register_epoll_timer(
  int epfd, std::chrono::nanoseconds initial, std::chrono::nanoseconds interval)
{
  int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
  TSKV_INVARIANT(timer_fd != -1, "failed to create epoll timer");

  struct itimerspec new_value;
  new_value.it_value    = tc::to_timespec(initial);
  new_value.it_interval = tc::to_timespec(interval);

  bool timer_configure_success = timerfd_settime(timer_fd, 0, &new_value, NULL) != -1;
  TSKV_INVARIANT(timer_configure_success, "failed to configure epoll timer");

  struct epoll_event timer_event;
  timer_event.events  = EPOLLIN; // monitor for readability (timer expiration)
  timer_event.data.fd = timer_fd;

  bool timer_register_success = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd, &timer_event) != -1;
  TSKV_INVARIANT(timer_register_success, "failed to register epoll timer");

  return timer_fd;
}

void Reactor<Proto>::on_timer_event()
{
  uint64_t expirations;
  ssize_t  s = read(timer_fd_, &expirations, sizeof(uint64_t));
  if (s != sizeof(uint64_t)) {
    TSKV_LOG_WARN("failed to read timer");
    return;
  }

  TSKV_LOG_INFO("Timer expired {} times", expirations);
}
*/

export namespace tskv::net {

template <Protocol Proto>
struct Reactor {
private:
  static constexpr std::size_t EVENT_BUFSIZE = 128;

  ChannelPool<Proto> pool_;

  epoll_event evt_buffer_[EVENT_BUFSIZE]{};

  int  epoll_fd_      = -1;
  int  listener_fd_   = -1;
  int  wakeup_fd_     = -1;
  int  signal_fd_     = -1;
  bool shutting_down_ = false;

  Reactor(const Reactor&)            = delete;
  Reactor& operator=(const Reactor&) = delete;

  void on_channel_event(Channel<Proto>* channel, std::uint32_t event_mask);
  void on_listener_event();

  void on_wakeup_event()
  {
    uint64_t tmp;
    while (::read(wakeup_fd_, &tmp, sizeof tmp) == sizeof tmp)
      continue; // drain
  }

  void on_signal_event()
  {
    signalfd_siginfo si;
    while (::read(signal_fd_, &si, sizeof si) == sizeof si) {
      // Treat SIGINT/SIGTERM as shutdown
      request_shutdown();
    }
  }

  void close_channel(Channel<Proto>* channel) noexcept;
  void sweep_closing_channels() noexcept;
  void close_listener() noexcept;

public:
  Reactor();
  ~Reactor();

  void add_listener(int listener_fd); // hands over ownership
  void poll_once();
  void run();
  void request_shutdown() noexcept;
};

template <Protocol Proto>
Reactor<Proto>::Reactor()
{
  { // epoll
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    TSKV_LOG_INFO("epoll_fd_ = {}", epoll_fd_);
    TSKV_INVARIANT(epoll_fd_ != -1, "Failed to create epoll instance.");
  }

  { // wakeup
    wakeup_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    TSKV_INVARIANT(wakeup_fd_ != -1, "eventfd failed");

    epoll_event wev{.events = EPOLLIN, .data = {.fd = wakeup_fd_}};
    const int   wrc = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wakeup_fd_, &wev);
    TSKV_INVARIANT(wrc != -1, "epoll add wakeup_fd_ failed");
  }

  { // signals
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &mask, nullptr);

    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    TSKV_INVARIANT(signal_fd_ != -1, "signalfd failed");

    epoll_event ev{.events = EPOLLIN, .data = {.fd = signal_fd_}};
    TSKV_INVARIANT(
      epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, signal_fd_, &ev) != -1, "epoll add signalfd failed");
  }
}

template <Protocol Proto>
Reactor<Proto>::~Reactor()
{
  if (listener_fd_ != -1) {
    close_listener();
  }

  if (wakeup_fd_ != -1) {
    ::close(wakeup_fd_);
  }

  if (signal_fd_ != -1) {
    ::close(signal_fd_);
  }

  if (epoll_fd_ != -1) {
    ::close(epoll_fd_);
  }
}

template <Protocol Proto>
void Reactor<Proto>::close_listener() noexcept
{
  if (listener_fd_ != -1) {
    epoll_event ev{};
    (void)epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, listener_fd_, &ev);
    ::close(listener_fd_);
    listener_fd_ = -1;
  }
}

// TODO[@zmeadows][P2]: add optional grace period so channels have time to flush if desired
template <Protocol Proto>
void Reactor<Proto>::request_shutdown() noexcept
{
  if (shutting_down_) {
    return;
  }

  TSKV_LOG_INFO("Shutdown requested...");

  shutting_down_ = true;

  // 1) stop accepting new connections
  close_listener();

  // 2) flip all channels to Draining (stop reading, flush any pending TX)
  pool_.for_each([&](auto* ch) { ch->begin_shutdown(); });

  // 3) wake epoll immediately
  uint64_t one = 1;
  (void)::write(wakeup_fd_, &one, sizeof one);

  // 4) close already-finished sockets now
  sweep_closing_channels();
}

template <Protocol Proto>
void Reactor<Proto>::add_listener(int listener_fd)
{
  TSKV_INVARIANT(listener_fd_ == -1, "multiple listeners not currently supported");
  // To support multiple listeners:
  // * store a small vector of listener fds (each EPOLLIN|EPOLLET-registered)
  // * tag epoll events (e.g., via event.data.u64 or a pointer wrapper) to distinguish listeners from channels, and
  // * dispatch `accept4()` on whichever listener produced the event.

  const int  flags        = fcntl(listener_fd, F_GETFL, 0);
  const bool non_blocking = (flags & O_NONBLOCK) != 0;
  TSKV_INVARIANT(flags != -1 && non_blocking, "invalid listener flags (blocking or broken)");

  struct epoll_event event{};
  event.events  = EPOLLIN | EPOLLET;
  event.data.fd = listener_fd;

  bool epoll_init_success = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listener_fd, &event) != -1;
  TSKV_LOG_INFO("listener_fd = {}", listener_fd);
  TSKV_INVARIANT(epoll_init_success, "failed to register listener with epoll_ctl");

  listener_fd_ = listener_fd;
}

template <Protocol Proto>
void Reactor<Proto>::close_channel(Channel<Proto>* channel) noexcept
{
  channel->notify_close();

  const int client_fd = channel->fd();

  struct epoll_event ev{};
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, &ev) == -1) {
    TSKV_LOG_WARN("epoll_ctl DEL failed for fd={}", client_fd);
  }

  channel->detach();

  pool_.release(client_fd);

  ::close(client_fd);

  TSKV_LOG_INFO("closed client_fd = {}", client_fd);
}

template <Protocol Proto>
void Reactor<Proto>::on_channel_event(Channel<Proto>* channel, std::uint32_t event_mask)
{
  const int client_fd = channel->fd();

  channel->handle_events(event_mask);

  if (channel->should_close()) {
    close_channel(channel);
    return;
  }

  // TODO[@zmeadows][P2]: if this shows up in profiler, save previous mask and compare, don't always MOD
  const std::uint32_t new_mask = channel->desired_events() | EPOLLET | EPOLLRDHUP;
  struct epoll_event  event{};
  event.events  = new_mask;
  event.data.fd = client_fd;
  if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &event) == -1) {
    TSKV_LOG_WARN("epoll_ctl MOD failed for fd={}", client_fd);
  }
}

template <Protocol Proto>
void Reactor<Proto>::on_listener_event()
{
  while (true) {
    sockaddr_storage client_addr{};
    socklen_t        client_addr_size = sizeof client_addr;

    int client_fd = accept4(
      listener_fd_, (sockaddr*)&client_addr, &client_addr_size, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (client_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // done processing new channels, try again later
        return;
      }

      TSKV_LOG_WARN("failed to accept new channel: errno={}", errno);

      switch (errno) {
        case EMFILE:
          metrics::inc_counter<"net.accept_error.emfile">();
          break;
        case ENFILE:
          metrics::inc_counter<"net.accept_error.enfile">();
          break;
        case ENOBUFS:
          metrics::inc_counter<"net.accept_error.enobufs">();
          break;
        default:
          metrics::inc_counter<"net.accept_error.other">();
          break;
      }

      return;
    }

    Channel<Proto>* channel = pool_.acquire(client_fd);
    channel->attach(client_fd);

    struct epoll_event event{};
    event.events  = channel->desired_events() | EPOLLET | EPOLLRDHUP;
    event.data.fd = client_fd;
    TSKV_LOG_INFO("added client_fd = {}", client_fd);

    bool add_client_success = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) != -1;
    if (!add_client_success) {
      TSKV_LOG_WARN("failed to add newly accepted client (fd = {}) to epoll watch list", client_fd);
      close_channel(channel);
    }
  }
}

template <Protocol Proto>
void Reactor<Proto>::poll_once()
{
  int nevents;
  do {
    // timeout == -1 => wait forever (or until interrupt)
    nevents = epoll_wait(epoll_fd_, evt_buffer_, EVENT_BUFSIZE, -1);
  } while (nevents == -1 && errno == EINTR);

  for (int ievent = 0; ievent < nevents; ++ievent) {
    const epoll_event&  evt        = evt_buffer_[ievent];
    const int           event_fd   = evt.data.fd;
    const std::uint32_t event_mask = evt.events;

    if (Channel<Proto>* channel = pool_.lookup(event_fd); channel != nullptr) [[likely]] {
      on_channel_event(channel, event_mask);
    }
    else if (event_fd == listener_fd_) {
      on_listener_event();
    }
    else if (event_fd == wakeup_fd_) [[unlikely]] {
      on_wakeup_event();
      sweep_closing_channels();
    }
    else if (event_fd == signal_fd_) [[unlikely]] {
      on_signal_event();
      sweep_closing_channels();
    }
    else {
      TSKV_LOG_WARN("unknown event file descriptor encountered: {}", event_fd);
    }
  }
}

// TODO[@zmeadows][P0]: need to handle carefully how often/when this is called
template <Protocol Proto>
void Reactor<Proto>::sweep_closing_channels() noexcept
{
  thread_local std::vector<Channel<Proto>*> channels_to_close;
  channels_to_close.clear();

  pool_.for_each([&](auto* channel) {
    if (channel->should_close()) {
      channels_to_close.push_back(channel);
    }
  });

  for (auto* ch : channels_to_close) {
    close_channel(ch);
  }
}

template <Protocol Proto>
void Reactor<Proto>::run()
{
  while (true) {
    if (shutting_down_ && pool_.empty()) {
      TSKV_LOG_INFO("Shutdown succeeded...");
      return;
    }
    poll_once();
  }
}

} // namespace tskv::net
