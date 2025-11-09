module;

#include <cassert>
#include <netdb.h>
#include <print>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tskv/common/logging.hpp"

export module tskv.net.server;

import tskv.common.buffer;
import tskv.common.logging;
import tskv.net.socket;
import tskv.net.connection;

namespace tc = tskv::common;

export namespace tskv::net {

void scratch_main()
{
  (void)signal(SIGPIPE, SIG_IGN);

  int listen_fd = start_listener();

  if (listen_fd == -1) {
    TSKV_LOG_CRITICAL("failed to bind/listen (IPv4)");
    std::abort();
  }

  int client_fd = accept_client(listen_fd);

  if (client_fd == -1) {
    TSKV_LOG_CRITICAL("accept");
    std::abort();
  }

  std::println("accepted connection.");

  Connection connection;
  connection.attach(client_fd);
  while (!connection.closed()) {
    connection.echo();
  }
  connection.disconnect();

  std::println("closed connection.");

  close(listen_fd);
}

} // namespace tskv::net
