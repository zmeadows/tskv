module;

#include <cassert>
#include <charconv>
#include <cstdint>
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

int start_listener(const char* const host, std::uint16_t port)
{
  addrinfo  hints{};
  addrinfo* servinfo = nullptr;

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char portbuf[8]{};
  auto [ptr, ec] = std::to_chars(portbuf, portbuf + 8, port);
  TSKV_DEMAND(ec == std::errc{}, "failed to convert port number ({}) to string", port);
  *ptr = '\0'; // Null-terminate the string in the buffer

  if (int status = getaddrinfo(host, portbuf, &hints, &servinfo); status != 0) {
    const auto errmsg = gai_strerror(status);
    TSKV_LOG_ERROR("getaddrinfo failure: {}", errmsg);
    return -1;
  }

  int listen_fd = -1;

  for (addrinfo* p = servinfo; p != NULL; p = p->ai_next) {
    if (p->ai_family != AF_INET) {
      continue;
    }

    int fd = socket(p->ai_family, p->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, p->ai_protocol);
    if (fd == -1) {
      continue;
    }

    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
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
