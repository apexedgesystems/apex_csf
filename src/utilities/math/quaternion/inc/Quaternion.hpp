#ifndef APEX_MATH_QUATERNION_HPP
#define APEX_MATH_QUATERNION_HPP
/**
 * @file Quaternion.hpp
 * @brief Unit quaternion for 3D rotations.
 *
 * Design:
 *  - Composition over data pointer (non-owning view like linalg Array).
 *  - All operations return uint8_t status codes.
 *  - Assumes Hamilton convention: q = w + xi + yj + zk.
 *  - Storage order: [w, x, y, z] (scalar first).
 *
 * Conventions:
 *  - Rotation is applied as: v' = q * v * q^{-1}.
 *  - Quaternion multiplication follows Hamilton convention (ijk = -1).
 *
 * @note RT-SAFE: All operations noexcept, no allocations.
 */

#include "src/utilities/math/quaternion/inc/QuaternionStatus.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace apex {
namespace math {
namespace quaternion {

/* ------------------------------- Quaternion ------------------------------- */

/**
 * @brief Non-owning quaternion view over 4-element storage.
 *
 * @tparam T Element type (float or double).
 *
 * @note RT-SAFE: All operations noexcept, no allocations.
 */
template <typename T> class Quaternion {
  static_assert(std::is_same<T, float>::value || std::is_same<T, double>::value,
                "Quaternion<T> supports only float or double.");

public:
  using ValueType = T;
  static constexpr std::size_t K_SIZE = 4;

  /* --------------------------- Construction ------------------------------ */

  /**
   * @brief Construct from raw pointer to 4-element storage [w, x, y, z].
   */
  explicit Quaternion(T* data) noexcept;

  /* ----------------------------- Accessors ------------------------------- */

  T* data() noexcept { return data_; }
  const T* data() const noexcept { return data_; }

  T& w() noexcept { return data_[0]; }
  T w() const noexcept { return data_[0]; }
  T& x() noexcept { return data_[1]; }
  T x() const noexcept { return data_[1]; }
  T& y() noexcept { return data_[2]; }
  T y() const noexcept { return data_[2]; }
  T& z() noexcept { return data_[3]; }
  T z() const noexcept { return data_[3]; }

  /* ------------------------- Set Operations ------------------------------ */

  /**
   * @brief Set to identity quaternion [1, 0, 0, 0].
   * @note RT-SAFE: No allocation.
   */
  uint8_t setIdentity() noexcept;

  /**
   * @brief Set from components [w, x, y, z].
   * @note RT-SAFE: No allocation.
   */
  uint8_t set(T w, T x, T y, T z) noexcept;

  /**
   * @brief Set from angle-axis representation.
   * @param angleRad Rotation angle in radians.
   * @param axisX, axisY, axisZ Unit axis components.
   * @note RT-SAFE: No allocation. Axis must be pre-normalized.
   */
  uint8_t setFromAngleAxis(T angleRad, T axisX, T axisY, T axisZ) noexcept;

  /* ------------------------- Basic Operations ---------------------------- */

  /**
   * @brief out = ||q||_2 (Euclidean norm).
   * @note RT-SAFE: No allocation.
   */
  uint8_t normInto(T& out) const noexcept;

  /**
   * @brief Normalize in place to unit quaternion.
   * @note RT-SAFE: No allocation.
   */
  uint8_t normalizeInPlace() noexcept;

  /**
   * @brief out = q* (conjugate: w, -x, -y, -z).
   * @note RT-SAFE: No allocation.
   */
  uint8_t conjugateInto(Quaternion<T>& out) const noexcept;

  /**
   * @brief out = q^{-1} (inverse; equals conjugate for unit quaternions).
   * @note RT-SAFE: No allocation.
   */
  uint8_t inverseInto(Quaternion<T>& out) const noexcept;

  /* ---------------------- Quaternion Multiplication ---------------------- */

  /**
   * @brief out = this * b (Hamilton product).
   * @note RT-SAFE: No allocation.
   */
  uint8_t multiplyInto(const Quaternion<T>& b, Quaternion<T>& out) const noexcept;

  /* ---------------------- Vector Rotation -------------------------------- */

  /**
   * @brief Rotate vector v by this quaternion: v' = q * v * q^{-1}.
   *
   * @param vIn Input vector [x, y, z] (3 elements).
   * @param vOut Output vector [x', y', z'] (3 elements).
   * @note RT-SAFE: No allocation. Assumes unit quaternion.
   */
  uint8_t rotateVectorInto(const T* vIn, T* vOut) const noexcept;

  /* ---------------------- Conversion Operations -------------------------- */

  /**
   * @brief Convert to 3x3 rotation matrix (row-major).
   *
   * @param matOut Output 9-element array (3x3 row-major).
   * @note RT-SAFE: No allocation.
   */
  uint8_t toRotationMatrixInto(T* matOut) const noexcept;

  /**
   * @brief Extract angle-axis representation.
   *
   * @param angleRad Output rotation angle in radians [0, pi].
   * @param axisX, axisY, axisZ Output unit axis components.
   * @note RT-SAFE: No allocation.
   */
  uint8_t toAngleAxisInto(T& angleRad, T& axisX, T& axisY, T& axisZ) const noexcept;

  /* ---------------------- Interpolation ---------------------------------- */

  /**
   * @brief Spherical linear interpolation (SLERP).
   *
   * @param b Target quaternion.
   * @param t Interpolation parameter [0, 1].
   * @param out Output quaternion.
   * @note RT-SAFE: No allocation. Both quaternions should be unit.
   */
  uint8_t slerpInto(const Quaternion<T>& b, T t, Quaternion<T>& out) const noexcept;

private:
  T* data_;
};

} // namespace quaternion
} // namespace math
} // namespace apex

#include "src/utilities/math/quaternion/src/Quaternion.tpp"

#endif // APEX_MATH_QUATERNION_HPP
