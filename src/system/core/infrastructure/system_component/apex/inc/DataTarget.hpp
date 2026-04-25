#ifndef APEX_SYSTEM_CORE_DATA_TARGET_HPP
#define APEX_SYSTEM_CORE_DATA_TARGET_HPP
/**
 * @file DataTarget.hpp
 * @brief Runtime addressing for registered data blocks.
 *
 * DataTarget identifies a byte range within any data block registered with
 * the executive registry. It is the fundamental addressing primitive for all
 * runtime data operations (actions, watchpoints, sequences).
 *
 * A target specifies:
 *   - Which component (fullUid)
 *   - Which data block (category)
 *   - Which bytes within that block (byteOffset + byteLen)
 *
 * RT-safe: Pure data structure, no allocation or I/O.
 *
 * Usage:
 * @code
 *   // Target the altitude field in HilPlantModel OUTPUT
 *   DataTarget target{};
 *   target.fullUid = 0x007800;                  // HilPlantModel
 *   target.category = DataCategory::OUTPUT;
 *   target.byteOffset = 36;                     // altitude offset in VehicleState
 *   target.byteLen = 4;                         // float = 4 bytes
 *
 *   // Target the entire STATE block of HilDriver #0
 *   DataTarget wholeBlock{};
 *   wholeBlock.fullUid = 0x007A00;
 *   wholeBlock.category = DataCategory::STATE;
 *   wholeBlock.byteOffset = 0;
 *   wholeBlock.byteLen = 0;                     // 0 = whole block
 * @endcode
 */

#include "src/system/core/infrastructure/system_component/apex/inc/DataCategory.hpp"

#include <cstdint>

namespace system_core {
namespace data {

/* ----------------------------- DataTarget ----------------------------- */

/**
 * @struct DataTarget
 * @brief Identifies a byte range within a registered data block.
 *
 * Used by DataAction and DataWatchpoint to address any data block
 * by fullUid + category + byte offset.
 *
 * @note RT-safe: Pure POD, no allocation.
 */
struct DataTarget {
  std::uint32_t fullUid{0};    ///< Component fullUid (from registry).
  DataCategory category{};     ///< Which data block within the component.
  std::uint16_t byteOffset{0}; ///< Starting byte within the data block.
  std::uint8_t byteLen{0};     ///< Bytes affected (0 = whole block).
};

static_assert(sizeof(DataTarget) == 12, "DataTarget expected 12 bytes");

/* ----------------------------- API ----------------------------- */

/**
 * @brief Check if a target addresses the entire block.
 * @param t Target to check.
 * @return True if byteLen is 0 (whole-block addressing).
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isWholeBlock(const DataTarget& t) noexcept { return t.byteLen == 0; }

/**
 * @brief Check if a target's byte range fits within a block of given size.
 * @param t Target to validate.
 * @param blockSize Size of the data block in bytes.
 * @return True if the range [byteOffset, byteOffset+byteLen) fits.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool isInBounds(const DataTarget& t, std::size_t blockSize) noexcept {
  if (t.byteLen == 0) {
    return t.byteOffset == 0;
  }
  return (static_cast<std::size_t>(t.byteOffset) + t.byteLen) <= blockSize;
}

/**
 * @brief Get the effective length for a target given the block size.
 * @param t Target.
 * @param blockSize Size of the data block in bytes.
 * @return byteLen if non-zero, otherwise blockSize.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::size_t effectiveLen(const DataTarget& t, std::size_t blockSize) noexcept {
  return (t.byteLen == 0) ? blockSize : static_cast<std::size_t>(t.byteLen);
}

/**
 * @brief Compare two targets for equality.
 * @param a First target.
 * @param b Second target.
 * @return True if all fields match.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool operator==(const DataTarget& a, const DataTarget& b) noexcept {
  return a.fullUid == b.fullUid && a.category == b.category && a.byteOffset == b.byteOffset &&
         a.byteLen == b.byteLen;
}

/**
 * @brief Compare two targets for inequality.
 * @param a First target.
 * @param b Second target.
 * @return True if any field differs.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool operator!=(const DataTarget& a, const DataTarget& b) noexcept {
  return !(a == b);
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_TARGET_HPP
