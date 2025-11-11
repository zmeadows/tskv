module;

#include <cassert>
#include <cstdlib>
#include <netdb.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tskv/common/defer.hpp"
#include "tskv/common/logging.hpp"

export module tskv.net.server;

import tskv.common.buffer;
import tskv.common.logging;
import tskv.net.socket;
import tskv.net.connection;
import tskv.net.reactor;

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

  defer {
    close(listen_fd);
  };

  Reactor reactor;

  reactor.add_listener(listen_fd);
  // TODO[@zmeadows][P1]: until reactor has a real exit path, the defer above never runs
  reactor.run();
}

} // namespace tskv::net
