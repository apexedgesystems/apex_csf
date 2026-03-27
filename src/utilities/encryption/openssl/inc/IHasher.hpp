#ifndef APEX_UTILITIES_ENCRYPTION_IHASHER_HPP
#define APEX_UTILITIES_ENCRYPTION_IHASHER_HPP
/**
 * @file IHasher.hpp
 * @brief Virtual interface for runtime-polymorphic hash algorithm selection.
 *
 * Use this when the hash algorithm must be selected at runtime (config-driven,
 * protocol negotiation, etc.). For compile-time selection, use the CRTP classes
 * directly (Sha256Hash, Sha512Hash, etc.) for zero-overhead.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <openssl/evp.h>
#include <vector>

namespace apex::encryption {

using bytes_span = compat::bytes_span;

/* ----------------------------- IHasher ----------------------------- */

/**
 * @brief Abstract interface for cryptographic hash functions.
 *
 * Enables runtime algorithm selection with one vtable lookup per call.
 * NOT RT-safe due to virtual dispatch; use CRTP classes directly in
 * hard real-time paths where timing must be fully deterministic.
 */
class IHasher {
public:
  /// Common status codes for hash operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_NULL_ALGORITHM = 1,
    ERROR_DIGEST_INIT = 2,
    ERROR_DIGEST_UPDATE = 3,
    ERROR_DIGEST_FINAL = 4,
    ERROR_OUTPUT_TOO_SMALL = 5,
    ERROR_UNKNOWN = 255
  };

  virtual ~IHasher() = default;

  /**
   * @brief Returns the digest size in bytes for this algorithm.
   * @note NOT RT-safe: Virtual dispatch.
   */
  [[nodiscard]] virtual std::size_t digestSize() const noexcept = 0;

  /**
   * @brief Returns the algorithm name (e.g., "SHA256", "SHA512").
   * @note NOT RT-safe: Virtual dispatch.
   */
  [[nodiscard]] virtual const char* algorithmName() const noexcept = 0;

  /**
   * @brief Hash message into a vector (resized to digest size).
   * @note NOT RT-safe: Virtual dispatch, possible allocation.
   */
  [[nodiscard]] virtual Status hash(bytes_span message, std::vector<std::uint8_t>& outDigest) = 0;

  /**
   * @brief Hash message into caller-provided buffer.
   * @note NOT RT-safe: Virtual dispatch.
   */
  [[nodiscard]] virtual Status hash(bytes_span message, std::uint8_t* outBuf,
                                    std::size_t& outLen) = 0;

protected:
  IHasher() = default;
  IHasher(const IHasher&) = default;
  IHasher& operator=(const IHasher&) = default;
  IHasher(IHasher&&) = default;
  IHasher& operator=(IHasher&&) = default;
};

/* ----------------------------- HasherAdapter ----------------------------- */

/**
 * @brief Adapter that bridges a CRTP hash class to the IHasher interface.
 *
 * @tparam Hash A CRTP-derived hash class (e.g., Sha256Hash, Sha512Hash).
 *
 * Example:
 * @code
 *   std::unique_ptr<IHasher> hasher = std::make_unique<HasherAdapter<Sha256Hash>>();
 *   hasher->hash(message, digest);
 * @endcode
 */
template <typename Hash> class HasherAdapter final : public IHasher {
public:
  HasherAdapter() = default;

  [[nodiscard]] std::size_t digestSize() const noexcept override {
    // Get digest size from OpenSSL at runtime
    const EVP_MD* md = Hash::fetchHashAlgorithm();
    return md ? static_cast<std::size_t>(EVP_MD_get_size(md)) : 0;
  }

  [[nodiscard]] const char* algorithmName() const noexcept override {
    return Hash::fetchHashName();
  }

  [[nodiscard]] Status hash(bytes_span message, std::vector<std::uint8_t>& outDigest) override {
    auto result = impl_.hash(message, outDigest);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status hash(bytes_span message, std::uint8_t* outBuf,
                            std::size_t& outLen) override {
    auto result = impl_.hash(message, outBuf, outLen);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

private:
  Hash impl_;
};

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_IHASHER_HPP
