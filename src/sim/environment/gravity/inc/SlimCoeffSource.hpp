#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_SLIM_COEFF_SOURCE_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_SLIM_COEFF_SOURCE_HPP
/**
 * @file SlimCoeffSource.hpp
 * @brief CoeffSource adapters for compact gravity coefficient formats.
 *
 * Provides coefficient sources for:
 *  - SlimCoeffSourceD: 20-byte records (double precision, no sigmas)
 *  - SlimCoeffSourceF: 12-byte records (single precision, no sigmas)
 *
 * These compact formats reduce file size by ~45% (D) or ~67% (F) compared
 * to the full 36-byte format, at the cost of losing uncertainty values.
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/SlimCoeffTable.hpp"

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- SlimCoeffSourceD ----------------------------- */

/**
 * @brief CoeffSource implementation for 20-byte double-precision records.
 *
 * Provides random-access coefficient lookup from a compact binary file.
 * Uses GravityNSRecordD format (n, m, Cbar, Sbar) without sigma uncertainties.
 *
 * File size comparison (EGM2008 N=2190):
 *  - Full format (36 bytes): ~86 MB
 *  - Slim double (20 bytes): ~48 MB (~45% smaller)
 *
 * @note NOT RT-safe: File I/O on each get() call. For RT use, pre-load coefficients.
 */
class SlimCoeffSourceD final : public CoeffSource {
public:
  SlimCoeffSourceD() noexcept = default;

  /**
   * @brief Open a 20-byte record binary file.
   * @param path Path to binary file.
   * @return true on success.
   * @note NOT RT-safe: File open operation.
   */
  bool open(const std::string& path) noexcept { return tbl_.open(path); }

  /**
   * @brief Minimum degree in file.
   * @note RT-safe: O(1) cached value.
   */
  int16_t minDegree() const noexcept override { return tbl_.minDegree(); }

  /**
   * @brief Maximum degree in file.
   * @note RT-safe: O(1) cached value.
   */
  int16_t maxDegree() const noexcept override { return tbl_.maxDegree(); }

  /**
   * @brief Retrieve coefficients by degree/order.
   * @param n Degree.
   * @param m Order.
   * @param C Output Cbar_nm.
   * @param S Output Sbar_nm.
   * @return true if found.
   * @note NOT RT-safe: File seek and read.
   */
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    GravityNSRecordD rec{};
    if (!tbl_.read(n, m, rec))
      return false;
    C = rec.Cbar;
    S = rec.Sbar;
    return true;
  }

private:
  mutable SlimCoeffTableD tbl_;
};

/* ----------------------------- SlimCoeffSourceF ----------------------------- */

/**
 * @brief CoeffSource implementation for 12-byte single-precision records.
 *
 * Provides random-access coefficient lookup from a compact binary file.
 * Uses GravityNSRecordF format (n, m, cbar, sbar) with float precision.
 *
 * File size comparison (EGM2008 N=2190):
 *  - Full format (36 bytes): ~86 MB
 *  - Slim float (12 bytes): ~29 MB (~67% smaller)
 *
 * @warning Single precision may introduce numerical errors for high-degree
 *          coefficients. Use SlimCoeffSourceD for precision-critical applications.
 *
 * @note NOT RT-safe: File I/O on each get() call. For RT use, pre-load coefficients.
 */
class SlimCoeffSourceF final : public CoeffSource {
public:
  SlimCoeffSourceF() noexcept = default;

  /**
   * @brief Open a 12-byte record binary file.
   * @param path Path to binary file.
   * @return true on success.
   * @note NOT RT-safe: File open operation.
   */
  bool open(const std::string& path) noexcept { return tbl_.open(path); }

  /**
   * @brief Minimum degree in file.
   * @note RT-safe: O(1) cached value.
   */
  int16_t minDegree() const noexcept override { return tbl_.minDegree(); }

  /**
   * @brief Maximum degree in file.
   * @note RT-safe: O(1) cached value.
   */
  int16_t maxDegree() const noexcept override { return tbl_.maxDegree(); }

  /**
   * @brief Retrieve coefficients by degree/order.
   * @param n Degree.
   * @param m Order.
   * @param C Output Cbar_nm (promoted to double).
   * @param S Output Sbar_nm (promoted to double).
   * @return true if found.
   * @note NOT RT-safe: File seek and read.
   */
  bool get(int16_t n, int16_t m, double& C, double& S) const noexcept override {
    GravityNSRecordF rec{};
    if (!tbl_.read(n, m, rec))
      return false;
    C = static_cast<double>(rec.cbar);
    S = static_cast<double>(rec.sbar);
    return true;
  }

private:
  mutable SlimCoeffTableF tbl_;
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_SLIM_COEFF_SOURCE_HPP
