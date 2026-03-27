/**
 * @file UartConfig.cpp
 * @brief Implementation of configuration enum toString functions.
 */

#include "src/system/core/infrastructure/protocols/serial/uart/inc/UartConfig.hpp"

namespace apex {
namespace protocols {
namespace serial {
namespace uart {

/* ---------------------------------- API ----------------------------------- */

const char* toString(BaudRate rate) noexcept {
  switch (rate) {
  case BaudRate::B_1200:
    return "1200";
  case BaudRate::B_2400:
    return "2400";
  case BaudRate::B_4800:
    return "4800";
  case BaudRate::B_9600:
    return "9600";
  case BaudRate::B_19200:
    return "19200";
  case BaudRate::B_38400:
    return "38400";
  case BaudRate::B_57600:
    return "57600";
  case BaudRate::B_115200:
    return "115200";
  case BaudRate::B_230400:
    return "230400";
  case BaudRate::B_460800:
    return "460800";
  case BaudRate::B_500000:
    return "500000";
  case BaudRate::B_576000:
    return "576000";
  case BaudRate::B_921600:
    return "921600";
  case BaudRate::B_1000000:
    return "1000000";
  case BaudRate::B_1152000:
    return "1152000";
  case BaudRate::B_1500000:
    return "1500000";
  case BaudRate::B_2000000:
    return "2000000";
  case BaudRate::B_2500000:
    return "2500000";
  case BaudRate::B_3000000:
    return "3000000";
  case BaudRate::B_3500000:
    return "3500000";
  case BaudRate::B_4000000:
    return "4000000";
  default:
    return "UNKNOWN";
  }
}

const char* toString(DataBits bits) noexcept {
  switch (bits) {
  case DataBits::FIVE:
    return "5";
  case DataBits::SIX:
    return "6";
  case DataBits::SEVEN:
    return "7";
  case DataBits::EIGHT:
    return "8";
  default:
    return "UNKNOWN";
  }
}

const char* toString(Parity parity) noexcept {
  switch (parity) {
  case Parity::NONE:
    return "NONE";
  case Parity::ODD:
    return "ODD";
  case Parity::EVEN:
    return "EVEN";
  default:
    return "UNKNOWN";
  }
}

const char* toString(StopBits bits) noexcept {
  switch (bits) {
  case StopBits::ONE:
    return "1";
  case StopBits::TWO:
    return "2";
  default:
    return "UNKNOWN";
  }
}

const char* toString(FlowControl fc) noexcept {
  switch (fc) {
  case FlowControl::NONE:
    return "NONE";
  case FlowControl::HARDWARE:
    return "HARDWARE";
  case FlowControl::SOFTWARE:
    return "SOFTWARE";
  default:
    return "UNKNOWN";
  }
}

} // namespace uart
} // namespace serial
} // namespace protocols
} // namespace apex
