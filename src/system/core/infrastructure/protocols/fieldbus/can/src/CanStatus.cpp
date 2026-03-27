/**
 * @file CanStatus.cpp
 * @brief CAN status code string conversion.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/can/inc/CanStatus.hpp"

namespace apex {
namespace protocols {
namespace fieldbus {
namespace can {

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
  default:
    return "UNKNOWN_STATUS";
  }
}

} // namespace can
} // namespace fieldbus
} // namespace protocols
} // namespace apex
