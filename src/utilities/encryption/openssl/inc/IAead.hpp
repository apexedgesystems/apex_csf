#ifndef APEX_UTILITIES_ENCRYPTION_IAEAD_HPP
#define APEX_UTILITIES_ENCRYPTION_IAEAD_HPP
/* ----------------------------- AeadAdapter ----------------------------- */

/**
 * @file IAead.hpp
 * @brief Virtual interface for runtime-polymorphic AEAD cipher selection.
 *
 * Use this when the AEAD algorithm must be selected at runtime (config-driven,
 * protocol negotiation, etc.). For compile-time selection, use the CRTP classes
 * directly (Aes256Gcm) for zero-overhead.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace apex::encryption {

using bytes_span = compat::bytes_span;

/* ----------------------------- IAead ----------------------------- */

/* ----------------------------- AeadAdapter ----------------------------- */

/**
 * @brief Abstract interface for AEAD (Authenticated Encryption with Associated Data).
 *
 * Enables runtime algorithm selection with one vtable lookup per call.
 * NOT RT-safe due to virtual dispatch; use CRTP classes directly in
 * hard real-time paths where timing must be fully deterministic.
 */
class IAead {
public:
  /// Common status codes for AEAD operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_INVALID_KEY = 1,
    ERROR_INVALID_IV = 2,
    ERROR_OUTPUT_TOO_SMALL = 3,
    ERROR_INIT = 4,
    ERROR_AAD = 5,
    ERROR_UPDATE = 6,
    ERROR_FINAL = 7,
    ERROR_AUTH = 8,
    ERROR_NULL_ALGORITHM = 9,
    ERROR_UNKNOWN = 255
  };

  virtual ~IAead() = default;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t keySize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t ivSize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t tagSize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual const char* algorithmName() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  virtual void setKey(bytes_span key) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  virtual void setIv(bytes_span iv) = 0;

  /** @note NOT RT-safe: Virtual dispatch, possible allocation. */
  [[nodiscard]] virtual Status encrypt(bytes_span plaintext, bytes_span aad,
                                       std::vector<std::uint8_t>& outCipher,
                                       std::vector<std::uint8_t>& outTag) = 0;

  /** @note NOT RT-safe: Virtual dispatch, possible allocation. */
  [[nodiscard]] virtual Status decrypt(bytes_span ciphertext, bytes_span aad,
                                       std::vector<std::uint8_t>& outPlain, bytes_span tag) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual Status encrypt(bytes_span plaintext, bytes_span aad, std::uint8_t* outBuf,
                                       std::size_t& outLen, std::uint8_t* tagBuf,
                                       std::size_t& tagLen) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual Status decrypt(bytes_span ciphertext, bytes_span aad, std::uint8_t* outBuf,
                                       std::size_t& outLen, const std::uint8_t* tag,
                                       std::size_t tagLen) = 0;

protected:
  IAead() = default;
  IAead(const IAead&) = default;
  IAead& operator=(const IAead&) = default;
  IAead(IAead&&) = default;
  IAead& operator=(IAead&&) = default;
};

/* ----------------------------- AeadAdapter ----------------------------- */

/**
 * @brief Adapter that bridges a CRTP AEAD class to the IAead interface.
 *
 * @tparam Aead A CRTP-derived AEAD class (e.g., Aes256Gcm).
 *
 * Example:
 * @code
 *   std::unique_ptr<IAead> aead = std::make_unique<AeadAdapter<Aes256Gcm>>();
 *   aead->setKey(keySpan);
 *   aead->setIv(ivSpan);
 *   aead->encrypt(plaintext, aad, ciphertext, tag);
 * @endcode
 */
template <typename Aead> class AeadAdapter final : public IAead {
public:
  AeadAdapter() = default;

  [[nodiscard]] std::size_t keySize() const noexcept override { return Aead::KEY_LENGTH; }

  [[nodiscard]] std::size_t ivSize() const noexcept override { return Aead::IV_LENGTH; }

  [[nodiscard]] std::size_t tagSize() const noexcept override { return Aead::TAG_LENGTH; }

  [[nodiscard]] const char* algorithmName() const noexcept override {
    return Aead::fetchCipherName();
  }

  void setKey(bytes_span key) override { impl_.setKey(key); }

  void setIv(bytes_span iv) override { impl_.setIv(iv); }

  [[nodiscard]] Status encrypt(bytes_span plaintext, bytes_span aad,
                               std::vector<std::uint8_t>& outCipher,
                               std::vector<std::uint8_t>& outTag) override {
    auto result = impl_.encrypt(plaintext, aad, outCipher, outTag);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status decrypt(bytes_span ciphertext, bytes_span aad,
                               std::vector<std::uint8_t>& outPlain, bytes_span tag) override {
    auto result = impl_.decrypt(ciphertext, aad, outPlain, tag);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status encrypt(bytes_span plaintext, bytes_span aad, std::uint8_t* outBuf,
                               std::size_t& outLen, std::uint8_t* tagBuf,
                               std::size_t& tagLen) override {
    auto result = impl_.encrypt(plaintext, aad, outBuf, outLen, tagBuf, tagLen);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status decrypt(bytes_span ciphertext, bytes_span aad, std::uint8_t* outBuf,
                               std::size_t& outLen, const std::uint8_t* tag,
                               std::size_t tagLen) override {
    auto result = impl_.decrypt(ciphertext, aad, outBuf, outLen, tag, tagLen);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

private:
  Aead impl_;
};

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_IAEAD_HPP
