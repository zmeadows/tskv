module;

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <print>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

export module tskv.net.socket;

export namespace tskv::net {

inline int socket_stub()
{
  return 0;
}

int start_listener()
{
  addrinfo  hints{};
  addrinfo* servinfo = nullptr;

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM; // TCP

  if (int status = getaddrinfo("localhost", "8080", &hints, &servinfo); status != 0) {
    std::println("{}", gai_strerror(status));
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

    if (bind(fd, p->ai_addr, p->ai_addrlen) == -1) {
      ::close(fd);
      continue;
    }

    listen_fd = fd;
    break;
  }

  freeaddrinfo(servinfo);

  if (listen(listen_fd, SOMAXCONN) == -1) {
    std::println("failed to listen.");
    return -1;
  }

  return listen_fd;
}

int accept_client(int listen_fd)
{

  sockaddr_storage client_addr{};
  socklen_t        client_addr_size = sizeof client_addr;

  return accept(listen_fd, (sockaddr*)&client_addr, &client_addr_size);
}

} // namespace tskv::net
