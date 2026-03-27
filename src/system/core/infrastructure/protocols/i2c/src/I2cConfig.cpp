/**
 * @file I2cConfig.cpp
 * @brief Implementation of I2C configuration string conversion.
 */

#include "src/system/core/infrastructure/protocols/i2c/inc/I2cConfig.hpp"

namespace apex {
namespace protocols {
namespace i2c {

/* ----------------------------- API ----------------------------- */

const char* toString(AddressMode mode) noexcept {
  switch (mode) {
  case AddressMode::SEVEN_BIT:
    return "SEVEN_BIT";
  case AddressMode::TEN_BIT:
    return "TEN_BIT";
  default:
    return "UNKNOWN";
  }
}

} // namespace i2c
} // namespace protocols
} // namespace apex
