#ifndef APEX_UTILITIES_HELPERS_STRINGS_HPP
#define APEX_UTILITIES_HELPERS_STRINGS_HPP
/**
 * @file Strings.hpp
 * @brief String manipulation helpers for embedded/RT systems.
 *
 * Provides safe string operations with bounds checking and no heap allocation.
 * All functions are designed for fixed-size buffers common in embedded systems.
 *
 * @note RT-SAFE: All functions are noexcept with no allocations.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib> // strtol
#include <cstring> // strlen, strncmp, strcmp
#include <string>
#include <string_view>

namespace apex {
namespace helpers {
namespace strings {

/* ----------------------------- Search ----------------------------- */

/**
 * @brief Test whether a target string appears in a read-only string sequence.
 *
 * Exact, case-sensitive match against each element.
 *
 * @param vec    Read-only view over strings.
 * @param target Target string to search for.
 * @return true if any element equals target; false otherwise.
 * @note RT-SAFE: No allocation, linear scan.
 */
[[nodiscard]] inline bool containsString(apex::compat::rospan<std::string> vec,
                                         std::string_view target) noexcept {
  for (std::size_t i = 0; i < vec.size(); ++i) {
    if (vec.data()[i] == target) {
      return true;
    }
  }
  return false;
}

/* ----------------------------- Parsing ----------------------------- */

/**
 * @brief Skip leading whitespace (spaces and tabs).
 * @param ptr Pointer into string.
 * @return Pointer to first non-whitespace character (or end of string).
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline const char* skipWhitespace(const char* ptr) noexcept {
  if (ptr == nullptr) {
    return nullptr;
  }
  while (*ptr == ' ' || *ptr == '\t') {
    ++ptr;
  }
  return ptr;
}

/* ----------------------------- Manipulation ----------------------------- */

/**
 * @brief Build a null-terminated, zero-padded fixed char array from a string_view.
 * @tparam N Destination array size (includes space for null terminator).
 * @param sv Source string view.
 * @return std::array<char, N> with up to N-1 characters copied, then '\0', remainder zeroed.
 * @note RT-safe: constexpr, no allocation.
 */
template <std::size_t N> constexpr std::array<char, N> makeFixedChar(std::string_view sv) noexcept {
  std::array<char, N> out{};
  if constexpr (N == 0)
    return out;
  const std::size_t CAP = N - 1;
  const std::size_t M = (sv.size() < CAP) ? sv.size() : CAP;
  for (std::size_t i = 0; i < M; ++i)
    out[i] = sv[i];
  out[M] = '\0';
  return out;
}

/**
 * @brief Strip trailing whitespace in-place.
 * @param buf Buffer to modify (null-terminated).
 * @param len Current string length (will be updated).
 * @note RT-SAFE: No allocation.
 */
inline void stripTrailingWhitespace(char* buf, std::size_t& len) noexcept {
  if (buf == nullptr) {
    return;
  }

  while (len > 0) {
    const char C = buf[len - 1];
    if (C == '\n' || C == '\r' || C == ' ' || C == '\t') {
      --len;
      buf[len] = '\0';
    } else {
      break;
    }
  }
}

/**
 * @brief Copy string into fixed-size array with null termination.
 * @tparam N Array size.
 * @param dest Destination array.
 * @param src Source string (null-terminated).
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline void copyToFixedArray(std::array<char, N>& dest, const char* src) noexcept {
  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  std::size_t i = 0;
  while (i < N - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/**
 * @brief Copy exactly len bytes into fixed-size array with null termination.
 * @tparam N Array size.
 * @param dest Destination array.
 * @param src Source buffer (not necessarily null-terminated).
 * @param len Number of bytes to copy from src.
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline void copyToFixedArray(std::array<char, N>& dest, const char* src, std::size_t len) noexcept {
  if (src == nullptr || len == 0) {
    dest[0] = '\0';
    return;
  }

  const std::size_t COPY_LEN = (len < N - 1) ? len : (N - 1);
  std::memcpy(dest.data(), src, COPY_LEN);
  dest[COPY_LEN] = '\0';
}

/**
 * @brief Copy std::string into fixed-size array with null termination.
 * @tparam N Array size.
 * @param dest Destination array.
 * @param src Source string.
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline void copyToFixedArray(std::array<char, N>& dest, const std::string& src) noexcept {
  copyToFixedArray(dest, src.c_str(), src.size());
}

/**
 * @brief Copy string into raw buffer with null termination.
 * @param dest Destination buffer.
 * @param destSize Size of destination buffer.
 * @param src Source string (null-terminated).
 * @note RT-SAFE: No allocation, bounded operation.
 */
inline void copyToBuffer(char* dest, std::size_t destSize, const char* src) noexcept {
  if (dest == nullptr || destSize == 0) {
    return;
  }

  if (src == nullptr) {
    dest[0] = '\0';
    return;
  }

  std::size_t i = 0;
  while (i < destSize - 1 && src[i] != '\0') {
    dest[i] = src[i];
    ++i;
  }
  dest[i] = '\0';
}

/**
 * @brief Check if string starts with prefix.
 * @param str String to check.
 * @param prefix Prefix to look for.
 * @return true if str starts with prefix.
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline bool startsWith(const char* str, const char* prefix) noexcept {
  if (str == nullptr || prefix == nullptr) {
    return false;
  }
  const std::size_t PREFIX_LEN = std::strlen(prefix);
  return std::strncmp(str, prefix, PREFIX_LEN) == 0;
}

/**
 * @brief Check if string ends with suffix.
 * @param str String to check.
 * @param suffix Suffix to look for.
 * @return true if str ends with suffix.
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline bool endsWith(const char* str, const char* suffix) noexcept {
  if (str == nullptr || suffix == nullptr) {
    return false;
  }

  const std::size_t STR_LEN = std::strlen(str);
  const std::size_t SUFFIX_LEN = std::strlen(suffix);

  if (SUFFIX_LEN > STR_LEN) {
    return false;
  }

  return std::strcmp(str + STR_LEN - SUFFIX_LEN, suffix) == 0;
}

/**
 * @brief Extract numeric index from name with prefix.
 * @param name Name to parse (e.g., "cpu0", "mc1", "node2").
 * @param prefix Expected prefix (e.g., "cpu", "mc", "node").
 * @return Extracted index, or -1 if parsing fails.
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline std::int32_t parseIndexFromName(const char* name,
                                                     const char* prefix) noexcept {
  if (name == nullptr || prefix == nullptr) {
    return -1;
  }

  const std::size_t PREFIX_LEN = std::strlen(prefix);
  if (std::strncmp(name, prefix, PREFIX_LEN) != 0) {
    return -1;
  }

  if (name[PREFIX_LEN] == '\0') {
    return -1;
  }

  char* end = nullptr;
  const long VAL = std::strtol(name + PREFIX_LEN, &end, 10);

  if (end == name + PREFIX_LEN || *end != '\0') {
    return -1;
  }

  if (VAL < 0 || VAL > INT32_MAX) {
    return -1;
  }

  return static_cast<std::int32_t>(VAL);
}

/* ----------------------------- Hex Utilities ----------------------------- */

/**
 * @brief Check if character is a valid hexadecimal digit.
 * @param c Character to check.
 * @return true if c is 0-9, a-f, or A-F.
 * @note RT-SAFE: Pure constexpr.
 */
[[nodiscard]] constexpr bool isHexDigit(char c) noexcept {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/**
 * @brief Convert a hex character to its 4-bit value.
 * @param c Hex character (0-9, a-f, A-F).
 * @return Value 0-15, or 0xFF if invalid.
 * @note RT-SAFE: Pure constexpr.
 */
[[nodiscard]] constexpr std::uint8_t hexCharToNibble(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return static_cast<std::uint8_t>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<std::uint8_t>(c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<std::uint8_t>(c - 'A' + 10);
  }
  return 0xFF;
}

/**
 * @brief Convert a 4-bit value to lowercase hex character.
 * @param nibble Value 0-15.
 * @return Hex character '0'-'9' or 'a'-'f'. Returns '?' for invalid input.
 * @note RT-SAFE: Pure constexpr.
 */
[[nodiscard]] constexpr char nibbleToHexChar(std::uint8_t nibble) noexcept {
  constexpr char HEX_CHARS[] = "0123456789abcdef";
  return (nibble < 16) ? HEX_CHARS[nibble] : '?';
}

/**
 * @brief Parse a hex string to uint32_t.
 *
 * Stops at first non-hex character or end of string.
 * Handles optional "0x" or "0X" prefix.
 *
 * @param str Hex string to parse.
 * @param[out] value Parsed value.
 * @return Number of hex digits consumed (0 on failure).
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline std::size_t parseHexU32(const char* str, std::uint32_t& value) noexcept {
  value = 0;
  if (str == nullptr) {
    return 0;
  }

  const char* ptr = str;

  // Skip optional 0x/0X prefix
  if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
    ptr += 2;
  }

  std::size_t digits = 0;
  while (isHexDigit(*ptr) && digits < 8) {
    const std::uint8_t NIBBLE = hexCharToNibble(*ptr);
    value = (value << 4) | NIBBLE;
    ++ptr;
    ++digits;
  }

  return digits;
}

/**
 * @brief Parse a hex string to uint64_t.
 *
 * Stops at first non-hex character or end of string.
 * Handles optional "0x" or "0X" prefix.
 *
 * @param str Hex string to parse.
 * @param[out] value Parsed value.
 * @return Number of hex digits consumed (0 on failure).
 * @note RT-SAFE: No allocation.
 */
[[nodiscard]] inline std::size_t parseHexU64(const char* str, std::uint64_t& value) noexcept {
  value = 0;
  if (str == nullptr) {
    return 0;
  }

  const char* ptr = str;

  // Skip optional 0x/0X prefix
  if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
    ptr += 2;
  }

  std::size_t digits = 0;
  while (isHexDigit(*ptr) && digits < 16) {
    const std::uint8_t NIBBLE = hexCharToNibble(*ptr);
    value = (value << 4) | NIBBLE;
    ++ptr;
    ++digits;
  }

  return digits;
}

/**
 * @brief Convert bytes to hex string in fixed buffer.
 *
 * Output is lowercase hex with no separators. Buffer must be at least
 * 2*dataLen + 1 bytes for null terminator.
 *
 * @param data Input bytes.
 * @param dataLen Number of bytes to convert.
 * @param buf Output buffer.
 * @param bufLen Size of output buffer.
 * @return Number of hex characters written (excluding null terminator).
 * @note RT-SAFE: No allocation, bounded operation.
 */
inline std::size_t toHexBuffer(const std::uint8_t* data, std::size_t dataLen, char* buf,
                               std::size_t bufLen) noexcept {
  if (buf == nullptr || bufLen == 0) {
    return 0;
  }

  if (data == nullptr || dataLen == 0) {
    buf[0] = '\0';
    return 0;
  }

  // Need 2 chars per byte plus null terminator
  const std::size_t MAX_BYTES = (bufLen - 1) / 2;
  const std::size_t BYTES_TO_CONVERT = (dataLen < MAX_BYTES) ? dataLen : MAX_BYTES;

  std::size_t pos = 0;
  for (std::size_t i = 0; i < BYTES_TO_CONVERT; ++i) {
    buf[pos++] = nibbleToHexChar(static_cast<std::uint8_t>(data[i] >> 4));
    buf[pos++] = nibbleToHexChar(static_cast<std::uint8_t>(data[i] & 0x0F));
  }
  buf[pos] = '\0';

  return pos;
}

/**
 * @brief Convert bytes to hex string in fixed array.
 *
 * @tparam N Output array size.
 * @param data Input bytes.
 * @param dataLen Number of bytes to convert.
 * @param buf Output array.
 * @return Number of hex characters written (excluding null terminator).
 * @note RT-SAFE: No allocation, bounded operation.
 */
template <std::size_t N>
inline std::size_t toHexArray(const std::uint8_t* data, std::size_t dataLen,
                              std::array<char, N>& buf) noexcept {
  return toHexBuffer(data, dataLen, buf.data(), N);
}

/**
 * @brief Parse hex string to bytes in fixed buffer.
 *
 * Stops at first non-hex character or end of buffer.
 * Handles optional "0x" or "0X" prefix.
 *
 * @param hexStr Input hex string.
 * @param buf Output buffer.
 * @param bufLen Size of output buffer.
 * @return Number of bytes written.
 * @note RT-SAFE: No allocation, bounded operation.
 */
inline std::size_t fromHexBuffer(const char* hexStr, std::uint8_t* buf,
                                 std::size_t bufLen) noexcept {
  if (hexStr == nullptr || buf == nullptr || bufLen == 0) {
    return 0;
  }

  const char* ptr = hexStr;

  // Skip optional 0x/0X prefix
  if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X')) {
    ptr += 2;
  }

  std::size_t bytesWritten = 0;
  while (bytesWritten < bufLen) {
    if (!isHexDigit(ptr[0]) || !isHexDigit(ptr[1])) {
      break;
    }

    const std::uint8_t HIGH = hexCharToNibble(ptr[0]);
    const std::uint8_t LOW = hexCharToNibble(ptr[1]);
    buf[bytesWritten++] = static_cast<std::uint8_t>((HIGH << 4) | LOW);
    ptr += 2;
  }

  return bytesWritten;
}

} // namespace strings
} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_STRINGS_HPP
