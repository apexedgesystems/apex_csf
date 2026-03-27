/**
 * @file Hkdf.cpp
 * @brief HKDF (HMAC-based Key Derivation Function) implementation.
 *
 * Implements RFC 5869 using OpenSSL HMAC primitives for portability
 * across OpenSSL 1.1.1 and 3.x.
 */

#include "src/utilities/encryption/openssl/inc/Hkdf.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <array>
#include <cstring>

namespace apex::encryption {

namespace {

/// Maximum hash output size we support (SHA-512 = 64 bytes).
constexpr std::size_t MAX_HASH_SIZE = 64;

/// Get EVP_MD for hash type.
const EVP_MD* getEvpMd(Hkdf::HashType hashType) noexcept {
  switch (hashType) {
  case Hkdf::HashType::SHA256:
    return EVP_sha256();
  case Hkdf::HashType::SHA512:
    return EVP_sha512();
  default:
    return nullptr;
  }
}

/// Compute HMAC.
bool computeHmac(const EVP_MD* md, const std::uint8_t* key, std::size_t keyLen,
                 const std::uint8_t* data, std::size_t dataLen, std::uint8_t* out,
                 unsigned int* outLen) noexcept {
  return HMAC(md, key, static_cast<int>(keyLen), data, dataLen, out, outLen) != nullptr;
}

} // namespace

/* ----------------------------- Hkdf Methods ----------------------------- */

std::size_t Hkdf::hashSize(HashType hashType) noexcept {
  switch (hashType) {
  case HashType::SHA256:
    return 32;
  case HashType::SHA512:
    return 64;
  default:
    return 0;
  }
}

std::size_t Hkdf::maxOutputLength(HashType hashType) noexcept { return 255 * hashSize(hashType); }

Hkdf::Status Hkdf::extract(HashType hashType, bytes_span ikm, bytes_span salt,
                           std::vector<std::uint8_t>& outPrk) noexcept {

  const EVP_MD* md = getEvpMd(hashType);
  if (!md) {
    return Status::ERROR_INVALID_HASH;
  }

  const std::size_t HASH_LEN = hashSize(hashType);
  outPrk.resize(HASH_LEN);

  // If salt is empty, use zero-filled salt of hash length
  std::array<std::uint8_t, MAX_HASH_SIZE> zeroSalt{};
  const std::uint8_t* saltPtr = salt.empty() ? zeroSalt.data() : salt.data();
  const std::size_t saltLen = salt.empty() ? HASH_LEN : salt.size();

  // PRK = HMAC(salt, IKM)
  unsigned int prkLen = 0;
  if (!computeHmac(md, saltPtr, saltLen, ikm.data(), ikm.size(), outPrk.data(), &prkLen)) {
    return Status::ERROR_EXTRACT_FAILED;
  }

  return Status::SUCCESS;
}

Hkdf::Status Hkdf::expand(HashType hashType, bytes_span prk, bytes_span info,
                          std::size_t outputLength, std::vector<std::uint8_t>& outKey) noexcept {

  const EVP_MD* md = getEvpMd(hashType);
  if (!md) {
    return Status::ERROR_INVALID_HASH;
  }

  const std::size_t HASH_LEN = hashSize(hashType);

  // Check output length limit (255 * hash_len per RFC 5869)
  if (outputLength > maxOutputLength(hashType)) {
    return Status::ERROR_OUTPUT_TOO_LONG;
  }

  outKey.resize(outputLength);

  // N = ceil(L / HashLen)
  const std::size_t N = (outputLength + HASH_LEN - 1) / HASH_LEN;

  // T = T(1) | T(2) | ... | T(N)
  // T(i) = HMAC(PRK, T(i-1) | info | i)
  std::array<std::uint8_t, MAX_HASH_SIZE> tPrev{};
  std::size_t tPrevLen = 0;
  std::size_t offset = 0;

  // Buffer for HMAC input: T(i-1) | info | counter
  std::vector<std::uint8_t> hmacInput;
  hmacInput.reserve(HASH_LEN + info.size() + 1);

  for (std::size_t i = 1; i <= N; ++i) {
    hmacInput.clear();

    // T(i-1) (empty for i=1)
    if (tPrevLen > 0) {
      hmacInput.insert(hmacInput.end(), tPrev.begin(), tPrev.begin() + tPrevLen);
    }

    // info
    if (!info.empty()) {
      hmacInput.insert(hmacInput.end(), info.begin(), info.end());
    }

    // counter (1-255)
    hmacInput.push_back(static_cast<std::uint8_t>(i));

    // T(i) = HMAC(PRK, T(i-1) | info | i)
    unsigned int tLen = 0;
    if (!computeHmac(md, prk.data(), prk.size(), hmacInput.data(), hmacInput.size(), tPrev.data(),
                     &tLen)) {
      return Status::ERROR_EXPAND_FAILED;
    }
    tPrevLen = tLen;

    // Copy to output (may be partial for last block)
    const std::size_t COPY_LEN = std::min(static_cast<std::size_t>(tLen), outputLength - offset);
    std::memcpy(outKey.data() + offset, tPrev.data(), COPY_LEN);
    offset += COPY_LEN;
  }

  return Status::SUCCESS;
}

Hkdf::Status Hkdf::derive(HashType hashType, bytes_span ikm, bytes_span salt, bytes_span info,
                          std::size_t outputLength, std::vector<std::uint8_t>& outKey) noexcept {

  // Extract
  std::vector<std::uint8_t> prk;
  Status st = extract(hashType, ikm, salt, prk);
  if (st != Status::SUCCESS) {
    return st;
  }

  // Expand
  return expand(hashType, bytes_span{prk.data(), prk.size()}, info, outputLength, outKey);
}

Hkdf::Status Hkdf::derive(HashType hashType, bytes_span ikm, bytes_span salt, bytes_span info,
                          std::uint8_t* outBuf, std::size_t& outLen) noexcept {

  std::vector<std::uint8_t> key;
  Status st = derive(hashType, ikm, salt, info, outLen, key);
  if (st != Status::SUCCESS) {
    return st;
  }

  std::memcpy(outBuf, key.data(), key.size());
  outLen = key.size();
  return Status::SUCCESS;
}

} // namespace apex::encryption
