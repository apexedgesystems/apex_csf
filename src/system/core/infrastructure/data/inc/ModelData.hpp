#ifndef APEX_SYSTEM_CORE_DATA_MODEL_DATA_HPP
#define APEX_SYSTEM_CORE_DATA_MODEL_DATA_HPP
/**
 * @file ModelData.hpp
 * @brief Typed container for model data with category-based access control.
 *
 * Wraps a trivially-copyable struct with compile-time category semantics:
 *   - STATIC_PARAM: Read-only after construction
 *   - TUNABLE_PARAM: Read-write at runtime
 *   - STATE: Read-write internal model state
 *   - INPUT: External data fed to model
 *   - OUTPUT: Data produced by model
 *
 * RT-safe: No allocation, all operations O(1) or O(sizeof(T)), noexcept.
 *
 * Usage:
 * @code
 *   // Define model data blocks
 *   struct PhysicalConstants {
 *     double gravity;
 *     double airDensity;
 *   };
 *
 *   struct ControlGains {
 *     double kp;
 *     double ki;
 *     double kd;
 *   };
 *
 *   // Create typed data blocks
 *   ModelData<PhysicalConstants, DataCategory::STATIC_PARAM> constants{{9.81, 1.225}};
 *   ModelData<ControlGains, DataCategory::TUNABLE_PARAM> gains{{1.0, 0.1, 0.01}};
 *
 *   // Read access (always available)
 *   double g = constants.get().gravity;
 *   double kp = gains->kp;
 *
 *   // Write access (only for non-STATIC categories)
 *   gains.get().kp = 2.0;         // OK - TUNABLE_PARAM is writable
 *   // constants.get().gravity = 10.0;  // Compile error - STATIC_PARAM
 *
 *   // For I/O with endianness/fault injection, use MasterDataProxy:
 *   ModelData<Telemetry, DataCategory::OUTPUT> telemetry;
 *   MasterDataProxy<Telemetry, true, false> proxy(&telemetry.get());
 *   proxy.resolve();  // Applies byte swap to output
 * @endcode
 */

#include "src/system/core/infrastructure/data/inc/DataCategory.hpp"

#include <cstdint>
#include <type_traits>
#include <utility>

namespace system_core {
namespace data {

/* ----------------------------- ModelData ----------------------------- */

/**
 * @class ModelData
 * @brief Typed container associating data with a semantic category.
 *
 * Provides compile-time access control based on category:
 *   - STATIC_PARAM: Only const access (read-only after init)
 *   - All others: Full read-write access
 *
 * @tparam T Trivially-copyable data type.
 * @tparam Category Semantic category from DataCategory enum.
 *
 * @note RT-safe: All operations bounded, noexcept, no allocation.
 */
template <typename T, DataCategory Category> class ModelData {
  static_assert(std::is_trivially_copyable_v<T>, "ModelData requires trivially copyable types");

public:
  /// The wrapped data type.
  using ValueType = T;

  /// The semantic category.
  static constexpr DataCategory CATEGORY = Category;

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Default constructor. Value-initializes the data.
   */
  ModelData() noexcept : data_{} {}

  /**
   * @brief Construct with initial value.
   * @param initial Initial data value.
   */
  explicit ModelData(const T& initial) noexcept : data_{initial} {}

  /**
   * @brief Copy constructor.
   */
  ModelData(const ModelData&) noexcept = default;

  /**
   * @brief Move constructor.
   */
  ModelData(ModelData&&) noexcept = default;

  /**
   * @brief Copy assignment.
   * @note Compile error for STATIC_PARAM to enforce immutability.
   */
  ModelData& operator=(const ModelData& other) noexcept {
    static_assert(Category != DataCategory::STATIC_PARAM,
                  "Cannot assign to STATIC_PARAM ModelData");
    data_ = other.data_;
    return *this;
  }

  /**
   * @brief Move assignment.
   * @note Compile error for STATIC_PARAM to enforce immutability.
   */
  ModelData& operator=(ModelData&& other) noexcept {
    static_assert(Category != DataCategory::STATIC_PARAM,
                  "Cannot assign to STATIC_PARAM ModelData");
    data_ = std::move(other.data_);
    return *this;
  }

  /* ----------------------------- Read Access ----------------------------- */

  /**
   * @brief Get const reference to data.
   * @return Const reference to wrapped value.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const T& get() const noexcept { return data_; }

  /**
   * @brief Arrow operator for const member access.
   * @return Const pointer to wrapped value.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const T* operator->() const noexcept { return &data_; }

  /**
   * @brief Dereference operator for const access.
   * @return Const reference to wrapped value.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const T& operator*() const noexcept { return data_; }

  /**
   * @brief Get pointer to data for proxy integration.
   * @return Const pointer to data.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const T* ptr() const noexcept { return &data_; }

  /* ----------------------------- Write Access ----------------------------- */

  /**
   * @brief Get mutable reference to data.
   * @return Mutable reference to wrapped value.
   * @note Only available for non-STATIC_PARAM categories.
   * @note RT-safe: O(1).
   */
  template <DataCategory C = Category>
  [[nodiscard]] std::enable_if_t<C != DataCategory::STATIC_PARAM, T&> get() noexcept {
    return data_;
  }

  /**
   * @brief Arrow operator for mutable member access.
   * @return Mutable pointer to wrapped value.
   * @note Only available for non-STATIC_PARAM categories.
   * @note RT-safe: O(1).
   */
  template <DataCategory C = Category>
  [[nodiscard]] std::enable_if_t<C != DataCategory::STATIC_PARAM, T*> operator->() noexcept {
    return &data_;
  }

  /**
   * @brief Set data value.
   * @param value New value to assign.
   * @note Only available for non-STATIC_PARAM categories.
   * @note RT-safe: O(sizeof(T)).
   */
  template <DataCategory C = Category>
  std::enable_if_t<C != DataCategory::STATIC_PARAM> set(const T& value) noexcept {
    data_ = value;
  }

  /**
   * @brief Get mutable pointer to data for proxy integration.
   * @return Mutable pointer to data.
   * @note Only available for non-STATIC_PARAM categories.
   * @note RT-safe: O(1).
   */
  template <DataCategory C = Category>
  [[nodiscard]] std::enable_if_t<C != DataCategory::STATIC_PARAM, T*> ptr() noexcept {
    return &data_;
  }

  /* ----------------------------- Category Queries ----------------------------- */

  /**
   * @brief Get the data category.
   * @return Category template parameter.
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr DataCategory category() noexcept { return Category; }

  /**
   * @brief Check if data is read-only (STATIC_PARAM).
   * @return True if category is STATIC_PARAM.
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr bool isReadOnly() noexcept {
    return Category == DataCategory::STATIC_PARAM;
  }

  /**
   * @brief Check if data is a parameter type.
   * @return True if STATIC_PARAM or TUNABLE_PARAM.
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr bool isParam() noexcept {
    return Category == DataCategory::STATIC_PARAM || Category == DataCategory::TUNABLE_PARAM;
  }

  /**
   * @brief Check if data is model input.
   * @return True if INPUT or any PARAM type.
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr bool isModelInput() noexcept {
    return Category == DataCategory::INPUT || isParam();
  }

  /**
   * @brief Check if data is model output.
   * @return True if OUTPUT.
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr bool isModelOutput() noexcept {
    return Category == DataCategory::OUTPUT;
  }

  /**
   * @brief Check if data is internal state.
   * @return True if STATE.
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr bool isState() noexcept { return Category == DataCategory::STATE; }

  /* ----------------------------- Size ----------------------------- */

  /**
   * @brief Get size of wrapped data.
   * @return sizeof(T).
   * @note RT-safe: O(1), constexpr.
   */
  [[nodiscard]] static constexpr std::size_t size() noexcept { return sizeof(T); }

private:
  T data_; ///< Wrapped data value.
};

/* ----------------------------- Type Aliases ----------------------------- */

/**
 * @brief Alias for static (read-only) parameter data.
 * @tparam T Data type.
 */
template <typename T> using StaticParam = ModelData<T, DataCategory::STATIC_PARAM>;

/**
 * @brief Alias for tunable (runtime-adjustable) parameter data.
 * @tparam T Data type.
 */
template <typename T> using TunableParam = ModelData<T, DataCategory::TUNABLE_PARAM>;

/**
 * @brief Alias for internal state data.
 * @tparam T Data type.
 */
template <typename T> using State = ModelData<T, DataCategory::STATE>;

/**
 * @brief Alias for input data.
 * @tparam T Data type.
 */
template <typename T> using Input = ModelData<T, DataCategory::INPUT>;

/**
 * @brief Alias for output data.
 * @tparam T Data type.
 */
template <typename T> using Output = ModelData<T, DataCategory::OUTPUT>;

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_MODEL_DATA_HPP
