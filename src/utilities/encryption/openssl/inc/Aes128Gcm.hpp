#ifndef APEX_UTILITIES_ENCRYPTION_AES128_GCM_HPP
#define APEX_UTILITIES_ENCRYPTION_AES128_GCM_HPP
/**
 * @file Aes128Gcm.hpp
 * @brief AES-128-GCM AEAD cipher using OpenSSL EVP.
 *
 * AES-128 provides sufficient security for most applications with faster
 * key schedule than AES-256. Common in TLS and constrained devices.
 */

#include "src/utilities/encryption/openssl/inc/AeadBase.hpp"

#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

/**
 * @brief AES-128-GCM AEAD cipher.
 *
 * Properties:
 *  - Key: 128 bits (16 bytes)
 *  - IV: 96 bits (12 bytes) - MUST be unique per key
 *  - Tag: 128 bits (16 bytes)
 *
 * Performance: Slightly faster than AES-256-GCM due to smaller key schedule.
 */
class Aes128Gcm : public AeadBase<Aes128Gcm> {
public:
  static constexpr std::size_t KEY_LENGTH = 16; ///< 128-bit key
  static constexpr std::size_t IV_LENGTH = 12;  ///< 96-bit IV (recommended)
  static constexpr std::size_t TAG_LENGTH = 16; ///< 128-bit tag

  /// Provider-friendly algorithm name (OpenSSL 3.x).
  static const char* fetchCipherName() noexcept;

  /// Pointer-based descriptor (OpenSSL 1.1.1 and fallback).
  static const EVP_CIPHER* fetchCipherAlgorithm() noexcept;

  /// No additional parameters needed for GCM.
  static const char* fetchParamName() noexcept { return nullptr; }

  /// No additional parameter value needed for GCM.
  static const char* fetchParamValue() noexcept { return nullptr; }
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Encrypt with AES-128-GCM (vector API).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
 */
template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status
aes128GcmEncrypt(bytes_span plaintext, bytes_span aad, const std::array<std::uint8_t, KeyLen>& key,
                 const std::array<std::uint8_t, IvLen>& iv, std::vector<std::uint8_t>& outCipher,
                 std::vector<std::uint8_t>& outTag) noexcept;

/**
 * @brief Decrypt with AES-128-GCM (vector API).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
 */
template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status aes128GcmDecrypt(bytes_span ciphertext, bytes_span aad,
                                   const std::array<std::uint8_t, KeyLen>& key,
                                   const std::array<std::uint8_t, IvLen>& iv,
                                   std::vector<std::uint8_t>& outPlain, bytes_span tag) noexcept;

/**
 * @brief Encrypt with AES-128-GCM (buffer API, zero-allocation).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
 */
template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status
aes128GcmEncrypt(bytes_span plaintext, bytes_span aad, const std::array<std::uint8_t, KeyLen>& key,
                 const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                 std::size_t& outLen, std::uint8_t* tagBuf, std::size_t& tagLen) noexcept;

/**
 * @brief Decrypt with AES-128-GCM (buffer API, zero-allocation).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
 */
template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status
aes128GcmDecrypt(bytes_span ciphertext, bytes_span aad, const std::array<std::uint8_t, KeyLen>& key,
                 const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                 std::size_t& outLen, const std::uint8_t* tag, std::size_t tagLen) noexcept;

/* ----------------------------- Implementation ----------------------------- */

template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status
aes128GcmEncrypt(bytes_span plaintext, bytes_span aad, const std::array<std::uint8_t, KeyLen>& key,
                 const std::array<std::uint8_t, IvLen>& iv, std::vector<std::uint8_t>& outCipher,
                 std::vector<std::uint8_t>& outTag) noexcept {
  static_assert(KeyLen == Aes128Gcm::KEY_LENGTH, "Key must be 16 bytes");
  static_assert(IvLen == Aes128Gcm::IV_LENGTH, "IV must be 12 bytes");

  Aes128Gcm cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.encrypt(plaintext, aad, outCipher, outTag);
}

template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status aes128GcmDecrypt(bytes_span ciphertext, bytes_span aad,
                                   const std::array<std::uint8_t, KeyLen>& key,
                                   const std::array<std::uint8_t, IvLen>& iv,
                                   std::vector<std::uint8_t>& outPlain, bytes_span tag) noexcept {
  static_assert(KeyLen == Aes128Gcm::KEY_LENGTH, "Key must be 16 bytes");
  static_assert(IvLen == Aes128Gcm::IV_LENGTH, "IV must be 12 bytes");

  Aes128Gcm cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.decrypt(ciphertext, aad, outPlain, tag);
}

template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status
aes128GcmEncrypt(bytes_span plaintext, bytes_span aad, const std::array<std::uint8_t, KeyLen>& key,
                 const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                 std::size_t& outLen, std::uint8_t* tagBuf, std::size_t& tagLen) noexcept {
  static_assert(KeyLen == Aes128Gcm::KEY_LENGTH, "Key must be 16 bytes");
  static_assert(IvLen == Aes128Gcm::IV_LENGTH, "IV must be 12 bytes");

  Aes128Gcm cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.encrypt(plaintext, aad, outBuf, outLen, tagBuf, tagLen);
}

template <std::size_t KeyLen, std::size_t IvLen>
Aes128Gcm::Status
aes128GcmDecrypt(bytes_span ciphertext, bytes_span aad, const std::array<std::uint8_t, KeyLen>& key,
                 const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                 std::size_t& outLen, const std::uint8_t* tag, std::size_t tagLen) noexcept {
  static_assert(KeyLen == Aes128Gcm::KEY_LENGTH, "Key must be 16 bytes");
  static_assert(IvLen == Aes128Gcm::IV_LENGTH, "IV must be 12 bytes");

  Aes128Gcm cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.decrypt(ciphertext, aad, outBuf, outLen, tag, tagLen);
}

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_AES128_GCM_HPP
