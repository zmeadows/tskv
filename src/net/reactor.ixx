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

import tskv.net.channel;
import tskv.net.socket;

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

void Reactor::on_timer_event()
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

struct Reactor {
private:
  static constexpr std::size_t EVENT_BUFSIZE = 128;

  epoll_event evt_buffer_[EVENT_BUFSIZE]{};
  ChannelPool pool_;

  int  epoll_fd_       = -1; // owning
  int  listener_fd_    = -1; // non-owning
  bool stop_requested_ = false;

  Reactor(const Reactor&)            = delete;
  Reactor& operator=(const Reactor&) = delete;

  void on_channel_event(Channel* channel, std::uint32_t event_mask);
  void on_listener_event();

public:
  Reactor();
  ~Reactor();

  void add_listener(int listener_fd);
  void poll_once();
  void run();
};

Reactor::Reactor()
{
  epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
  TSKV_LOG_INFO("epoll_fd_ = {}", epoll_fd_);
  TSKV_INVARIANT(epoll_fd_ != -1, "Failed to create epoll instance.");
}

Reactor::~Reactor()
{
  if (epoll_fd_ != -1) {
    close(epoll_fd_);
    epoll_fd_ = -1;
  }

  listener_fd_ = -1;

  stop_requested_ = false;
}

void Reactor::add_listener(int listener_fd)
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

void Reactor::on_channel_event(Channel* channel, std::uint32_t event_mask)
{
  const int client_fd = channel->fd();

  channel->handle_events(event_mask);

  if (channel->should_close()) {
    struct epoll_event ev{};
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, &ev);
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
  epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &event);
}

void Reactor::on_listener_event()
{
  sockaddr_storage client_addr{};
  socklen_t        client_addr_size = sizeof client_addr;

  while (true) {
    int client_fd = accept4(listener_fd_, (sockaddr*)&client_addr, &client_addr_size, 0);

    if (client_fd == -1) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // done processing new channels, try again later
        return;
      }
      else {
        TSKV_LOG_WARN("failed to accept new channel");
        continue;
      }
    }

    // process valid new channel

    if (!set_socket_nonblocking(client_fd)) {
      TSKV_LOG_WARN("failed to set client channel socket to nonblocking");
      ::close(client_fd);
      continue;
    }

    Channel* channel = pool_.acquire(client_fd);
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

void Reactor::poll_once()
{
  int nevents;
  do {
    nevents = epoll_wait(epoll_fd_, evt_buffer_, EVENT_BUFSIZE, -1);
  } while (nevents == -1 && errno == EINTR);

  TSKV_INVARIANT(nevents != -1, "failed to poll events from epoll");

  for (int ievent = 0; ievent < nevents; ++ievent) {
    const epoll_event&  evt        = evt_buffer_[ievent];
    const int           event_fd   = evt.data.fd;
    const std::uint32_t event_mask = evt.events;

    if (Channel* channel = pool_.lookup(event_fd); channel != nullptr) [[likely]] {
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

void Reactor::run()
{
  while (true) {
    poll_once();
  }
}

} // namespace tskv::net
