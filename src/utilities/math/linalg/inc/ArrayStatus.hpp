#ifndef APEX_MATH_LINALG_ARRAY_STATUS_HPP
#define APEX_MATH_LINALG_ARRAY_STATUS_HPP
/**
 * @file ArrayStatus.hpp
 * @brief Status codes for linear algebra operations (uint8_t).
 *
 * @note RT-SAFE: Enum only, no allocations or system calls.
 */

#include <cstdint>

namespace apex {
namespace math {
namespace linalg {

/* -------------------------------- Status ---------------------------------- */

/**
 * @brief Result codes for array/matrix/vector operations.
 *
 * Returned as uint8_t values for consistency with other system modules.
 * Errors are < 128; warnings are >= 128.
 *
 * @note RT-SAFE: Enum only.
 */
enum class Status : std::uint8_t {
  SUCCESS = 0, ///< Operation succeeded.

  /* ----------------------- Generic / BLAS / LAPACK ----------------------- */
  ERROR_SIZE_MISMATCH = 1,  ///< Dimensions incompatible.
  ERROR_OUT_OF_BOUNDS = 2,  ///< Index exceeded bounds.
  ERROR_NON_CONTIGUOUS = 3, ///< Data layout not contiguous where required.
  ERROR_INVALID_LAYOUT = 4, ///< Layout (row/col) not supported by the op.
  ERROR_NOT_SQUARE = 5,     ///< Matrix is not square.
  ERROR_SINGULAR = 6,       ///< Singular (non-invertible) matrix or zero pivot.
  ERROR_LIB_FAILURE = 7,    ///< Underlying library call failed.
  ERROR_INVALID_VALUE = 8,  ///< Value/domain error (e.g., NaN, bad enum).
  ERROR_UNSUPPORTED_OP = 9, ///< Operation not supported for this type/shape.

  /* ----------------------------- CUDA-specific --------------------------- */
  ERROR_POINTER_DOMAIN = 10,     ///< Host/device pointer mismatch.
  ERROR_DEVICE_ALLOC = 11,       ///< Device (or pinned) allocation failure.
  ERROR_DEVICE_UNAVAILABLE = 12, ///< CUDA runtime/device not available.
  ERROR_BAD_STREAM = 13,         ///< Invalid or incompatible stream handle.
  ERROR_INT_OVERFLOW = 14,       ///< Dimensions exceed 32-bit int range.
  ERROR_ALIASING = 15,           ///< Disallowed operand overlap detected.

  /* -------------------------------- Warnings ----------------------------- */
  WARNING_LAYOUT_REINTERPRETED = 128, ///< Layout adjusted internally.
  WARNING_PRECISION_LOSS = 129,       ///< Potential precision loss.

  ERROR_UNKNOWN = 255 ///< Unspecified failure.
};

/* ------------------------------ Helpers ---------------------------------- */

/** @brief Check if status indicates success. RT-SAFE. */
inline constexpr bool ok(Status s) noexcept { return s == Status::SUCCESS; }

/** @brief Check if status indicates failure. RT-SAFE. */
inline constexpr bool failed(Status s) noexcept { return s != Status::SUCCESS; }

} // namespace linalg
} // namespace math
} // namespace apex

#endif // APEX_MATH_LINALG_ARRAY_STATUS_HPP
