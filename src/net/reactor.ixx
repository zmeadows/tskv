module;

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <errno.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <unistd.h>

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

  // To support multiple listeners:
  // * store a small vector of listener fds (each EPOLLIN|EPOLLET-registered)
  // * tag epoll events (e.g., via event.data.u64 or a pointer wrapper) to distinguish listeners from channels, and
  // * dispatch `accept4()` on whichever listener produced the event.

  int  epoll_fd_       = -1; // owning
  int  listener_fd_    = -1; // non-owning
  bool stop_requested_ = false;

  Reactor(const Reactor&)            = delete;
  Reactor& operator=(const Reactor&) = delete;

  void on_channel_event(Channel<Proto>* channel, std::uint32_t event_mask);
  void on_listener_event();

public:
  Reactor();
  ~Reactor();

  void add_listener(int listener_fd);
  void poll_once();
  void run();
};

template <Protocol Proto>
Reactor<Proto>::Reactor()
{
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  TSKV_LOG_INFO("epoll_fd_ = {}", epoll_fd_);
  TSKV_INVARIANT(epoll_fd_ != -1, "Failed to create epoll instance.");
}

template <Protocol Proto>
Reactor<Proto>::~Reactor()
{
  if (epoll_fd_ != -1) {
    close(epoll_fd_);
    epoll_fd_ = -1;
  }

  listener_fd_ = -1;

  stop_requested_ = false;
}

template <Protocol Proto>
void Reactor<Proto>::add_listener(int listener_fd)
{
  TSKV_INVARIANT(listener_fd_ == -1, "multiple listeners not currently supported");

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
void Reactor<Proto>::on_channel_event(Channel<Proto>* channel, std::uint32_t event_mask)
{
  const int client_fd = channel->fd();

  channel->handle_events(event_mask);

  if (channel->should_close()) {
    struct epoll_event ev{};
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, &ev) == -1) {
      TSKV_LOG_WARN("epoll_ctl DEL failed for fd={}", client_fd);
    }
    channel->detach();
    pool_.release(client_fd);
    ::close(client_fd);
    TSKV_LOG_INFO("closed client_fd = {}", client_fd);
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
    int client_fd = accept4(listener_fd_, (sockaddr*)&client_addr, &client_addr_size, 0);

    if (client_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // done processing new channels, try again later
        return;
      }
      else {
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
      }
    }

    // process valid new channel

    if (!set_socket_nonblocking(client_fd)) {
      TSKV_LOG_WARN("failed to set client channel socket to nonblocking");
      ::close(client_fd);
      continue;
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
      channel->detach();
      pool_.release(client_fd);
      ::close(client_fd);
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

  TSKV_INVARIANT(nevents != -1, "failed to poll events from epoll");

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
    else {
      TSKV_LOG_WARN("unknown event file descriptor encountered: {}", event_fd);
    }
  }
}

template <Protocol Proto>
void Reactor<Proto>::run()
{
  // run() currently blocks forever in epoll_wait; to support graceful shutdown,
  // wire `stop_requested_` to a wakeup fd (eventfd/timerfd) registered with epoll,
  // and break the loop when that fd fires. On shutdown, stop accepting, mark
  // channels for draining (read side closed), flush pending writes, then remove
  // fds from epoll and close them before returning.
  while (true) {
    poll_once();
  }
}

} // namespace tskv::net
