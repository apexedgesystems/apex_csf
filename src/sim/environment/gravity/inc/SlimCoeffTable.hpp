#ifndef APEX_SIM_ENVIRONMENT_GRAVITY_SLIM_COEFF_TABLE_HPP
#define APEX_SIM_ENVIRONMENT_GRAVITY_SLIM_COEFF_TABLE_HPP
/**
 * @file SlimCoeffTable.hpp
 * @brief Random-access reader for compact gravity coefficient records.
 *
 * Supports 20-byte (GravityNSRecordD) and 12-byte (GravityNSRecordF) formats.
 * These formats omit sigma uncertainties for reduced file size.
 *
 * File layout must be ordered by increasing (n, m) with m = 0..n for each n,
 * starting at the first record's degree n0.
 */

#include "src/sim/environment/gravity/inc/GravityRecord.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- SlimCoeffTable ----------------------------- */

/**
 * @brief Templated random-access view over compact gravity coefficient files.
 *
 * @tparam RecordT Record type (GravityNSRecordD or GravityNSRecordF).
 *
 * No exceptions; all operations return bool and write errors to std::cerr.
 * Uses closed-form triangular indexing to compute the byte offset for (n, m).
 *
 * @note NOT RT-safe: File I/O operations.
 */
template <typename RecordT> class SlimCoeffTable {
public:
  /// Record size in bytes.
  static constexpr std::size_t RECORD_SIZE = sizeof(RecordT);

  SlimCoeffTable() noexcept = default;
  ~SlimCoeffTable() = default;

  /**
   * @brief Open and validate a compact record file.
   * @param path Path to binary file.
   * @return false on any error.
   * @note NOT RT-safe: File open operation.
   */
  bool open(const std::string& path) noexcept {
    close();

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
      std::cerr << "[SlimCoeffTable] Failed to open: " << path << "\n";
      return false;
    }

    // Compute file size.
    in.seekg(0, std::ios::end);
    const std::streamoff end = in.tellg();
    if (end < 0) {
      std::cerr << "[SlimCoeffTable] tellg() failed\n";
      return false;
    }
    if ((end % static_cast<std::streamoff>(RECORD_SIZE)) != 0) {
      std::cerr << "[SlimCoeffTable] File size is not a multiple of record size (" << RECORD_SIZE
                << ")\n";
      return false;
    }

    records_ = static_cast<std::uint64_t>(end / static_cast<std::streamoff>(RECORD_SIZE));
    if (records_ == 0) {
      std::cerr << "[SlimCoeffTable] Empty file\n";
      return false;
    }

    in_.swap(in);
    path_ = path;

    // Read first record to get n0.
    RecordT first{};
    in_.seekg(0, std::ios::beg);
    if (!in_.read(reinterpret_cast<char*>(&first), static_cast<std::streamsize>(RECORD_SIZE))) {
      std::cerr << "[SlimCoeffTable] Failed to read first record\n";
      close();
      return false;
    }
    n0_ = first.n;

    // Read last record to derive nMax.
    RecordT last{};
    in_.seekg(static_cast<std::streamoff>((records_ - 1) * RECORD_SIZE), std::ios::beg);
    if (!in_.read(reinterpret_cast<char*>(&last), static_cast<std::streamsize>(RECORD_SIZE))) {
      std::cerr << "[SlimCoeffTable] Failed to read last record\n";
      close();
      return false;
    }
    nMax_ = last.n;

    // Sanity: verify triangular count matches records_:
    // sum_{n=n0..nMax} (n+1) = (nMax+1)(nMax+2)/2 - n0*(n0+1)/2
    const std::uint64_t triMax =
        static_cast<std::uint64_t>(nMax_ + 1) * static_cast<std::uint64_t>(nMax_ + 2) / 2;
    const std::uint64_t triN0 =
        static_cast<std::uint64_t>(n0_) * static_cast<std::uint64_t>(n0_ + 1) / 2;
    const std::uint64_t expected = triMax - triN0;
    if (expected != records_) {
      std::cerr << "[SlimCoeffTable] Record count mismatch. Expected triangular " << expected
                << ", found " << records_ << ". File may not be a dense (n, m=0..n) triangle.\n";
      close();
      return false;
    }

    return true;
  }

  /**
   * @brief Close the file (idempotent).
   * @note NOT RT-safe: File close operation.
   */
  void close() noexcept {
    if (in_.is_open())
      in_.close();
    path_.clear();
    n0_ = 0;
    nMax_ = -1;
    records_ = 0;
  }

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
  bool read(int16_t n, int16_t m, RecordT& out) noexcept {
    std::uint64_t idx = 0;
    if (!indexFor(n, m, idx))
      return false;

    const std::streamoff pos = static_cast<std::streamoff>(idx * RECORD_SIZE);

    in_.seekg(pos, std::ios::beg);
    if (!in_.good())
      return false;

    if (!in_.read(reinterpret_cast<char*>(&out), static_cast<std::streamsize>(RECORD_SIZE))) {
      return false;
    }
    return true;
  }

private:
  bool indexFor(int16_t n, int16_t m, std::uint64_t& outIdx) const noexcept {
    if (!in_.is_open())
      return false;
    if (n < n0_ || n > nMax_)
      return false;
    if (m < 0 || m > n)
      return false;

    // Base index for degree n = sum_{d=n0}^{n-1} (d+1) = n*(n+1)/2 - n0*(n0+1)/2
    const std::uint64_t triN =
        static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(n + 1) / 2;
    const std::uint64_t triN0 =
        static_cast<std::uint64_t>(n0_) * static_cast<std::uint64_t>(n0_ + 1) / 2;
    const std::uint64_t base = triN - triN0;
    const std::uint64_t idx = base + static_cast<std::uint64_t>(m);

    if (idx >= records_)
      return false;
    outIdx = idx;
    return true;
  }

  mutable std::ifstream in_{};
  std::string path_{};

  int16_t n0_{0};
  int16_t nMax_{-1};
  std::uint64_t records_{0};
};

/* ----------------------------- Type Aliases ----------------------------- */

/// 20-byte record table (double precision, no sigmas).
using SlimCoeffTableD = SlimCoeffTable<GravityNSRecordD>;

/// 12-byte record table (single precision, no sigmas).
using SlimCoeffTableF = SlimCoeffTable<GravityNSRecordF>;

} // namespace gravity
} // namespace environment
} // namespace sim

#endif // APEX_SIM_ENVIRONMENT_GRAVITY_SLIM_COEFF_TABLE_HPP
