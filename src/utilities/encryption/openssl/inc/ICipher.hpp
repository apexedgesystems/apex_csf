#ifndef APEX_UTILITIES_ENCRYPTION_ICIPHER_HPP
#define APEX_UTILITIES_ENCRYPTION_ICIPHER_HPP
/* ----------------------------- CipherAdapter ----------------------------- */

/**
 * @file ICipher.hpp
 * @brief Virtual interface for runtime-polymorphic symmetric cipher selection.
 *
 * Use this when the cipher algorithm must be selected at runtime (config-driven,
 * protocol negotiation, etc.). For compile-time selection, use the CRTP classes
 * directly (Aes256Cbc, Aes256Ctr) for zero-overhead.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace apex::encryption {

using bytes_span = compat::bytes_span;

/* ----------------------------- ICipher ----------------------------- */

/* ----------------------------- CipherAdapter ----------------------------- */

/**
 * @brief Abstract interface for symmetric cipher algorithms.
 *
 * Enables runtime algorithm selection with one vtable lookup per call.
 * NOT RT-safe due to virtual dispatch; use CRTP classes directly in
 * hard real-time paths where timing must be fully deterministic.
 */
class ICipher {
public:
  /// Common status codes for cipher operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_INVALID_KEY = 1,
    ERROR_INVALID_IV = 2,
    ERROR_INIT = 3,
    ERROR_UPDATE = 4,
    ERROR_FINAL = 5,
    ERROR_OUTPUT_TOO_SMALL = 6,
    ERROR_NULL_ALGORITHM = 7,
    ERROR_UNKNOWN = 255
  };

  virtual ~ICipher() = default;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t keySize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t ivSize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t blockSize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual const char* algorithmName() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  virtual void setKey(bytes_span key) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  virtual void setIv(bytes_span iv) = 0;

  /** @note NOT RT-safe: Virtual dispatch, possible allocation. */
  [[nodiscard]] virtual Status encrypt(bytes_span plaintext, std::vector<std::uint8_t>& out) = 0;

  /** @note NOT RT-safe: Virtual dispatch, possible allocation. */
  [[nodiscard]] virtual Status decrypt(bytes_span ciphertext, std::vector<std::uint8_t>& out) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual Status encrypt(bytes_span plaintext, std::uint8_t* outBuf,
                                       std::size_t& outLen) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual Status decrypt(bytes_span ciphertext, std::uint8_t* outBuf,
                                       std::size_t& outLen) = 0;

protected:
  ICipher() = default;
  ICipher(const ICipher&) = default;
  ICipher& operator=(const ICipher&) = default;
  ICipher(ICipher&&) = default;
  ICipher& operator=(ICipher&&) = default;
};

/* ----------------------------- CipherAdapter ----------------------------- */

/**
 * @brief Adapter that bridges a CRTP cipher class to the ICipher interface.
 *
 * @tparam Cipher A CRTP-derived cipher class (e.g., Aes256Cbc, Aes256Ctr).
 *
 * Example:
 * @code
 *   std::unique_ptr<ICipher> cipher = std::make_unique<CipherAdapter<Aes256Cbc>>();
 *   cipher->setKey(keySpan);
 *   cipher->setIv(ivSpan);
 *   cipher->encrypt(plaintext, ciphertext);
 * @endcode
 */
template <typename Cipher> class CipherAdapter final : public ICipher {
public:
  CipherAdapter() = default;

  [[nodiscard]] std::size_t keySize() const noexcept override { return Cipher::KEY_LENGTH; }

  [[nodiscard]] std::size_t ivSize() const noexcept override { return Cipher::IV_LENGTH; }

  [[nodiscard]] std::size_t blockSize() const noexcept override { return Cipher::BLOCK_SIZE; }

  [[nodiscard]] const char* algorithmName() const noexcept override {
    return Cipher::fetchCipherName();
  }

  void setKey(bytes_span key) override { impl_.setKey(key); }

  void setIv(bytes_span iv) override { impl_.setIv(iv); }

  [[nodiscard]] Status encrypt(bytes_span plaintext, std::vector<std::uint8_t>& out) override {
    auto result = impl_.encrypt(plaintext, out);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status decrypt(bytes_span ciphertext, std::vector<std::uint8_t>& out) override {
    auto result = impl_.decrypt(ciphertext, out);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status encrypt(bytes_span plaintext, std::uint8_t* outBuf,
                               std::size_t& outLen) override {
    auto result = impl_.encrypt(plaintext, outBuf, outLen);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status decrypt(bytes_span ciphertext, std::uint8_t* outBuf,
                               std::size_t& outLen) override {
    auto result = impl_.decrypt(ciphertext, outBuf, outLen);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

private:
  Cipher impl_;
};

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_ICIPHER_HPP
