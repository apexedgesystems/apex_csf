/**
 * @file CrcBase.tpp
 * @brief Template implementation for CrcBase.
 */
#ifndef APEX_UTILITIES_CHECKSUMS_CRC_BASE_TPP
#define APEX_UTILITIES_CHECKSUMS_CRC_BASE_TPP

#include "src/utilities/compatibility/inc/compat_lang.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"
#include "src/utilities/checksums/crc/inc/CrcBase.hpp"

namespace apex {
namespace checksums {
namespace crc {

template <typename T, typename Derived>
inline Status CrcBase<T, Derived>::update(const uint8_t* data, size_t len) noexcept {
  Derived::updateImpl(rem_, data, len);
  return Status::SUCCESS;
}

template <typename T, typename Derived>
inline Status CrcBase<T, Derived>::update(apex::compat::bytes_span data) noexcept {
  return update(data.data(), data.size());
}

template <typename T, typename Derived>
inline Status CrcBase<T, Derived>::finalize(T& out) const noexcept {
  T result = rem_;
  APEX_IF_CONSTEXPR(Derived::fetchReflectOut()) { result = reflectBits(result, fetchWidth()); }
  result ^= fetchXorOut();
  result &= fetchMask();
  out = result;
  return Status::SUCCESS;
}

template <typename T, typename Derived>
inline Status CrcBase<T, Derived>::calculate(const uint8_t* data, size_t len, T& out) noexcept {
  reset();
  update(data, len);
  return finalize(out);
}

template <typename T, typename Derived>
inline Status CrcBase<T, Derived>::calculate(apex::compat::bytes_span data, T& out) noexcept {
  return calculate(data.data(), data.size(), out);
}

#ifdef APEX_COMPAT_HAS_VECTOR
template <typename T, typename Derived>
inline Status CrcBase<T, Derived>::calculate(const std::vector<uint8_t>& data, T& out) noexcept {
  return calculate(data.data(), data.size(), out);
}
#endif

template <typename T, typename Derived>
constexpr T CrcBase<T, Derived>::reflectBits(T v, uint8_t width) noexcept {
  if (width == 0 || width > sizeof(T) * 8) {
    return T(0);
  }
  T r = 0;
  for (uint8_t i = 0; i < width; ++i) {
    r = (r << 1) | (v & 1);
    v >>= 1;
  }
  return r;
}

} // namespace crc
} // namespace checksums
} // namespace apex

#endif // APEX_UTILITIES_CHECKSUMS_CRC_BASE_TPP
