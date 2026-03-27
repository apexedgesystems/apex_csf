/**
 * @file RfcommConfig.cpp
 * @brief Implementation of BluetoothAddress and RfcommConfig.
 */

#include "RfcommConfig.hpp"

#include <cctype>
#include <cstring>

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- File Helpers ----------------------------- */

namespace {

/**
 * @brief Parse a single hex digit.
 * @param c Character to parse.
 * @return Value 0-15, or 255 on invalid input.
 */
std::uint8_t parseHexDigit(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<std::uint8_t>(c - 'A' + 10);
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  return 255;
}

/**
 * @brief Parse a two-digit hex byte.
 * @param str Pointer to two hex characters.
 * @param out Output byte value.
 * @return true on success, false on invalid input.
 */
bool parseHexByte(const char* str, std::uint8_t& out) noexcept {
  std::uint8_t high = parseHexDigit(str[0]);
  std::uint8_t low = parseHexDigit(str[1]);
  if (high == 255 || low == 255) {
    return false;
  }
  out = static_cast<std::uint8_t>((high << 4) | low);
  return true;
}

} // namespace

/* ----------------------------- BluetoothAddress ----------------------------- */

BluetoothAddress BluetoothAddress::fromString(const char* str) noexcept {
  BluetoothAddress addr{};

  if (str == nullptr) {
    return addr;
  }

  // Expected format: "XX:XX:XX:XX:XX:XX" (17 characters)
  std::size_t len = std::strlen(str);
  if (len != 17) {
    return addr;
  }

  // Parse each byte
  for (std::size_t i = 0; i < 6; ++i) {
    std::size_t pos = i * 3;

    // Parse hex byte
    if (!parseHexByte(&str[pos], addr.bytes[i])) {
      addr.bytes = {};
      return addr;
    }

    // Check separator (except after last byte)
    if (i < 5 && str[pos + 2] != ':') {
      addr.bytes = {};
      return addr;
    }
  }

  return addr;
}

std::size_t BluetoothAddress::toString(char* buf, std::size_t bufSize) const noexcept {
  if (buf == nullptr || bufSize == 0) {
    return 0;
  }

  // Need at least 18 bytes for "XX:XX:XX:XX:XX:XX\0"
  if (bufSize < BLUETOOTH_ADDRESS_STRING_SIZE) {
    buf[0] = '\0';
    return 0;
  }

  static constexpr char HEX_CHARS[] = "0123456789ABCDEF";

  char* p = buf;
  for (std::size_t i = 0; i < 6; ++i) {
    if (i > 0) {
      *p++ = ':';
    }
    *p++ = HEX_CHARS[(bytes[i] >> 4) & 0x0F];
    *p++ = HEX_CHARS[bytes[i] & 0x0F];
  }
  *p = '\0';

  return static_cast<std::size_t>(p - buf);
}

bool BluetoothAddress::isValid() const noexcept {
  for (std::uint8_t b : bytes) {
    if (b != 0) {
      return true;
    }
  }
  return false;
}

bool BluetoothAddress::isBroadcast() const noexcept {
  for (std::uint8_t b : bytes) {
    if (b != 0xFF) {
      return false;
    }
  }
  return true;
}

/* ----------------------------- RfcommConfig ----------------------------- */

bool RfcommConfig::isValid() const noexcept {
  // Remote address must be set
  if (!remoteAddress.isValid()) {
    return false;
  }

  // Channel must be in valid range
  if (channel < RFCOMM_CHANNEL_MIN || channel > RFCOMM_CHANNEL_MAX) {
    return false;
  }

  return true;
}

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex
