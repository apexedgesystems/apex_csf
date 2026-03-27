#ifndef APEX_UTILITIES_ENCRYPTION_IMAC_HPP
#define APEX_UTILITIES_ENCRYPTION_IMAC_HPP
/* ----------------------------- MacAdapter ----------------------------- */

/**
 * @file IMac.hpp
 * @brief Virtual interface for runtime-polymorphic MAC algorithm selection.
 *
 * Use this when the MAC algorithm must be selected at runtime (config-driven,
 * protocol negotiation, etc.). For compile-time selection, use the CRTP classes
 * directly (HmacSha256, HmacSha512, CmacAes128, Poly1305) for zero-overhead.
 */

#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace apex::encryption {

using bytes_span = compat::bytes_span;

/* ----------------------------- IMac ----------------------------- */

/* ----------------------------- MacAdapter ----------------------------- */

/**
 * @brief Abstract interface for Message Authentication Code algorithms.
 *
 * Enables runtime algorithm selection with one vtable lookup per call.
 * NOT RT-safe due to virtual dispatch; use CRTP classes directly in
 * hard real-time paths where timing must be fully deterministic.
 */
class IMac {
public:
  /// Common status codes for MAC operations.
  enum class Status : std::uint8_t {
    SUCCESS = 0,
    ERROR_NULL_ALGORITHM = 1,
    ERROR_INIT = 2,
    ERROR_UPDATE = 3,
    ERROR_FINAL = 4,
    ERROR_OUTPUT_TOO_SMALL = 5,
    ERROR_INVALID_KEY = 6,
    ERROR_UNKNOWN = 255
  };

  virtual ~IMac() = default;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t keySize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual std::size_t digestSize() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual const char* algorithmName() const noexcept = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  virtual void setKey(bytes_span key) = 0;

  /** @note NOT RT-safe: Virtual dispatch, possible allocation. */
  [[nodiscard]] virtual Status mac(bytes_span message, std::vector<std::uint8_t>& out) = 0;

  /** @note NOT RT-safe: Virtual dispatch. */
  [[nodiscard]] virtual Status mac(bytes_span message, std::uint8_t* outBuf,
                                   std::size_t& outLen) = 0;

protected:
  IMac() = default;
  IMac(const IMac&) = default;
  IMac& operator=(const IMac&) = default;
  IMac(IMac&&) = default;
  IMac& operator=(IMac&&) = default;
};

/* ----------------------------- MacAdapter ----------------------------- */

/**
 * @brief Adapter that bridges a CRTP MAC class to the IMac interface.
 *
 * @tparam Mac A CRTP-derived MAC class (e.g., HmacSha256, Poly1305).
 *
 * Example:
 * @code
 *   std::unique_ptr<IMac> mac = std::make_unique<MacAdapter<HmacSha256>>();
 *   mac->setKey(keySpan);
 *   mac->mac(message, tag);
 * @endcode
 */
template <typename Mac> class MacAdapter final : public IMac {
public:
  MacAdapter() = default;

  [[nodiscard]] std::size_t keySize() const noexcept override { return Mac::KEY_LENGTH; }

  [[nodiscard]] std::size_t digestSize() const noexcept override { return Mac::DIGEST_LENGTH; }

  [[nodiscard]] const char* algorithmName() const noexcept override {
    // MAC classes expose algorithm via fetchParamValue() (e.g., "SHA256", "AES-128-CBC")
    return Mac::fetchParamValue();
  }

  void setKey(bytes_span key) override { impl_.setKey(key); }

  [[nodiscard]] Status mac(bytes_span message, std::vector<std::uint8_t>& out) override {
    auto result = impl_.mac(message, out);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

  [[nodiscard]] Status mac(bytes_span message, std::uint8_t* outBuf, std::size_t& outLen) override {
    auto result = impl_.mac(message, outBuf, outLen);
    return static_cast<Status>(static_cast<std::uint8_t>(result));
  }

private:
  Mac impl_;
};

} // namespace apex::encryption

#endif // APEX_UTILITIES_ENCRYPTION_IMAC_HPP
