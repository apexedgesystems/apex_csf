/**
 * @file MnaSystemSparse.cpp
 * @brief Implementation of sparse MNA solver using KLU (SuiteSparse).
 */

#include "src/sim/electronics/algorithms/mna/inc/MnaSystemSparse.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>

namespace sim::electronics::mna {

/* ----------------------------- Constructor ----------------------------- */

MnaSystemSparse::MnaSystemSparse(std::size_t netCount) : netCount_(netCount), I_(netCount, 0.0) {
  // Reserve for ~4 entries per net (typical for circuit matrices: each
  // transistor stamps 3-5 entries, and most circuits have more transistors
  // than nets). The vector retains capacity across clear() calls, so this
  // only matters for the first factorize.
  triplets_.reserve(netCount * 4);
  klu_defaults(&kluCommon_);
}

MnaSystemSparse::~MnaSystemSparse() { freeKluFactors(); }

void MnaSystemSparse::freeKluFactors() {
  if (kluNumeric_ != nullptr) {
    klu_free_numeric(&kluNumeric_, &kluCommon_);
    kluNumeric_ = nullptr;
  }
  if (kluSymbolic_ != nullptr) {
    klu_free_symbolic(&kluSymbolic_, &kluCommon_);
    kluSymbolic_ = nullptr;
  }
}

bool MnaSystemSparse::kluSolve(double* rhsSolution, std::size_t dim) {
  int ok = klu_solve(kluSymbolic_, kluNumeric_, static_cast<int>(dim), 1, rhsSolution, &kluCommon_);
  return ok != 0;
}

/* ----------------------------- Stamping ----------------------------- */

void MnaSystemSparse::addConductance(NetID a, NetID b, double g) {
  assert(a < netCount_ && b < netCount_);
  auto ia = static_cast<int>(a);
  auto ib = static_cast<int>(b);
  if (a == b) {
    triplets_.push_back({ia, ia, g});
  } else {
    triplets_.push_back({ia, ia, g});
    triplets_.push_back({ib, ib, g});
    triplets_.push_back({ia, ib, -g});
    triplets_.push_back({ib, ia, -g});
  }
}

void MnaSystemSparse::addCurrent(NetID a, NetID b, double i) {
  assert(a < netCount_ && b < netCount_);
  I_[a] += i;
  I_[b] -= i;
}

void MnaSystemSparse::addMatrixEntry(NetID row, NetID col, double value) {
  assert(row < netCount_ && col < netCount_);
  triplets_.push_back({static_cast<int>(row), static_cast<int>(col), value});
}

std::size_t MnaSystemSparse::addVoltageSource(NetID pos, NetID neg, double v) {
  assert(pos < netCount_ && neg < netCount_);
  vsources_.push_back({pos, neg, v});
  return vsources_.size() - 1;
}

/* ----------------------------- Matrix Build ----------------------------- */

namespace {
inline std::int64_t packRowCol(int row, int col) noexcept {
  return (static_cast<std::int64_t>(static_cast<std::uint32_t>(row)) << 32) |
         static_cast<std::uint32_t>(col);
}
} // namespace

bool MnaSystemSparse::patternMatchesCached() const noexcept {
  if (!cscPatternCached_) {
    return false;
  }
  if (tripletToCscIdx_.size() != triplets_.size()) {
    return false;
  }
  if (vsourceCscIdx_.size() != vsources_.size() * 4) {
    return false;
  }
  const std::size_t N_TRIP = triplets_.size();
  const CooEntry* TRIP = triplets_.data();
  const std::int64_t* CACHED = cachedRowCol_.data();
  for (std::size_t i = 0; i < N_TRIP; ++i) {
    if (packRowCol(TRIP[i].row, TRIP[i].col) != CACHED[i]) {
      return false;
    }
  }
  const std::size_t N_VS = vsources_.size();
  const auto* VS = vsources_.data();
  const std::int64_t* CACHED_VS = cachedVsourceRc_.data();
  for (std::size_t i = 0; i < N_VS; ++i) {
    if (packRowCol(static_cast<int>(VS[i].pos), static_cast<int>(VS[i].neg)) != CACHED_VS[i]) {
      return false;
    }
  }
  return true;
}

void MnaSystemSparse::buildCscFromCachedPattern() {
  // Zero only the value array; pattern (cscAp_/cscAi_) is unchanged.
  std::fill(cscAx_.begin(), cscAx_.end(), 0.0);

  // Scatter triplet values via the pre-recorded mapping.
  const std::size_t N_TRIP = triplets_.size();
  const int* MAP = tripletToCscIdx_.data();
  const CooEntry* TRIP = triplets_.data();
  double* AX = cscAx_.data();
  for (std::size_t i = 0; i < N_TRIP; ++i) {
    int slot = MAP[i];
    if (slot >= 0) {
      AX[slot] += TRIP[i].value;
    }
  }

  // Scatter voltage-source stamps (always +/- 1.0; written, not accumulated,
  // since each vsource produces unique (row,col) pairs outside the triplet set).
  const std::size_t N_VS = vsources_.size();
  for (std::size_t i = 0; i < N_VS; ++i) {
    int posRow = vsourceCscIdx_[4 * i + 0];      // (pos, vsRow) = 1.0
    int posSym = vsourceCscIdx_[4 * i + 1];      // (vsRow, pos) = 1.0
    int negRow = vsourceCscIdx_[4 * i + 2];      // (neg, vsRow) = -1.0
    int negSym = vsourceCscIdx_[4 * i + 3];      // (vsRow, neg) = -1.0
    if (posRow >= 0) AX[posRow] = 1.0;
    if (posSym >= 0) AX[posSym] = 1.0;
    if (negRow >= 0) AX[negRow] = -1.0;
    if (negSym >= 0) AX[negSym] = -1.0;
  }

  // Ground diagonal entry {0,0,1.0}.
  if (groundCscIdx_ >= 0) {
    AX[groundCscIdx_] = 1.0;
  }
}

void MnaSystemSparse::recordCscPattern() {
  // Binary-search each triplet's (row,col) in cscAi_[cscAp_[col]..cscAp_[col+1]).
  // buildCsc filters (row==0 || col==0), so those triplets get -1.
  const std::size_t N_TRIP = triplets_.size();
  tripletToCscIdx_.resize(N_TRIP);
  cachedRowCol_.resize(N_TRIP);
  for (std::size_t i = 0; i < N_TRIP; ++i) {
    int row = triplets_[i].row;
    int col = triplets_[i].col;
    cachedRowCol_[i] = packRowCol(row, col);
    if (row == 0 || col == 0) {
      tripletToCscIdx_[i] = -1;
      continue;
    }
    int start = cscAp_[col];
    int end = cscAp_[col + 1];
    auto it = std::lower_bound(cscAi_.begin() + start, cscAi_.begin() + end, row);
    tripletToCscIdx_[i] = static_cast<int>(it - cscAi_.begin());
  }

  // Record voltage-source slots (4 per vsource, each -1 if pos/neg == 0).
  const std::size_t N_VS = vsources_.size();
  const std::size_t N = netCount_;
  vsourceCscIdx_.resize(4 * N_VS);
  cachedVsourceRc_.resize(N_VS);
  for (std::size_t i = 0; i < N_VS; ++i) {
    int pos = static_cast<int>(vsources_[i].pos);
    int neg = static_cast<int>(vsources_[i].neg);
    int vsRow = static_cast<int>(N + i);
    cachedVsourceRc_[i] = packRowCol(pos, neg);
    auto lookup = [&](int row, int col) -> int {
      int start = cscAp_[col];
      int end = cscAp_[col + 1];
      auto it = std::lower_bound(cscAi_.begin() + start, cscAi_.begin() + end, row);
      return static_cast<int>(it - cscAi_.begin());
    };
    vsourceCscIdx_[4 * i + 0] = (pos != 0) ? lookup(pos, vsRow) : -1;
    vsourceCscIdx_[4 * i + 1] = (pos != 0) ? lookup(vsRow, pos) : -1;
    vsourceCscIdx_[4 * i + 2] = (neg != 0) ? lookup(neg, vsRow) : -1;
    vsourceCscIdx_[4 * i + 3] = (neg != 0) ? lookup(vsRow, neg) : -1;
  }

  // Ground diagonal {0,0,1.0} lives at cscAp_[0].
  if (!cscAp_.empty()) {
    int start = cscAp_[0];
    int end = cscAp_[1];
    auto it = std::lower_bound(cscAi_.begin() + start, cscAi_.begin() + end, 0);
    groundCscIdx_ = (it != cscAi_.begin() + end && *it == 0)
                        ? static_cast<int>(it - cscAi_.begin())
                        : -1;
  } else {
    groundCscIdx_ = -1;
  }

  cscPatternCached_ = true;
}

void MnaSystemSparse::buildCsc() {
  if (patternMatchesCached()) {
    buildCscFromCachedPattern();
    return;
  }

  std::size_t n = netCount_;
  std::size_t m = vsources_.size();
  int dim = static_cast<int>(n + m);

  // Collect augmented COO entries (persistent vector avoids reallocation).
  augCoo_.clear();
  augCoo_.reserve(triplets_.size() + 4 * m + 1);

  for (const auto& t : triplets_) {
    if (t.row != 0 && t.col != 0) {
      augCoo_.push_back(t);
    }
  }

  // Voltage source stamps (skip ground-connected)
  for (std::size_t i = 0; i < m; ++i) {
    const auto& vs = vsources_[i];
    auto vsRow = static_cast<int>(n + i);
    auto posInt = static_cast<int>(vs.pos);
    auto negInt = static_cast<int>(vs.neg);
    if (vs.pos != 0) {
      augCoo_.push_back({posInt, vsRow, 1.0});
      augCoo_.push_back({vsRow, posInt, 1.0});
    }
    if (vs.neg != 0) {
      augCoo_.push_back({negInt, vsRow, -1.0});
      augCoo_.push_back({vsRow, negInt, -1.0});
    }
  }

  augCoo_.push_back({0, 0, 1.0});

  std::size_t nnzRaw = augCoo_.size();

  // Counting sort CSC construction: O(nnz + dim) instead of O(nnz log nnz).
  // Phase 1: Count entries per column.
  cscAp_.assign(dim + 2, 0);
  for (std::size_t i = 0; i < nnzRaw; ++i) {
    cscAp_[augCoo_[i].col + 1]++;
  }

  // Phase 2: Prefix sum -> column pointers (pre-merge).
  for (int c = 0; c < dim; ++c) {
    cscAp_[c + 1] += cscAp_[c];
  }

  // Phase 3: Scatter entries into CSC arrays using column offsets.
  cscAi_.resize(nnzRaw);
  cscAx_.resize(nnzRaw);
  colWork_.assign(cscAp_.begin(), cscAp_.begin() + dim);
  for (std::size_t i = 0; i < nnzRaw; ++i) {
    int pos = colWork_[augCoo_[i].col]++;
    cscAi_[pos] = augCoo_[i].row;
    cscAx_[pos] = augCoo_[i].value;
  }

  // Phase 4: Per-column insertion sort by row + merge duplicates.
  std::size_t writePos = 0;
  for (int c = 0; c < dim; ++c) {
    int colStart = cscAp_[c];
    int colEnd = cscAp_[c + 1];

    // Insertion sort within column (typically 2-5 entries).
    for (int j = colStart + 1; j < colEnd; ++j) {
      int keyRow = cscAi_[j];
      double keyVal = cscAx_[j];
      int k = j - 1;
      while (k >= colStart && cscAi_[k] > keyRow) {
        cscAi_[k + 1] = cscAi_[k];
        cscAx_[k + 1] = cscAx_[k];
        --k;
      }
      cscAi_[k + 1] = keyRow;
      cscAx_[k + 1] = keyVal;
    }

    // Update column pointer and merge duplicates (same row -> sum values).
    cscAp_[c] = static_cast<int>(writePos);
    for (int j = colStart; j < colEnd; ++j) {
      if (writePos > 0 && cscAi_[writePos - 1] == cscAi_[j] &&
          static_cast<int>(writePos - 1) >= cscAp_[c]) {
        cscAx_[writePos - 1] += cscAx_[j];
      } else {
        cscAi_[writePos] = cscAi_[j];
        cscAx_[writePos] = cscAx_[j];
        ++writePos;
      }
    }
  }
  cscAp_[dim] = static_cast<int>(writePos);

  recordCscPattern();
}

void MnaSystemSparse::buildRhsVector() {
  std::size_t n = netCount_;
  std::size_t m = vsources_.size();
  std::size_t dim = n + m;

  b_.assign(dim, 0.0);

  for (std::size_t i = 0; i < n; ++i) {
    b_[i] = I_[i];
  }

  for (std::size_t i = 0; i < m; ++i) {
    b_[n + i] = vsources_[i].v;
  }

  b_[0] = 0.0;
}

/* ----------------------------- Factorize ----------------------------- */

bool MnaSystemSparse::factorize() {
  buildCsc();
  buildRhsVector();

  // KLU fast-path: if symbolic ordering + numeric factor are still
  // valid (same pattern), use `klu_refactor` -- which reuses the
  // partial-pivot ordering and only redoes the numerical work.
  // Typically 5-10x faster than `klu_factor` and matches the NR
  // iteration pattern (structure stable, values change). Falls
  // back to full analyze + factor on any failure.
  //
  // Hot path: every NR iter inside `TransientSolver::solveStep`
  // calls factorize(); without this fast-path, every iter pays the
  // full `klu_analyze` + `klu_factor` cost.
  if (kluSymbolic_ != nullptr && kluNumeric_ != nullptr && cachedVsourceCount_ == vsources_.size()) {
    int ok = klu_refactor(cscAp_.data(), cscAi_.data(), cscAx_.data(), kluSymbolic_, kluNumeric_,
                          &kluCommon_);
    if (ok) {
      factorized_ = true;
      // Skip the cachedTriplets_ copy on the fast path: tripletsMatch()
      // is only consulted by the cached-LU branch which factorize()
      // is bypassing, and the copy is O(nnz) for every NR iter.
      return true;
    }
    // Refactor failed (numerical issue, e.g. zero pivot under the
    // existing pivot ordering); fall through to full factor below.
  }

  freeKluFactors();

  auto dim = static_cast<int>(netCount_ + vsources_.size());
  int* Ap = cscAp_.data();
  int* Ai = cscAi_.data();
  double* Ax = cscAx_.data();

  kluSymbolic_ = klu_analyze(dim, Ap, Ai, &kluCommon_);
  if (kluSymbolic_ == nullptr) {
    factorized_ = false;
    patternAnalyzed_ = false;
    return false;
  }

  kluNumeric_ = klu_factor(Ap, Ai, Ax, kluSymbolic_, &kluCommon_);
  if (kluNumeric_ == nullptr) {
    factorized_ = false;
    patternAnalyzed_ = false;
    return false;
  }
  // KLU_SINGULAR is a warning (status=1): matrix is singular but KLU
  // still produces a result. The result may be inaccurate. For NR iteration,
  // an inaccurate result is acceptable as long as it doesn't produce NaN/Inf.
  // ngspice falls back to full reorder on KLU_SINGULAR (niiter.c line 171)
  // but we accept the result and let the NR clamping handle bad values.

  factorized_ = true;
  patternAnalyzed_ = true;
  cachedTriplets_ = triplets_;
  cachedVsourceCount_ = vsources_.size();
  return true;
}

bool MnaSystemSparse::refactorize() {
  buildCsc();
  buildRhsVector();

  // Fast path: when the symbolic pattern AND a prior numeric factor
  // are both still valid, use `klu_refactor` -- which reuses the
  // partial-pivot ordering from the previous numeric factor and
  // only redoes the numerical work. KLU `klu_refactor` is typically
  // 5-10x faster than `klu_factor` on the same matrix shape, and
  // matches the NR loop pattern exactly (structure stable across
  // iters, only values change). Falls back to full factor on any
  // failure (e.g. structural singularity from a pivot that was
  // valid on the previous iter but not this one).
  if (kluSymbolic_ != nullptr && kluNumeric_ != nullptr) {
    int ok = klu_refactor(cscAp_.data(), cscAi_.data(), cscAx_.data(), kluSymbolic_, kluNumeric_,
                          &kluCommon_);
    if (ok) {
      factorized_ = true;
      // Skip the cachedTriplets_ copy: tripletsMatch() isn't needed
      // on this fast path (see the parallel comment in factorize).
      return true;
    }
    // Refactor failed (numerical issue); fall through to full factor.
  }

  // Full numeric factorization reusing cached symbolic ordering
  if (kluNumeric_ != nullptr) {
    klu_free_numeric(&kluNumeric_, &kluCommon_);
    kluNumeric_ = nullptr;
  }
  kluNumeric_ = klu_factor(cscAp_.data(), cscAi_.data(), cscAx_.data(), kluSymbolic_, &kluCommon_);
  if (kluNumeric_ == nullptr) {
    factorized_ = false;
    return false;
  }

  factorized_ = true;
  cachedTriplets_ = triplets_;
  return true;
}

bool MnaSystemSparse::refactorizeInPlace() {
  // Direct CSC build (buildCsc) eliminates the need for in-place value updates.
  return refactorize();
}

bool MnaSystemSparse::tripletsMatch() const {
  if (triplets_.size() != cachedTriplets_.size()) {
    return false;
  }
  for (std::size_t i = 0; i < triplets_.size(); ++i) {
    if (triplets_[i].row != cachedTriplets_[i].row || triplets_[i].col != cachedTriplets_[i].col ||
        triplets_[i].value != cachedTriplets_[i].value) {
      return false;
    }
  }
  return true;
}

void MnaSystemSparse::updateRhs() {
  buildRhsVector();
  factorized_ = true;
}

/* ----------------------------- Solve ----------------------------- */

MnaResultSparse MnaSystemSparse::solve() {
  MnaResultSparse result;

  if (!factorized_) {
    result.errorMessage = "Matrix not factorized. Call factorize() first.";
    return result;
  }

  std::size_t n = netCount_;
  std::size_t m = vsources_.size();
  std::size_t dim = n + m;

  x_ = b_;
  if (!kluSolve(x_.data(), dim)) {
    result.errorMessage = "KLU solve failed.";
    return result;
  }

  result.nodeVoltages.resize(n);
  result.branchCurrents.resize(m);

  for (std::size_t i = 0; i < n; ++i) {
    result.nodeVoltages[i] = x_[i];
  }

  for (std::size_t i = 0; i < m; ++i) {
    result.branchCurrents[i] = x_[n + i];
  }

  result.success = true;
  return result;
}

bool MnaSystemSparse::solveInto(double* nodeVoltages, double* branchCurrents) {
  if (!factorized_) {
    return false;
  }

  std::size_t n = netCount_;
  std::size_t m = vsources_.size();
  std::size_t dim = n + m;

  x_ = b_;
  if (!kluSolve(x_.data(), dim)) {
    return false;
  }

  for (std::size_t i = 0; i < n; ++i) {
    nodeVoltages[i] = x_[i];
  }

  for (std::size_t i = 0; i < m; ++i) {
    branchCurrents[i] = x_[n + i];
  }

  return true;
}

/* ----------------------------- Cached Solve ----------------------------- */

void MnaSystemSparse::computeMatvec(const double* x, double* ax, std::size_t dim) const {
  std::size_t n = netCount_;
  std::size_t m = vsources_.size();

  std::memset(ax, 0, dim * sizeof(double));

  for (const auto& t : triplets_) {
    ax[t.row] += t.value * x[t.col];
  }

  for (std::size_t i = 0; i < m; ++i) {
    const auto& vs = vsources_[i];
    std::size_t vsRow = n + i;
    ax[vs.pos] += x[vsRow];
    ax[vs.neg] -= x[vsRow];
    ax[vsRow] += x[vs.pos];
    ax[vsRow] -= x[vs.neg];
  }

  if (n > 0) {
    ax[0] = x[0];
  }
}

bool MnaSystemSparse::solveCached(const double* rhs, double* dx, std::size_t dim) {
  if (!factorized_) {
    return false;
  }

  std::memcpy(dx, rhs, dim * sizeof(double));
  return kluSolve(dx, dim);
}

/* ----------------------------- Clear ----------------------------- */

void MnaSystemSparse::clear() {
  triplets_.clear();
  std::fill(I_.begin(), I_.end(), 0.0);
  vsources_.clear();
  factorized_ = false;
}

void MnaSystemSparse::clearStamps() {
  triplets_.clear();
  std::fill(I_.begin(), I_.end(), 0.0);
  vsources_.clear();
  // NOTE: factorized_ is NOT reset - cached LU factors remain valid
}

} // namespace sim::electronics::mna
