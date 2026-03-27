#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_FULL_TABLE_COEFF_SOURCE_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_FULL_TABLE_COEFF_SOURCE_HPP
/**
 * @file FullTableCoeffSource.hpp
 * @brief CoeffSource adapter wrapping GravityCoeffTable (36-byte records).
 */

#include "src/sim/environment/gravity/inc/CoeffSource.hpp"
#include "src/sim/environment/gravity/inc/GravityCoeffTable.hpp"

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- FullTableCoeffSource ----------------------------- */

/**
 * @brief CoeffSource implementation backed by GravityCoeffTable binary file.
 *
 * Provides random-access coefficient lookup from a pre-packed binary file.
 * Compatible with EGM2008 (Earth) and GRGM1200A (Moon) coefficient files.
 *
 * @note NOT RT-safe: File I/O on each get() call. For RT use, pre-load coefficients.
 */
class FullTableCoeffSource final : public CoeffSource {
public:
  FullTableCoeffSource() noexcept = default;

  /**
   * @brief Open a 36-byte record binary file.
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
    GravityRecord rec{};
    if (!tbl_.read(n, m, rec))
      return false;
    C = rec.Cbar;
    S = rec.Sbar;
    return true;
  }

private:
  mutable GravityCoeffTable tbl_;
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_FULL_TABLE_COEFF_SOURCE_HPP
