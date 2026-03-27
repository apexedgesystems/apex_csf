/**
 * @file LinStatus.cpp
 * @brief Implementation of LIN status string conversion.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinStatus.hpp"

namespace apex {
namespace protocols {
namespace fieldbus {
namespace lin {

/* ----------------------------- API ----------------------------- */

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
  case Status::ERROR_CHECKSUM:
    return "ERROR_CHECKSUM";
  case Status::ERROR_SYNC:
    return "ERROR_SYNC";
  case Status::ERROR_PARITY:
    return "ERROR_PARITY";
  case Status::ERROR_FRAME:
    return "ERROR_FRAME";
  case Status::ERROR_NO_RESPONSE:
    return "ERROR_NO_RESPONSE";
  case Status::ERROR_BUS_COLLISION:
    return "ERROR_BUS_COLLISION";
  case Status::ERROR_BREAK:
    return "ERROR_BREAK";
  }
  return "UNKNOWN";
}

} // namespace lin
} // namespace fieldbus
} // namespace protocols
} // namespace apex
