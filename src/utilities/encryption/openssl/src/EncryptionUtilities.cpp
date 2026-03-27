/**
 * @file EncryptionUtilities.cpp
 * @brief Common encryption utility implementations for random byte generation and secure memory
 * operations.
 */

#include "src/utilities/encryption/openssl/inc/EncryptionUtilities.hpp"

#include <cctype>
#include <openssl/crypto.h> // for OPENSSL_cleanse
#include <openssl/rand.h>

namespace apex::encryption {

uint8_t generateRandomBytes(compat::mutable_bytes_span outBuf) noexcept {
  // RAND_bytes returns 1 on success
  return (RAND_bytes(outBuf.data(), static_cast<int>(outBuf.size())) == 1) ? 0 : 1;
}

std::vector<uint8_t> generateRandomVector(size_t len) noexcept {
  std::vector<uint8_t> v;
  v.resize(len);
  if (generateRandomBytes(v) != 0) {
    v.clear();
  }
  return v;
}

std::string toHex(compat::bytes_span data) {
  static constexpr char DIGITS[] = "0123456789abcdef";
  std::string out;
  out.reserve(data.size() * 2);

  for (uint8_t byte : data) {
    out.push_back(DIGITS[byte >> 4]);
    out.push_back(DIGITS[byte & 0x0F]);
  }
  return out;
}

bool fromHex(const std::string& hexStr, std::vector<uint8_t>& outVec) noexcept {
  size_t len = hexStr.size();
  if ((len & 1) != 0)
    return false;

  outVec.clear();
  outVec.reserve(len / 2);

  auto val = [&](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };

  for (size_t i = 0; i < len; i += 2) {
    int hi = val(hexStr[i]);
    int lo = val(hexStr[i + 1]);
    if (hi < 0 || lo < 0)
      return false;
    outVec.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return true;
}

bool constantTimeCompare(compat::bytes_span a, compat::bytes_span b) noexcept {
  if (a.size() != b.size())
    return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.size(); ++i) {
    diff |= a[i] ^ b[i];
  }
  return diff == 0;
}

void secureZeroMemory(void* ptr, size_t len) noexcept {
  if (ptr && len) {
    OPENSSL_cleanse(ptr, len);
  }
}

} // namespace apex::encryption
