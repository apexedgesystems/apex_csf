/**
 * @file UartStatus.cpp
 * @brief Implementation of Status enum toString function.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartStatus.hpp"

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ---------------------------------- API ----------------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::WOULD_BLOCK:
    return "WOULD_BLOCK";
  case Status::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case Status::ERROR_CLOSED:
    return "ERROR_CLOSED";
  case Status::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  case Status::ERROR_NOT_CONFIGURED:
    return "ERROR_NOT_CONFIGURED";
  case Status::ERROR_IO:
    return "ERROR_IO";
  case Status::ERROR_UNSUPPORTED:
    return "ERROR_UNSUPPORTED";
  case Status::ERROR_BUSY:
    return "ERROR_BUSY";
  default:
    return "UNKNOWN_STATUS";
  }
}

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex
