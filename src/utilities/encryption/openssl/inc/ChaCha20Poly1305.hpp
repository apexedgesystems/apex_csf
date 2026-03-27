#ifndef APEX_UTILITIES_ENCRYPTION_CHACHA20_POLY1305_HPP
#define APEX_UTILITIES_ENCRYPTION_CHACHA20_POLY1305_HPP
/**
 * @file ChaCha20Poly1305.hpp
 * @brief ChaCha20-Poly1305 AEAD cipher using OpenSSL EVP.
 *
 * ChaCha20-Poly1305 is the preferred AEAD for platforms without AES-NI
 * hardware acceleration (ARM Cortex-M/A without crypto extensions).
 * On such platforms, it is 3-5x faster than AES-GCM.
 *
 * Used in: WireGuard, TLS 1.3, SSH, QUIC, noise protocol.
 */

#include "src/utilities/encryption/openssl/inc/AeadBase.hpp"

#include <array>
#include <cstdint>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

/**
 * @brief ChaCha20-Poly1305 AEAD cipher.
 *
 * Properties:
 *  - Key: 256 bits (32 bytes)
 *  - Nonce: 96 bits (12 bytes) - MUST be unique per key
 *  - Tag: 128 bits (16 bytes)
 *  - No padding required (stream cipher)
 *
 * Performance note: On ARM without crypto extensions, ChaCha20-Poly1305
 * significantly outperforms AES-GCM. On x86 with AES-NI, AES-GCM is faster.
 */
class ChaCha20Poly1305 : public AeadBase<ChaCha20Poly1305> {
public:
  static constexpr std::size_t KEY_LENGTH = 32; ///< 256-bit key
  static constexpr std::size_t IV_LENGTH = 12;  ///< 96-bit nonce
  static constexpr std::size_t TAG_LENGTH = 16; ///< 128-bit tag

  /// Provider-friendly algorithm name (OpenSSL 3.x).
  static const char* fetchCipherName() noexcept;

  /// Pointer-based descriptor (OpenSSL 1.1.1 and fallback).
  static const EVP_CIPHER* fetchCipherAlgorithm() noexcept;

  /// No additional parameters needed for ChaCha20-Poly1305.
  static const char* fetchParamName() noexcept { return nullptr; }

  /// No additional parameter value needed for ChaCha20-Poly1305.
  static const char* fetchParamValue() noexcept { return nullptr; }

  /// Alternate name for IV (nonce) for clarity.
  static constexpr std::size_t NONCE_LENGTH = IV_LENGTH;
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Encrypt with ChaCha20-Poly1305 (vector API).
 * @param plaintext Data to encrypt.
 * @param aad Additional authenticated data (may be empty).
 * @param key 32-byte key.
 * @param iv 12-byte nonce (MUST be unique per key).
 * @param[out] outCipher Ciphertext output.
 * @param[out] outTag 16-byte authentication tag.
 * @return Status code.
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
 */
template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status chacha20Poly1305Encrypt(bytes_span plaintext, bytes_span aad,
                                                 const std::array<std::uint8_t, KeyLen>& key,
                                                 const std::array<std::uint8_t, IvLen>& iv,
                                                 std::vector<std::uint8_t>& outCipher,
                                                 std::vector<std::uint8_t>& outTag) noexcept;

/**
 * @brief Decrypt with ChaCha20-Poly1305 (vector API).
 * @param ciphertext Data to decrypt.
 * @param aad Additional authenticated data (must match encryption).
 * @param key 32-byte key.
 * @param iv 12-byte nonce.
 * @param[out] outPlain Plaintext output.
 * @param tag 16-byte authentication tag to verify.
 * @return Status code (ERROR_AUTH if tag verification fails).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation after reserve.
 */
template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status chacha20Poly1305Decrypt(bytes_span ciphertext, bytes_span aad,
                                                 const std::array<std::uint8_t, KeyLen>& key,
                                                 const std::array<std::uint8_t, IvLen>& iv,
                                                 std::vector<std::uint8_t>& outPlain,
                                                 bytes_span tag) noexcept;

/**
 * @brief Encrypt with ChaCha20-Poly1305 (buffer API, zero-allocation).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
 */
template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status
chacha20Poly1305Encrypt(bytes_span plaintext, bytes_span aad,
                        const std::array<std::uint8_t, KeyLen>& key,
                        const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                        std::size_t& outLen, std::uint8_t* tagBuf, std::size_t& tagLen) noexcept;

/**
 * @brief Decrypt with ChaCha20-Poly1305 (buffer API, zero-allocation).
 * @note RT-safe: Bounded OpenSSL calls, no heap allocation.
 */
template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status
chacha20Poly1305Decrypt(bytes_span ciphertext, bytes_span aad,
                        const std::array<std::uint8_t, KeyLen>& key,
                        const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                        std::size_t& outLen, const std::uint8_t* tag, std::size_t tagLen) noexcept;

/* ----------------------------- Implementation ----------------------------- */

template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status chacha20Poly1305Encrypt(bytes_span plaintext, bytes_span aad,
                                                 const std::array<std::uint8_t, KeyLen>& key,
                                                 const std::array<std::uint8_t, IvLen>& iv,
                                                 std::vector<std::uint8_t>& outCipher,
                                                 std::vector<std::uint8_t>& outTag) noexcept {
  static_assert(KeyLen == ChaCha20Poly1305::KEY_LENGTH, "Key must be 32 bytes");
  static_assert(IvLen == ChaCha20Poly1305::IV_LENGTH, "IV must be 12 bytes");

  ChaCha20Poly1305 cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.encrypt(plaintext, aad, outCipher, outTag);
}

template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status chacha20Poly1305Decrypt(bytes_span ciphertext, bytes_span aad,
                                                 const std::array<std::uint8_t, KeyLen>& key,
                                                 const std::array<std::uint8_t, IvLen>& iv,
                                                 std::vector<std::uint8_t>& outPlain,
                                                 bytes_span tag) noexcept {
  static_assert(KeyLen == ChaCha20Poly1305::KEY_LENGTH, "Key must be 32 bytes");
  static_assert(IvLen == ChaCha20Poly1305::IV_LENGTH, "IV must be 12 bytes");

  ChaCha20Poly1305 cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.decrypt(ciphertext, aad, outPlain, tag);
}

template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status
chacha20Poly1305Encrypt(bytes_span plaintext, bytes_span aad,
                        const std::array<std::uint8_t, KeyLen>& key,
                        const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                        std::size_t& outLen, std::uint8_t* tagBuf, std::size_t& tagLen) noexcept {
  static_assert(KeyLen == ChaCha20Poly1305::KEY_LENGTH, "Key must be 32 bytes");
  static_assert(IvLen == ChaCha20Poly1305::IV_LENGTH, "IV must be 12 bytes");

  ChaCha20Poly1305 cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.encrypt(plaintext, aad, outBuf, outLen, tagBuf, tagLen);
}

template <std::size_t KeyLen, std::size_t IvLen>
ChaCha20Poly1305::Status
chacha20Poly1305Decrypt(bytes_span ciphertext, bytes_span aad,
                        const std::array<std::uint8_t, KeyLen>& key,
                        const std::array<std::uint8_t, IvLen>& iv, std::uint8_t* outBuf,
                        std::size_t& outLen, const std::uint8_t* tag, std::size_t tagLen) noexcept {
  static_assert(KeyLen == ChaCha20Poly1305::KEY_LENGTH, "Key must be 32 bytes");
  static_assert(IvLen == ChaCha20Poly1305::IV_LENGTH, "IV must be 12 bytes");

  ChaCha20Poly1305 cipher;
  cipher.setKey(bytes_span{key.data(), key.size()});
  cipher.setIv(bytes_span{iv.data(), iv.size()});
  return cipher.decrypt(ciphertext, aad, outBuf, outLen, tag, tagLen);
}

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_CHACHA20_POLY1305_HPP
