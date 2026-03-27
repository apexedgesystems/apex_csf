/**
 * @file SpiConfig.cpp
 * @brief Implementation of SPI configuration string conversion.
 */

#include "src/system/core/infrastructure/protocols/spi/inc/SpiConfig.hpp"

namespace apex {
namespace protocols {
namespace spi {

/* ----------------------------- API ----------------------------- */

const char* toString(SpiMode mode) noexcept {
  switch (mode) {
  case SpiMode::MODE_0:
    return "MODE_0";
  case SpiMode::MODE_1:
    return "MODE_1";
  case SpiMode::MODE_2:
    return "MODE_2";
  case SpiMode::MODE_3:
    return "MODE_3";
  default:
    return "UNKNOWN";
  }
}

const char* toString(BitOrder order) noexcept {
  switch (order) {
  case BitOrder::MSB_FIRST:
    return "MSB_FIRST";
  case BitOrder::LSB_FIRST:
    return "LSB_FIRST";
  default:
    return "UNKNOWN";
  }
}

} // namespace spi
} // namespace protocols
} // namespace apex
