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

void MnaSystemSparse::buildCsc() {
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
