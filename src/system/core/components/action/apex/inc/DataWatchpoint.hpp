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
 *   group.refs = {1, 2};
 *   group.count = 2;
 *   group.logic = GroupLogic::AND;
 *   group.eventId = 5;
 *   group.armed = true;
 * @endcode
 */

#include "src/system/core/components/action/apex/inc/ActionEngineConfig.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/DataTarget.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

#include <array>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum number of concurrent watchpoints.
constexpr std::size_t WATCHPOINT_TABLE_SIZE = Config::WATCHPOINT_COUNT;

/// Maximum threshold/value size in bytes.
constexpr std::size_t WATCH_VALUE_SIZE = Config::WATCH_VALUE_SIZE;

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

/* ----------------------------- WatchFunction ----------------------------- */

/**
 * @enum WatchFunction
 * @brief Pre-processing function applied to raw data before predicate evaluation.
 *
 * The function transforms raw watched bytes into a computed double value.
 * The predicate (GT, LT, etc.) then compares the computed value against
 * the threshold. This enables monitoring derived quantities (rate of change,
 * vector magnitude) without requiring the user to write custom code.
 *
 * Pipeline: raw bytes -> WatchFunction -> computed double -> predicate -> edge detect
 */
enum class WatchFunction : std::uint8_t {
  NONE = 0,      ///< No transform. Predicate evaluates raw bytes directly.
  DELTA = 1,     ///< |current - previous|. Absolute change since last tick.
  RATE = 2,      ///< (current - previous) / dt. Rate of change per second.
  MAGNITUDE = 3, ///< sqrt(sum of squares). Vector magnitude across consecutive fields.
  MEAN = 4,      ///< Rolling average over sampleWindow ticks.
  STALE = 5,     ///< Ticks since value last changed.
  CUSTOM = 6     ///< User-supplied compute delegate.
};

/**
 * @brief Human-readable string for WatchFunction.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline const char* toString(WatchFunction f) noexcept {
  switch (f) {
  case WatchFunction::NONE:
    return "NONE";
  case WatchFunction::DELTA:
    return "DELTA";
  case WatchFunction::RATE:
    return "RATE";
  case WatchFunction::MAGNITUDE:
    return "MAGNITUDE";
  case WatchFunction::MEAN:
    return "MEAN";
  case WatchFunction::STALE:
    return "STALE";
  case WatchFunction::CUSTOM:
    return "CUSTOM";
  }
  return "UNKNOWN";
}

/* ----------------------------- DataWatchpoint ----------------------------- */

/// Delegate signature for custom watchpoint predicates.
/// Parameters: (data pointer, data length). Returns true if predicate satisfied.
using WatchAssessDelegate = apex::concurrency::Delegate<bool, const std::uint8_t*, std::size_t>;

/// Delegate signature for custom compute functions.
/// Parameters: (data pointer, data length). Returns computed double value.
using WatchComputeDelegate = apex::concurrency::Delegate<double, const std::uint8_t*, std::size_t>;

/**
 * @struct DataWatchpoint
 * @brief Monitors a data target and fires an event on predicate match.
 *
 * Edge-triggered: fires only when the predicate transitions from false
 * to true. Re-arms automatically when the predicate goes back to false.
 *
 * Compute pipeline:
 *   1. Raw bytes read from data target.
 *   2. WatchFunction transforms to computed double (or NONE for raw).
 *   3. Predicate (GT, LT, etc.) compares computed value against threshold.
 *   4. Edge detection fires event on false-to-true transition.
 *
 * When predicate is CUSTOM, the customAssess delegate is invoked instead
 * of the standard pipeline. When function is CUSTOM, the customCompute
 * delegate provides the transform step.
 *
 * @note RT-safe: No allocation.
 */
struct DataWatchpoint {
  std::uint16_t watchpointId{0};                          ///< Unique ID (0 = unassigned).
  DataTarget target{};                                    ///< What to watch.
  WatchPredicate predicate{WatchPredicate::EQ};           ///< When to fire.
  WatchDataType dataType{WatchDataType::RAW};             ///< How to interpret bytes.
  WatchFunction function{WatchFunction::NONE};            ///< Pre-processing transform.
  std::uint16_t eventId{0};                               ///< Event ID fired on trigger.
  std::array<std::uint8_t, WATCH_VALUE_SIZE> threshold{}; ///< Comparison value.
  std::array<std::uint8_t, WATCH_VALUE_SIZE>
      lastValue{};                      ///< Previous value (for CHANGED/DELTA/RATE).
  WatchAssessDelegate customAssess{};   ///< Custom predicate (CUSTOM mode).
  WatchComputeDelegate customCompute{}; ///< Custom compute (WatchFunction::CUSTOM).
  bool armed{false};                    ///< Active flag.
  bool lastResult{false};               ///< Previous predicate result.
  std::uint32_t fireCount{0};           ///< Times edge triggered.
  std::uint32_t minFireCount{0};        ///< Debounce: edges before dispatch (0 = immediate).
  std::uint16_t cadenceTicks{0};        ///< Evaluate every N ticks (0 = every tick).

  /* ---- Compute State ---- */
  double computedValue{0.0};       ///< Last computed value (for MEAN accumulation).
  double previousValue{0.0};       ///< Previous typed value (for DELTA/RATE).
  std::uint8_t magnitudeFields{1}; ///< Number of consecutive fields for MAGNITUDE (1-8).
  std::uint16_t sampleWindow{8};   ///< Window size for MEAN (ticks).
  std::uint16_t sampleCount{0};    ///< Current sample count (MEAN).
  double sampleSum{0.0};           ///< Running sum (MEAN).
  std::uint32_t staleTicks{0};     ///< Ticks since value last changed (STALE).
};

/* ----------------------------- Compute Functions ----------------------------- */

namespace detail {

/**
 * @brief Extract a double from raw bytes based on WatchDataType.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline double extractDouble(const std::uint8_t* data, WatchDataType type) noexcept {
  switch (type) {
  case WatchDataType::UINT8: {
    std::uint8_t v;
    std::memcpy(&v, data, 1);
    return static_cast<double>(v);
  }
  case WatchDataType::UINT16: {
    std::uint16_t v;
    std::memcpy(&v, data, 2);
    return static_cast<double>(v);
  }
  case WatchDataType::UINT32: {
    std::uint32_t v;
    std::memcpy(&v, data, 4);
    return static_cast<double>(v);
  }
  case WatchDataType::UINT64: {
    std::uint64_t v;
    std::memcpy(&v, data, 8);
    return static_cast<double>(v);
  }
  case WatchDataType::INT8: {
    std::int8_t v;
    std::memcpy(&v, data, 1);
    return static_cast<double>(v);
  }
  case WatchDataType::INT16: {
    std::int16_t v;
    std::memcpy(&v, data, 2);
    return static_cast<double>(v);
  }
  case WatchDataType::INT32: {
    std::int32_t v;
    std::memcpy(&v, data, 4);
    return static_cast<double>(v);
  }
  case WatchDataType::INT64: {
    std::int64_t v;
    std::memcpy(&v, data, 8);
    return static_cast<double>(v);
  }
  case WatchDataType::FLOAT32: {
    float v;
    std::memcpy(&v, data, 4);
    return static_cast<double>(v);
  }
  case WatchDataType::FLOAT64: {
    double v;
    std::memcpy(&v, data, 8);
    return v;
  }
  case WatchDataType::RAW:
    return static_cast<double>(data[0]);
  }
  return 0.0;
}

} // namespace detail

/**
 * @brief Apply a WatchFunction to compute a derived value from raw data.
 * @param wp Watchpoint (modified: compute state updated).
 * @param data Pointer to raw watched bytes.
 * @param dataLen Byte count available.
 * @param dtSeconds Time step in seconds (for RATE function).
 * @return Computed value as double. For NONE, returns the raw typed value.
 * @note RT-safe: O(magnitudeFields) for MAGNITUDE, O(1) otherwise.
 *
 * Updates internal state (previousValue, sampleSum, staleTicks) each call.
 * The caller compares the returned value against the threshold using the
 * standard predicate.
 */
inline double applyWatchFunction(DataWatchpoint& wp, const std::uint8_t* data, std::size_t dataLen,
                                 double dtSeconds = 0.01) noexcept {
  if (data == nullptr || dataLen == 0) {
    return 0.0;
  }

  const double CURRENT = detail::extractDouble(data, wp.dataType);

  switch (wp.function) {
  case WatchFunction::NONE:
    return CURRENT;

  case WatchFunction::DELTA: {
    const double DELTA =
        (CURRENT >= wp.previousValue) ? (CURRENT - wp.previousValue) : (wp.previousValue - CURRENT);
    wp.previousValue = CURRENT;
    wp.computedValue = DELTA;
    return DELTA;
  }

  case WatchFunction::RATE: {
    const double RATE = (dtSeconds > 0.0) ? (CURRENT - wp.previousValue) / dtSeconds : 0.0;
    wp.previousValue = CURRENT;
    wp.computedValue = RATE;
    return RATE;
  }

  case WatchFunction::MAGNITUDE: {
    // Sum of squares across consecutive fields of the same data type.
    const std::uint8_t FIELD_SIZE = dataTypeSize(wp.dataType);
    if (FIELD_SIZE == 0) {
      return CURRENT;
    }
    double sumSq = 0.0;
    const std::uint8_t FIELDS =
        (wp.magnitudeFields > 0) ? wp.magnitudeFields : static_cast<std::uint8_t>(1);
    for (std::uint8_t f = 0; f < FIELDS; ++f) {
      const std::size_t OFFSET = static_cast<std::size_t>(f) * FIELD_SIZE;
      if (OFFSET + FIELD_SIZE > dataLen) {
        break;
      }
      const double V = detail::extractDouble(data + OFFSET, wp.dataType);
      sumSq += V * V;
    }
    // Fast inverse sqrt approximation for RT, or just use sqrt
    const double MAG = std::sqrt(sumSq);
    wp.computedValue = MAG;
    return MAG;
  }

  case WatchFunction::MEAN: {
    wp.sampleSum += CURRENT;
    ++wp.sampleCount;
    if (wp.sampleCount >= wp.sampleWindow) {
      wp.computedValue = wp.sampleSum / static_cast<double>(wp.sampleCount);
      wp.sampleSum = 0.0;
      wp.sampleCount = 0;
    }
    return wp.computedValue;
  }

  case WatchFunction::STALE: {
    // Compare current bytes against lastValue
    const std::size_t LEN = (wp.target.byteLen == 0) ? dataLen : wp.target.byteLen;
    const std::size_t CMP_LEN = (LEN < WATCH_VALUE_SIZE) ? LEN : WATCH_VALUE_SIZE;
    if (std::memcmp(data, wp.lastValue.data(), CMP_LEN) == 0) {
      ++wp.staleTicks;
    } else {
      wp.staleTicks = 0;
    }
    wp.computedValue = static_cast<double>(wp.staleTicks);
    return wp.computedValue;
  }

  case WatchFunction::CUSTOM:
    if (wp.customCompute) {
      wp.computedValue = wp.customCompute(data, dataLen);
      return wp.computedValue;
    }
    return CURRENT;
  }

  return CURRENT;
}

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

/**
 * @brief Compare a computed double value against a threshold using a predicate.
 * @param computed Computed value from WatchFunction.
 * @param thresholdBytes Threshold stored as raw bytes in the watchpoint.
 * @param pred Predicate to apply.
 * @return True if predicate is satisfied.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool compareComputed(double computed, const std::uint8_t* thresholdBytes,
                                          WatchPredicate pred) noexcept {
  double threshold = 0.0;
  std::memcpy(&threshold, thresholdBytes, sizeof(double));

  switch (pred) {
  case WatchPredicate::GT:
    return computed > threshold;
  case WatchPredicate::LT:
    return computed < threshold;
  case WatchPredicate::GE:
    return computed >= threshold;
  case WatchPredicate::LE:
    return computed <= threshold;
  case WatchPredicate::EQ:
    return computed == threshold;
  case WatchPredicate::NE:
    return computed != threshold;
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

  bool result = false;

  if (wp.function != WatchFunction::NONE) {
    // Compute pipeline: raw -> function -> computed double -> predicate
    const double COMPUTED = applyWatchFunction(wp, data, dataLen);
    result = detail::compareComputed(COMPUTED, wp.threshold.data(), wp.predicate);
  } else {
    // Direct pipeline: raw bytes -> predicate
    result = evaluatePredicate(wp, data, dataLen);
  }

  const bool EDGE = result && !wp.lastResult;
  wp.lastResult = result;

  // Reset fireCount when predicate goes false (re-arm debounce)
  if (!result) {
    wp.fireCount = 0;
  }

  // Update lastValue for CHANGED predicate and STALE function
  if (wp.predicate == WatchPredicate::CHANGED || wp.function == WatchFunction::STALE) {
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
constexpr std::size_t WATCHPOINT_GROUP_MAX_REFS = Config::GROUP_MAX_REFS;

/// Maximum number of concurrent watchpoint groups.
constexpr std::size_t WATCHPOINT_GROUP_TABLE_SIZE = Config::GROUP_COUNT;

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
  std::uint16_t groupId{0};                                    ///< Unique ID (0 = unassigned).
  std::array<std::uint16_t, WATCHPOINT_GROUP_MAX_REFS> refs{}; ///< Watchpoint IDs.
  std::uint8_t count{0};                                       ///< Active reference count.
  GroupLogic logic{GroupLogic::AND};                           ///< Built-in combination logic.
  GroupAssessDelegate customAssess{}; ///< Custom assessment (overrides logic).
  std::uint16_t eventId{0};           ///< Event ID fired on trigger.
  bool armed{false};                  ///< Active flag.
  bool lastResult{false};             ///< Previous group result.
  std::uint32_t fireCount{0};         ///< Times this group fired.
};

/* ----------------------------- Group Evaluation ----------------------------- */

/**
 * @brief Evaluate a watchpoint group against the active watchpoint table.
 * @param group Group to evaluate.
 * @param table Pointer to the watchpoint table.
 * @param tableSize Number of entries in the watchpoint table.
 * @param wpIndex Optional ID-to-index lookup (nullptr = linear scan fallback).
 * @return True if the group condition is currently satisfied.
 * @note RT-safe: O(count) with index, O(count * tableSize) without.
 *
 * References watchpoints by ID. Uses the wpIdToIndex lookup for O(1)
 * ref resolution when available. Falls back to linear scan otherwise.
 */
[[nodiscard]] inline bool evaluateGroup(const WatchpointGroup& group, const DataWatchpoint* table,
                                        std::size_t tableSize,
                                        const std::uint8_t* wpIndex = nullptr) noexcept {
  if (table == nullptr || group.count == 0) {
    return false;
  }

  // Collect lastResult from referenced watchpoints
  std::array<bool, WATCHPOINT_GROUP_MAX_REFS> results{};
  for (std::uint8_t i = 0; i < group.count && i < WATCHPOINT_GROUP_MAX_REFS; ++i) {
    const std::uint16_t REF_ID = group.refs[i];
    results[i] = false;

    // ID 0 is unused/unassigned -- skip
    if (REF_ID == 0) {
      continue;
    }

    // Fast path: use ID-to-index lookup table
    if (wpIndex != nullptr && REF_ID < Config::WATCHPOINT_ID_LIMIT) {
      const std::uint8_t IDX = wpIndex[REF_ID];
      if (IDX < tableSize && table[IDX].armed) {
        results[i] = table[IDX].lastResult;
      }
      continue;
    }

    // Slow path: linear scan (when no index or ID out of range)
    for (std::size_t w = 0; w < tableSize; ++w) {
      if (table[w].armed && table[w].watchpointId == REF_ID) {
        results[i] = table[w].lastResult;
        break;
      }
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
 * @param wpIndex Optional ID-to-index lookup (nullptr = linear scan fallback).
 * @return True if the group result transitioned from false to true.
 * @note RT-safe: O(count) with index.
 */
inline bool evaluateGroupEdge(WatchpointGroup& group, const DataWatchpoint* table,
                              std::size_t tableSize,
                              const std::uint8_t* wpIndex = nullptr) noexcept {
  if (!group.armed) {
    return false;
  }

  const bool RESULT = evaluateGroup(group, table, tableSize, wpIndex);
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
