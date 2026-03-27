/**
 * @file GravityCoeffTable.cpp
 * @brief Random-access reader for 36-byte gravity coefficient records.
 */

#include "src/sim/environment/gravity/inc/GravityCoeffTable.hpp"

#include <iostream>
#include <vector>

namespace sim {
namespace environment {
namespace gravity {

/* ----------------------------- GravityCoeffTable Methods ----------------------------- */

bool GravityCoeffTable::open(const std::string& path) noexcept {
  close();

  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    std::cerr << "[GravityCoeffTable] Failed to open: " << path << "\n";
    return false;
  }

  // Compute file size.
  in.seekg(0, std::ios::end);
  const std::streamoff end = in.tellg();
  if (end < 0) {
    std::cerr << "[GravityCoeffTable] tellg() failed\n";
    return false;
  }
  if ((end % static_cast<std::streamoff>(K_GRAVITY_RECORD_SIZE)) != 0) {
    std::cerr << "[GravityCoeffTable] File size is not a multiple of record size ("
              << K_GRAVITY_RECORD_SIZE << ")\n";
    return false;
  }

  records_ = static_cast<std::uint64_t>(end / static_cast<std::streamoff>(K_GRAVITY_RECORD_SIZE));
  if (records_ == 0) {
    std::cerr << "[GravityCoeffTable] Empty file\n";
    return false;
  }

  // Use larger i/o buffers for fewer syscalls.
  static constexpr std::size_t K_IO_BUF_SIZE = 1u << 20; // 1 MiB
  std::vector<char> inBuf(K_IO_BUF_SIZE);
  // Note: std::filebuf::pubsetbuf does not take ownership; keep vector alive by moving into member.
  in_.swap(in);
  in_.rdbuf()->pubsetbuf(inBuf.data(), inBuf.size());
  // Stash the buffer so its lifetime matches the stream.
  static_cast<void>(inBuf); // intentionally unused after pubsetbuf (lifetime tied to this scope)

  path_ = path;

  // Read first record to get n0.
  GravityRecord first{};
  in_.seekg(0, std::ios::beg);
  if (!in_.read(reinterpret_cast<char*>(&first),
                static_cast<std::streamsize>(K_GRAVITY_RECORD_SIZE))) {
    std::cerr << "[GravityCoeffTable] Failed to read first record\n";
    close();
    return false;
  }
  n0_ = first.n;

  // Read last record to derive nMax.
  GravityRecord last{};
  in_.seekg(static_cast<std::streamoff>((records_ - 1) * K_GRAVITY_RECORD_SIZE), std::ios::beg);
  if (!in_.read(reinterpret_cast<char*>(&last),
                static_cast<std::streamsize>(K_GRAVITY_RECORD_SIZE))) {
    std::cerr << "[GravityCoeffTable] Failed to read last record\n";
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
    std::cerr << "[GravityCoeffTable] Record count mismatch. Expected triangular " << expected
              << ", found " << records_ << ". File may not be a dense (n, m=0..n) triangle.\n";
    close();
    return false;
  }

  return true;
}

void GravityCoeffTable::close() noexcept {
  if (in_.is_open())
    in_.close();
  path_.clear();
  n0_ = 0;
  nMax_ = -1;
  records_ = 0;
}

bool GravityCoeffTable::indexFor(int16_t n, int16_t m, std::uint64_t& outIdx) const noexcept {
  if (!in_.is_open())
    return false;
  if (n < n0_ || n > nMax_)
    return false;
  if (m < 0 || m > n)
    return false;

  // Base index for degree n = sum_{d=n0}^{n-1} (d+1) = n*(n+1)/2 - n0*(n0+1)/2
  const std::uint64_t triN = static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(n + 1) / 2;
  const std::uint64_t triN0 =
      static_cast<std::uint64_t>(n0_) * static_cast<std::uint64_t>(n0_ + 1) / 2;
  const std::uint64_t base = triN - triN0;
  const std::uint64_t idx = base + static_cast<std::uint64_t>(m);

  if (idx >= records_)
    return false;
  outIdx = idx;
  return true;
}

bool GravityCoeffTable::read(int16_t n, int16_t m, GravityRecord& out) noexcept {
  std::uint64_t idx = 0;
  if (!indexFor(n, m, idx))
    return false;

  const std::streamoff pos = static_cast<std::streamoff>(idx * K_GRAVITY_RECORD_SIZE);

  in_.seekg(pos, std::ios::beg);
  if (!in_.good())
    return false;

  if (!in_.read(reinterpret_cast<char*>(&out),
                static_cast<std::streamsize>(K_GRAVITY_RECORD_SIZE))) {
    return false;
  }
  return true;
}

} // namespace gravity
} // namespace environment
} // namespace sim
