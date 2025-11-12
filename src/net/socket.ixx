module;

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tskv/common/logging.hpp"

export module tskv.net.socket;

import tskv.common.logging;

export namespace tskv::net {

bool set_socket_nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags == -1) {
    return false;
  }

  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
    return false;
  }

  return true;
}

int start_listener()
{
  addrinfo  hints{};
  addrinfo* servinfo = nullptr;

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM; // TCP

  if (int status = getaddrinfo("localhost", "8080", &hints, &servinfo); status != 0) {
    const auto errmsg = gai_strerror(status);
    TSKV_LOG_ERROR("getaddrinfo failure: {}", errmsg);
    return -1;
  }

  int listen_fd = -1;

  for (addrinfo* p = servinfo; p != NULL; p = p->ai_next) {
    if (p->ai_family != AF_INET) {
      continue;
    }

    int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (fd == -1) {
      continue;
    }

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
      ::close(fd);
      continue;
    }

    if (!set_socket_nonblocking(fd)) {
      ::close(fd);
      continue;
    }

    if (bind(fd, p->ai_addr, p->ai_addrlen) == -1) {
      ::close(fd);
      continue;
    }

    listen_fd = fd;
    break;
  }

  freeaddrinfo(servinfo);

  if (listen(listen_fd, SOMAXCONN) == -1) {
    TSKV_LOG_ERROR("Failed to listen on listen_fd = {}", listen_fd);
    ::close(listen_fd);
    return -1;
  }

  return listen_fd;
}

} // namespace tskv::net
