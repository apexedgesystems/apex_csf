/**
 * @file I2cStatus.cpp
 * @brief Implementation of I2C status code string conversion.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cStatus.hpp"

namespace apex {
namespace protocols {
namespace i2c {

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
  case Status::ERROR_UNSUPPORTED:
    return "ERROR_UNSUPPORTED";
  case Status::ERROR_BUSY:
    return "ERROR_BUSY";
  case Status::ERROR_NACK:
    return "ERROR_NACK";
  default:
    return "UNKNOWN";
  }
}

} // namespace i2c
} // namespace protocols
} // namespace apex
