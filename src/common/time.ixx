module;

#include <chrono>

#include "tskv/common/logging.hpp"

export module tskv.common.time;

import tskv.common.logging;

export namespace tskv::common {

timespec to_timespec(std::chrono::nanoseconds d)
{
  using namespace std::chrono;

  auto sec  = duration_cast<seconds>(d);
  auto nsec = duration_cast<nanoseconds>(d - sec);

  timespec ts;
  ts.tv_sec  = static_cast<time_t>(sec.count());
  ts.tv_nsec = static_cast<long>(nsec.count());

  return ts;
}

} // namespace tskv::common
