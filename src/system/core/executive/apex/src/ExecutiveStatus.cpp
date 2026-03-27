/**
 * @file ExecutiveStatus.cpp
 * @brief String representations for executive status codes.
 */

#include "src/system/core/executive/apex/inc/ExecutiveStatus.hpp"

namespace executive {

/* ----------------------------- API ----------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";

  // Error codes
  case Status::ERROR_MODULE_INIT_FAIL:
    return "ERROR_MODULE_INIT_FAIL";
  case Status::ERROR_ARG_PARSE_FAIL:
    return "ERROR_ARG_PARSE_FAIL";
  case Status::ERROR_RUNTIME_FAILURE:
    return "ERROR_RUNTIME_FAILURE";
  case Status::ERROR_SHUTDOWN_FAILURE:
    return "ERROR_SHUTDOWN_FAILURE";
  case Status::ERROR_HARD_REALTIME_FAILURE:
    return "ERROR_HARD_REALTIME_FAILURE";
  case Status::ERROR_COMPONENT_COLLISION:
    return "ERROR_COMPONENT_COLLISION";
  case Status::ERROR_SIGNAL_BLOCK_FAILED:
    return "ERROR_SIGNAL_BLOCK_FAILED";
  case Status::ERROR_SCHEDULER_NO_TASKS:
    return "ERROR_SCHEDULER_NO_TASKS";
  case Status::ERROR_CONFIG_NOT_FOUND:
    return "ERROR_CONFIG_NOT_FOUND";
  case Status::ERROR_TPRM_UNPACK_FAIL:
    return "ERROR_TPRM_UNPACK_FAIL";

  // Warning codes
  case Status::WARN_CLOCK_DRIFT:
    return "WARN_CLOCK_DRIFT";
  case Status::WARN_FRAME_OVERRUN:
    return "WARN_FRAME_OVERRUN";
  case Status::WARN_REGISTRY_EXPORT:
    return "WARN_REGISTRY_EXPORT";
  case Status::WARN_CLOCK_FROZEN:
    return "WARN_CLOCK_FROZEN";
  case Status::WARN_CLOCK_STOP_TIMEOUT:
    return "WARN_CLOCK_STOP_TIMEOUT";
  case Status::WARN_TASK_DRAIN_TIMEOUT:
    return "WARN_TASK_DRAIN_TIMEOUT";
  case Status::WARN_IO_ERROR:
    return "WARN_IO_ERROR";
  case Status::WARN_STARTUP_TIME_PASSED:
    return "WARN_STARTUP_TIME_PASSED";
  case Status::WARN_INVALID_CLOCK_FREQ:
    return "WARN_INVALID_CLOCK_FREQ";
  case Status::WARN_THREAD_CONFIG_FAIL:
    return "WARN_THREAD_CONFIG_FAIL";
  case Status::WARN_TPRM_LOAD_FAIL:
    return "WARN_TPRM_LOAD_FAIL";
  case Status::WARN_SWAP_FAILED:
    return "WARN_SWAP_FAILED";
  case Status::WARN_QUEUE_ALLOC_FAIL:
    return "WARN_QUEUE_ALLOC_FAIL";

  // Marker
  case Status::EOE_EXECUTIVE:
    return "EOE_EXECUTIVE";
  }
  return "UNKNOWN_STATUS";
}

} // namespace executive
