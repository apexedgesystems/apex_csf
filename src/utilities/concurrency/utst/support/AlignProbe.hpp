#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_ALIGN_PROBE_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_ALIGN_PROBE_HPP
/**
 * @file AlignProbe.hpp
 * @brief An element type that needs 64-byte alignment and counts every
 *        construction at an address that does not have it. A container that
 *        allocates its storage without the element's alignment shows up in
 *        the count.
 */

#include <atomic>
#include <cstdint>

namespace apex {
namespace concurrency {
namespace test {

struct alignas(64) AlignProbe {
  AlignProbe() {
    if (reinterpret_cast<std::uintptr_t>(this) % 64 != 0) {
      misaligned().fetch_add(1, std::memory_order_relaxed);
    }
  }

  /// Constructions seen at an address that is not a multiple of 64.
  static std::atomic<int>& misaligned() {
    static std::atomic<int> count{0};
    return count;
  }

  std::uint8_t byte{0};
};

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_ALIGN_PROBE_HPP
