/**
 * @file SecureRandom.cpp
 * @brief Cryptographically secure random number generation implementation.
 */

#include "src/utilities/encryption/openssl/inc/SecureRandom.hpp"

#include <openssl/rand.h>

#include <cstring>

namespace apex::encryption {

SecureRandom::Status SecureRandom::fill(std::uint8_t* buffer, std::size_t size) noexcept {
  if (size == 0) {
    return Status::SUCCESS;
  }

  if (buffer == nullptr) {
    return Status::ERROR_INVALID_SIZE;
  }

  // RAND_bytes returns 1 on success, 0 on failure
  if (RAND_bytes(buffer, static_cast<int>(size)) != 1) {
    return Status::ERROR_ENTROPY;
  }

  return Status::SUCCESS;
}

SecureRandom::Status SecureRandom::generate(std::size_t size,
                                            std::vector<std::uint8_t>& out) noexcept {
  out.resize(size);
  if (size == 0) {
    return Status::SUCCESS;
  }
  return fill(out.data(), size);
}

SecureRandom::Status SecureRandom::randomUint32(std::uint32_t& value) noexcept {
  std::uint8_t buf[sizeof(std::uint32_t)];
  Status st = fill(buf, sizeof(buf));
  if (st == Status::SUCCESS) {
    std::memcpy(&value, buf, sizeof(value));
  }
  return st;
}

SecureRandom::Status SecureRandom::randomUint64(std::uint64_t& value) noexcept {
  std::uint8_t buf[sizeof(std::uint64_t)];
  Status st = fill(buf, sizeof(buf));
  if (st == Status::SUCCESS) {
    std::memcpy(&value, buf, sizeof(value));
  }
  return st;
}

bool SecureRandom::isSeeded() noexcept { return RAND_status() == 1; }

} // namespace apex::encryption
