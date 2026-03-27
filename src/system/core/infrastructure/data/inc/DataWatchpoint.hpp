#ifndef APEX_SYSTEM_CORE_DATA_WATCHPOINT_HPP
#define APEX_SYSTEM_CORE_DATA_WATCHPOINT_HPP
/**
 * @file DataWatchpoint.hpp
 * @brief Conditional data triggers with type-aware comparison.
 *
 * DataWatchpoint monitors a byte range in a registered data block and
 * evaluates a predicate each cycle. When the predicate transitions from
 * false to true (edge-triggered), the watchpoint fires an event ID that
 * can trigger DataActions with ON_EVENT triggers.
 *
 * Supported predicates:
 *   - Comparison: GT, LT, GE, LE, EQ, NE (type-aware via WatchDataType)
 *   - Bitfield: BIT_SET, BIT_CLEAR (raw byte checks)
 *   - Change: CHANGED (value differs from last evaluation)
 *   - Custom: User-supplied Delegate for arbitrary logic
 *
 * WatchpointGroup combines multiple watchpoints with AND/OR logic or
 * a custom delegate for cross-variable equations.
 *
 * RT-safe: All operations bounded, noexcept, no allocation.
 *
 * Usage:
 * @code
 *   // Watch altitude > 150.0 on HilPlantModel OUTPUT
 *   DataWatchpoint wp{};
 *   wp.target = {0x007800, DataCategory::OUTPUT, 36, 4};
 *   wp.predicate = WatchPredicate::GT;
 *   wp.dataType = WatchDataType::FLOAT32;
 *   wp.eventId = 1;
 *   float threshold = 150.0F;
 *   std::memcpy(wp.threshold.data(), &threshold, 4);
 *   wp.armed = true;
 *
 *   // Custom predicate via delegate
 *   DataWatchpoint custom{};
 *   custom.target = {0x007800, DataCategory::OUTPUT, 36, 4};
 *   custom.predicate = WatchPredicate::CUSTOM;
 *   custom.customAssess = {myPredicate, &myCtx};
 *   custom.armed = true;
 *
 *   // Group: altitude > 150 AND velocity < 10
 *   WatchpointGroup group{};
 *   group.indices = {0, 1};
 *   group.count = 2;
 *   group.logic = GroupLogic::AND;
 *   group.eventId = 5;
 *   group.armed = true;
 * @endcode
 */

#include "src/system/core/infrastructure/data/inc/DataTarget.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <array>
#include <cstdint>
#include <cstring>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of concurrent watchpoints.
constexpr std::size_t WATCHPOINT_TABLE_SIZE = 8;

/// Maximum threshold/value size in bytes.
constexpr std::size_t WATCH_VALUE_SIZE = 8;

/* ----------------------------- WatchPredicate ----------------------------- */

/**
 * @enum WatchPredicate
 * @brief Comparison operation for watchpoint evaluation.
 */
enum class WatchPredicate : std::uint8_t {
  GT = 0,        ///< Greater than threshold.
  LT = 1,        ///< Less than threshold.
  GE = 2,        ///< Greater or equal.
  LE = 3,        ///< Less or equal.
  EQ = 4,        ///< Equal to threshold.
  NE = 5,        ///< Not equal to threshold.
  BIT_SET = 6,   ///< (value & threshold) == threshold.
  BIT_CLEAR = 7, ///< (value & threshold) == 0.
  CHANGED = 8,   ///< Value differs from last evaluation.
  CUSTOM = 9     ///< User-supplied delegate (customAssess field).
};

/**
 * @brief Human-readable string for WatchPredicate.
 * @param p Predicate value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(WatchPredicate p) noexcept {
  switch (p) {
  case WatchPredicate::GT:
    return "GT";
  case WatchPredicate::LT:
    return "LT";
  case WatchPredicate::GE:
    return "GE";
  case WatchPredicate::LE:
    return "LE";
  case WatchPredicate::EQ:
    return "EQ";
  case WatchPredicate::NE:
    return "NE";
  case WatchPredicate::BIT_SET:
    return "BIT_SET";
  case WatchPredicate::BIT_CLEAR:
    return "BIT_CLEAR";
  case WatchPredicate::CHANGED:
    return "CHANGED";
  case WatchPredicate::CUSTOM:
    return "CUSTOM";
  }
  return "UNKNOWN";
}

/* ----------------------------- WatchDataType ----------------------------- */

/**
 * @enum WatchDataType
 * @brief Data type for type-aware comparison.
 *
 * The evaluator interprets the watched bytes and threshold using this type
 * to perform correct signed/unsigned/floating-point comparison.
 */
enum class WatchDataType : std::uint8_t {
  UINT8 = 0,   ///< Unsigned 8-bit integer.
  UINT16 = 1,  ///< Unsigned 16-bit integer.
  UINT32 = 2,  ///< Unsigned 32-bit integer.
  UINT64 = 3,  ///< Unsigned 64-bit integer.
  INT8 = 4,    ///< Signed 8-bit integer.
  INT16 = 5,   ///< Signed 16-bit integer.
  INT32 = 6,   ///< Signed 32-bit integer.
  INT64 = 7,   ///< Signed 64-bit integer.
  FLOAT32 = 8, ///< IEEE 754 single precision.
  FLOAT64 = 9, ///< IEEE 754 double precision.
  RAW = 10     ///< Byte-level comparison (memcmp).
};

/**
 * @brief Human-readable string for WatchDataType.
 * @param t Type value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(WatchDataType t) noexcept {
  switch (t) {
  case WatchDataType::UINT8:
    return "UINT8";
  case WatchDataType::UINT16:
    return "UINT16";
  case WatchDataType::UINT32:
    return "UINT32";
  case WatchDataType::UINT64:
    return "UINT64";
  case WatchDataType::INT8:
    return "INT8";
  case WatchDataType::INT16:
    return "INT16";
  case WatchDataType::INT32:
    return "INT32";
  case WatchDataType::INT64:
    return "INT64";
  case WatchDataType::FLOAT32:
    return "FLOAT32";
  case WatchDataType::FLOAT64:
    return "FLOAT64";
  case WatchDataType::RAW:
    return "RAW";
  }
  return "UNKNOWN";
}

/**
 * @brief Get the byte width of a WatchDataType.
 * @param t Type value.
 * @return Number of bytes, or 0 for RAW (variable).
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline std::uint8_t dataTypeSize(WatchDataType t) noexcept {
  switch (t) {
  case WatchDataType::UINT8:
  case WatchDataType::INT8:
    return 1;
  case WatchDataType::UINT16:
  case WatchDataType::INT16:
    return 2;
  case WatchDataType::UINT32:
  case WatchDataType::INT32:
  case WatchDataType::FLOAT32:
    return 4;
  case WatchDataType::UINT64:
  case WatchDataType::INT64:
  case WatchDataType::FLOAT64:
    return 8;
  case WatchDataType::RAW:
    return 0;
  }
  return 0;
}

/* ----------------------------- DataWatchpoint ----------------------------- */

/// Delegate signature for custom watchpoint predicates.
/// Parameters: (data pointer, data length). Returns true if predicate satisfied.
using WatchAssessDelegate = apex::concurrency::Delegate<bool, const std::uint8_t*, std::size_t>;

/**
 * @struct DataWatchpoint
 * @brief Monitors a data target and fires an event on predicate match.
 *
 * Edge-triggered: fires only when the predicate transitions from false
 * to true. Re-arms automatically when the predicate goes back to false.
 *
 * When predicate is CUSTOM, the customAssess delegate is invoked instead
 * of the built-in comparison logic. The delegate receives the watched
 * bytes (already offset-adjusted) and their length.
 *
 * Debounce: if minFireCount > 0, the eventId is not dispatched until
 * fireCount reaches minFireCount. Edge transitions still increment
 * fireCount, but evaluateEdge() only returns true once the threshold
 * is met. This prevents spurious triggers on noisy signals.
 *
 * @note RT-safe: No allocation. Trivially copyable when delegate is null.
 */
struct DataWatchpoint {
  DataTarget target{};                                    ///< What to watch.
  WatchPredicate predicate{WatchPredicate::EQ};           ///< When to fire.
  WatchDataType dataType{WatchDataType::RAW};             ///< How to interpret bytes.
  std::uint16_t eventId{0};                               ///< Event ID fired on trigger.
  std::array<std::uint8_t, WATCH_VALUE_SIZE> threshold{}; ///< Comparison value.
  std::array<std::uint8_t, WATCH_VALUE_SIZE> lastValue{}; ///< Previous value (CHANGED).
  WatchAssessDelegate customAssess{};                     ///< Custom predicate (CUSTOM mode).
  bool armed{false};                                      ///< Active flag.
  bool lastResult{false};                                 ///< Previous predicate result.
  std::uint32_t fireCount{0};                             ///< Times edge triggered.
  std::uint32_t minFireCount{0}; ///< Debounce: edges before dispatch (0 = immediate).
};

/* ----------------------------- Evaluation ----------------------------- */

namespace detail {

/**
 * @brief Compare two typed values using a predicate.
 * @tparam T Arithmetic type.
 * @param value Pointer to current value bytes.
 * @param threshold Pointer to threshold bytes.
 * @param pred Predicate to evaluate.
 * @return True if predicate is satisfied.
 * @note RT-safe: O(1).
 */
template <typename T>
[[nodiscard]] inline bool compareTyped(const std::uint8_t* value, const std::uint8_t* threshold,
                                       WatchPredicate pred) noexcept {
  T v{};
  T t{};
  std::memcpy(&v, value, sizeof(T));
  std::memcpy(&t, threshold, sizeof(T));

  switch (pred) {
  case WatchPredicate::GT:
    return v > t;
  case WatchPredicate::LT:
    return v < t;
  case WatchPredicate::GE:
    return v >= t;
  case WatchPredicate::LE:
    return v <= t;
  case WatchPredicate::EQ:
    return v == t;
  case WatchPredicate::NE:
    return v != t;
  default:
    return false;
  }
}

} // namespace detail

/**
 * @brief Evaluate a watchpoint predicate against live data.
 * @param wp Watchpoint to evaluate.
 * @param data Pointer to the watched bytes (already offset-adjusted).
 * @param dataLen Number of bytes available at data pointer.
 * @return True if predicate is currently satisfied.
 * @note RT-safe: O(byteLen).
 *
 * This evaluates the raw predicate result. For edge detection, use
 * evaluateEdge() which also updates lastResult and lastValue.
 */
[[nodiscard]] inline bool evaluatePredicate(const DataWatchpoint& wp, const std::uint8_t* data,
                                            std::size_t dataLen) noexcept {
  if (data == nullptr || dataLen == 0) {
    return false;
  }

  const auto PRED = wp.predicate;

  // CUSTOM: delegate-based assessment
  if (PRED == WatchPredicate::CUSTOM) {
    return wp.customAssess ? wp.customAssess(data, dataLen) : false;
  }

  // BIT_SET: (value & threshold) == threshold
  if (PRED == WatchPredicate::BIT_SET) {
    const std::size_t LEN = (wp.target.byteLen == 0) ? dataLen : wp.target.byteLen;
    for (std::size_t i = 0; i < LEN && i < WATCH_VALUE_SIZE; ++i) {
      if ((data[i] & wp.threshold[i]) != wp.threshold[i]) {
        return false;
      }
    }
    return true;
  }

  // BIT_CLEAR: (value & threshold) == 0
  if (PRED == WatchPredicate::BIT_CLEAR) {
    const std::size_t LEN = (wp.target.byteLen == 0) ? dataLen : wp.target.byteLen;
    for (std::size_t i = 0; i < LEN && i < WATCH_VALUE_SIZE; ++i) {
      if ((data[i] & wp.threshold[i]) != 0) {
        return false;
      }
    }
    return true;
  }

  // CHANGED: value differs from lastValue
  if (PRED == WatchPredicate::CHANGED) {
    const std::size_t LEN = (wp.target.byteLen == 0) ? dataLen : wp.target.byteLen;
    const std::size_t CMP_LEN = (LEN < WATCH_VALUE_SIZE) ? LEN : WATCH_VALUE_SIZE;
    return std::memcmp(data, wp.lastValue.data(), CMP_LEN) != 0;
  }

  // Typed comparison (GT, LT, GE, LE, EQ, NE)
  switch (wp.dataType) {
  case WatchDataType::UINT8:
    return detail::compareTyped<std::uint8_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::UINT16:
    return detail::compareTyped<std::uint16_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::UINT32:
    return detail::compareTyped<std::uint32_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::UINT64:
    return detail::compareTyped<std::uint64_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::INT8:
    return detail::compareTyped<std::int8_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::INT16:
    return detail::compareTyped<std::int16_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::INT32:
    return detail::compareTyped<std::int32_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::INT64:
    return detail::compareTyped<std::int64_t>(data, wp.threshold.data(), PRED);
  case WatchDataType::FLOAT32:
    return detail::compareTyped<float>(data, wp.threshold.data(), PRED);
  case WatchDataType::FLOAT64:
    return detail::compareTyped<double>(data, wp.threshold.data(), PRED);
  case WatchDataType::RAW: {
    // RAW: byte-level memcmp (only EQ/NE meaningful)
    const std::size_t LEN = (wp.target.byteLen == 0) ? dataLen : wp.target.byteLen;
    const std::size_t CMP_LEN = (LEN < WATCH_VALUE_SIZE) ? LEN : WATCH_VALUE_SIZE;
    const int CMP = std::memcmp(data, wp.threshold.data(), CMP_LEN);
    if (PRED == WatchPredicate::EQ) {
      return CMP == 0;
    }
    if (PRED == WatchPredicate::NE) {
      return CMP != 0;
    }
    return false;
  }
  }

  return false;
}

/**
 * @brief Evaluate a watchpoint with edge detection and state update.
 * @param wp Watchpoint to evaluate (modified: lastResult, lastValue, fireCount).
 * @param data Pointer to the watched bytes (already offset-adjusted).
 * @param dataLen Number of bytes available at data pointer.
 * @return True if the watchpoint should dispatch its eventId.
 * @note RT-safe: O(byteLen).
 *
 * Updates wp.lastResult for edge detection and wp.lastValue for CHANGED
 * predicate tracking. Increments wp.fireCount on each edge trigger.
 *
 * When minFireCount > 0 (debounce), returns true only when fireCount
 * reaches minFireCount exactly. This lets noisy signals settle before
 * dispatching. After dispatch, fireCount continues incrementing but
 * no further dispatches occur until the predicate goes false and
 * fireCount resets.
 */
inline bool evaluateEdge(DataWatchpoint& wp, const std::uint8_t* data,
                         std::size_t dataLen) noexcept {
  if (!wp.armed || data == nullptr) {
    return false;
  }

  const bool RESULT = evaluatePredicate(wp, data, dataLen);
  const bool EDGE = RESULT && !wp.lastResult;

  wp.lastResult = RESULT;

  // Reset fireCount when predicate goes false (re-arm debounce)
  if (!RESULT) {
    wp.fireCount = 0;
  }

  // Update lastValue for CHANGED predicate
  if (wp.predicate == WatchPredicate::CHANGED) {
    const std::size_t LEN = (wp.target.byteLen == 0) ? dataLen : wp.target.byteLen;
    const std::size_t COPY_LEN = (LEN < WATCH_VALUE_SIZE) ? LEN : WATCH_VALUE_SIZE;
    std::memcpy(wp.lastValue.data(), data, COPY_LEN);
  }

  if (EDGE) {
    ++wp.fireCount;
  }

  // No debounce: dispatch on first edge
  if (wp.minFireCount == 0) {
    return EDGE;
  }

  // Debounce: dispatch only when count reaches threshold
  return wp.fireCount == wp.minFireCount;
}

/* ----------------------------- GroupLogic ----------------------------- */

/**
 * @enum GroupLogic
 * @brief Combination logic for watchpoint groups.
 */
enum class GroupLogic : std::uint8_t {
  AND = 0, ///< All referenced watchpoints must be true.
  OR = 1   ///< Any referenced watchpoint must be true.
};

/**
 * @brief Human-readable string for GroupLogic.
 * @param g Logic value.
 * @return Static string.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(GroupLogic g) noexcept {
  switch (g) {
  case GroupLogic::AND:
    return "AND";
  case GroupLogic::OR:
    return "OR";
  }
  return "UNKNOWN";
}

/* ----------------------------- WatchpointGroup ----------------------------- */

/// Maximum watchpoints per group.
constexpr std::size_t WATCHPOINT_GROUP_MAX_REFS = 4;

/// Maximum number of concurrent watchpoint groups.
constexpr std::size_t WATCHPOINT_GROUP_TABLE_SIZE = 4;

/// Delegate signature for custom group assessment.
/// Parameters: (array of lastResult bools per referenced watchpoint, count).
/// Returns true if the group condition is satisfied.
using GroupAssessDelegate = apex::concurrency::Delegate<bool, const bool*, std::uint8_t>;

/**
 * @struct WatchpointGroup
 * @brief Combines multiple watchpoint results with logic or custom delegate.
 *
 * References watchpoints by index into the watchpoint table. Evaluates
 * each referenced watchpoint's lastResult using AND/OR logic, or invokes
 * a custom delegate for cross-variable equations.
 *
 * Edge-triggered: fires only on false-to-true transition of the combined
 * result, same as individual watchpoints.
 *
 * @note RT-safe: No allocation. Static sizing.
 */
struct WatchpointGroup {
  std::array<std::uint8_t, WATCHPOINT_GROUP_MAX_REFS> indices{}; ///< Watchpoint table indices.
  std::uint8_t count{0};                                         ///< Active reference count.
  GroupLogic logic{GroupLogic::AND};                             ///< Built-in combination logic.
  GroupAssessDelegate customAssess{}; ///< Custom assessment (overrides logic).
  std::uint16_t eventId{0};           ///< Event ID fired on trigger.
  bool armed{false};                  ///< Active flag.
  bool lastResult{false};             ///< Previous group result.
  std::uint32_t fireCount{0};         ///< Times this group fired.
};

/* ----------------------------- Group Evaluation ----------------------------- */

/**
 * @brief Evaluate a watchpoint group against the watchpoint table.
 * @param group Group to evaluate.
 * @param table Pointer to the watchpoint table.
 * @param tableSize Number of entries in the watchpoint table.
 * @return True if the group condition is currently satisfied.
 * @note RT-safe: O(count).
 *
 * Reads lastResult from each referenced watchpoint. If customAssess is
 * set, passes the collected results to the delegate. Otherwise applies
 * AND/OR logic.
 */
[[nodiscard]] inline bool evaluateGroup(const WatchpointGroup& group, const DataWatchpoint* table,
                                        std::size_t tableSize) noexcept {
  if (table == nullptr || group.count == 0) {
    return false;
  }

  // Collect lastResult from referenced watchpoints
  std::array<bool, WATCHPOINT_GROUP_MAX_REFS> results{};
  for (std::uint8_t i = 0; i < group.count && i < WATCHPOINT_GROUP_MAX_REFS; ++i) {
    if (group.indices[i] < tableSize) {
      results[i] = table[group.indices[i]].lastResult;
    } else {
      results[i] = false;
    }
  }

  // Custom delegate takes priority
  if (group.customAssess) {
    return group.customAssess(results.data(), group.count);
  }

  // Built-in logic
  if (group.logic == GroupLogic::AND) {
    for (std::uint8_t i = 0; i < group.count; ++i) {
      if (!results[i]) {
        return false;
      }
    }
    return true;
  }

  // OR
  for (std::uint8_t i = 0; i < group.count; ++i) {
    if (results[i]) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Evaluate a watchpoint group with edge detection.
 * @param group Group to evaluate (modified: lastResult, fireCount).
 * @param table Pointer to the watchpoint table.
 * @param tableSize Number of entries in the watchpoint table.
 * @return True if the group result transitioned from false to true.
 * @note RT-safe: O(count).
 */
inline bool evaluateGroupEdge(WatchpointGroup& group, const DataWatchpoint* table,
                              std::size_t tableSize) noexcept {
  if (!group.armed) {
    return false;
  }

  const bool RESULT = evaluateGroup(group, table, tableSize);
  const bool FIRED = RESULT && !group.lastResult;

  group.lastResult = RESULT;

  if (FIRED) {
    ++group.fireCount;
  }

  return FIRED;
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_WATCHPOINT_HPP
