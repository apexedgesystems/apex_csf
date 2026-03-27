#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_COEFF_TABLE_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_COEFF_TABLE_HPP
/**
 * @file GravityCoeffTable.hpp
 * @brief Random-access reader for 36-byte gravity coefficient records.
 *
 * Layout must match GravityRecord (host-endian, 36 bytes).
 * File is assumed to be ordered by increasing (n, m) with m = 0..n for each n,
 * starting at the first record's degree n0. Compatible with EGM2008 (Earth)
 * and GRGM1200A (Moon) coefficient files.
 */

#include "src/sim/environment/gravity/inc/GravityRecord.hpp"

#include <cstdint>
#include <fstream>
#include <string>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- GravityCoeffTable ----------------------------- */

/**
 * @brief Lightweight random-access view over a packed gravity coefficient table.
 *
 * No exceptions; all operations return bool and write errors to std::cerr.
 * Uses closed-form triangular indexing to compute the byte offset for (n, m).
 *
 * @note NOT RT-safe: File I/O operations.
 */
class GravityCoeffTable {
public:
  GravityCoeffTable() noexcept = default;
  ~GravityCoeffTable() = default;

  /**
   * @brief Open and validate a 36-byte-record file.
   * @param path Path to binary file.
   * @return false on any error.
   * @note NOT RT-safe: File open operation.
   */
  bool open(const std::string& path) noexcept;

  /**
   * @brief Close the file (idempotent).
   * @note NOT RT-safe: File close operation.
   */
  void close() noexcept;

  /**
   * @brief Check if file is open.
   * @return true if open() succeeded and streams are usable.
   * @note RT-safe: O(1).
   */
  bool isOpen() const noexcept { return in_.is_open(); }

  /**
   * @brief First degree in file.
   * @note RT-safe: O(1) cached value.
   */
  int16_t minDegree() const noexcept { return n0_; }

  /**
   * @brief Last degree in file.
   * @note RT-safe: O(1) cached value.
   */
  int16_t maxDegree() const noexcept { return nMax_; }

  /**
   * @brief Total number of records in file.
   * @note RT-safe: O(1) cached value.
   */
  std::uint64_t recordCount() const noexcept { return records_; }

  /**
   * @brief Fetch one record by (n, m).
   * @param n Degree.
   * @param m Order.
   * @param out Output record.
   * @return false if out-of-range or I/O error.
   * @note NOT RT-safe: File seek and read.
   */
  bool read(int16_t n, int16_t m, GravityRecord& out) noexcept;

private:
  bool indexFor(int16_t n, int16_t m, std::uint64_t& outIdx) const noexcept;

  std::ifstream in_{};
  std::string path_{};

  int16_t n0_{0};
  int16_t nMax_{-1};
  std::uint64_t records_{0};
};

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_COEFF_TABLE_HPP
