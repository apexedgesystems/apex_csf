/**
 * @file AprotoStatus.cpp
 * @brief Implementation of APROTO status code utilities.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoStatus.hpp"

namespace system_core {
namespace protocols {
namespace aproto {

/* --------------------------------- API ---------------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::ERROR_INVALID_MAGIC:
    return "ERROR_INVALID_MAGIC";
  case Status::ERROR_INVALID_VERSION:
    return "ERROR_INVALID_VERSION";
  case Status::ERROR_INCOMPLETE:
    return "ERROR_INCOMPLETE";
  case Status::ERROR_PAYLOAD_TOO_LARGE:
    return "ERROR_PAYLOAD_TOO_LARGE";
  case Status::ERROR_BUFFER_TOO_SMALL:
    return "ERROR_BUFFER_TOO_SMALL";
  case Status::ERROR_PAYLOAD_TRUNCATED:
    return "ERROR_PAYLOAD_TRUNCATED";
  case Status::ERROR_CRC_MISMATCH:
    return "ERROR_CRC_MISMATCH";
  case Status::ERROR_DECRYPT_FAILED:
    return "ERROR_DECRYPT_FAILED";
  case Status::ERROR_ENCRYPT_FAILED:
    return "ERROR_ENCRYPT_FAILED";
  case Status::ERROR_INVALID_KEY:
    return "ERROR_INVALID_KEY";
  case Status::ERROR_MISSING_CRYPTO:
    return "ERROR_MISSING_CRYPTO";
  case Status::WARN_RESERVED_FLAGS:
    return "WARN_RESERVED_FLAGS";
  case Status::EOE_APROTO:
    return "EOE_APROTO";
  }
  return "UNKNOWN";
}

} // namespace aproto
} // namespace protocols
} // namespace system_core
