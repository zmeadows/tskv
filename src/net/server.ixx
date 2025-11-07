module;

#include <cstdlib>
#include <iostream>
#include <netdb.h>
#include <print>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

export module net.server;

import common.buffer;
namespace tc = tskv::common;

void FATAL(std::string_view msg)
{
  std::println(std::cerr, "fatal: {}", msg);
  std::exit(EXIT_FAILURE);
}

export namespace tskv::net {

struct Connection {
private:
  using Buffer = tc::SimpleBuffer<2048>;

  Buffer tx_buf_{};
  Buffer rx_buf_{};

  int  fd_         = -1;
  bool tx_enabled_ = false;
  bool rx_enabled_ = false;

public:
  void attach(int client_fd)
  {
    fd_         = client_fd;
    tx_enabled_ = true;
    rx_enabled_ = true;
  }

  inline bool closed() { return fd_ < 0; }

  void close()
  {
    ::close(fd_);
    fd_         = -1;
    tx_enabled_ = false;
    rx_enabled_ = false;
  }

  std::size_t send(std::span<std::byte> bytes)
  {
    (void)bytes;
    (void)tx_buf_;
    return 0;
  }

  [[nodiscard]] const std::pair<std::byte*, std::size_t> read() const
  {
    (void)rx_buf_;
    return {nullptr, 0};
  }
};

void echo_blocking(int client_fd)
{

  char    buf[1024]{};
  ssize_t msg_len = 0;

  while (true) {
    if (msg_len = recv(client_fd, buf, sizeof buf, 0); msg_len == -1) {
      FATAL("recv");
    }

    if (msg_len == 0) {
      std::println("client shutdown.");
      break;
    }

    {
      ssize_t bytes_sent = 0;

      while (bytes_sent < msg_len) {
        const ssize_t n = send(client_fd, buf + bytes_sent, msg_len - bytes_sent, 0);
        if (n == -1) {
          if (errno == EINTR) {
            continue;
          }
          else if (errno == EPIPE) {
            std::println("client disappeared while sending");
            return;
          }
          else {
            FATAL("send");
          }
        }
        bytes_sent += n;
      }
    }
  }
}

void scratch()
{
  (void)signal(SIGPIPE, SIG_IGN);

  addrinfo  hints{};
  addrinfo* servinfo = nullptr;

  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM; // TCP

  if (int status = getaddrinfo("localhost", "8080", &hints, &servinfo); status != 0) {
    FATAL(gai_strerror(status));
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
      close(fd);
      continue;
    }

    if (bind(fd, p->ai_addr, p->ai_addrlen) == -1) {
      close(fd);
      continue;
    }

    listen_fd = fd;
    break;
  }

  freeaddrinfo(servinfo);

  if (listen_fd == -1) {
    FATAL("failed to bind IPv4 listener socket");
  }

  if (listen(listen_fd, SOMAXCONN) == -1) {
    FATAL("listen");
  }

  sockaddr_storage client_addr{};
  socklen_t        client_addr_size = sizeof client_addr;

  {
    int client_fd = accept(listen_fd, (sockaddr*)&client_addr, &client_addr_size);

    if (client_fd == -1) {
      FATAL("accept");
    }

    std::println("accepted connection.");

    echo_blocking(client_fd);

    close(client_fd);
    close(listen_fd);
  }
}

} // namespace tskv::net
