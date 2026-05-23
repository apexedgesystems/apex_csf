#ifndef APEX_MNASYSTEMSPARSE_HPP
#define APEX_MNASYSTEMSPARSE_HPP
/**
 * @file MnaSystemSparse.hpp
 * @brief Sparse MNA solver using KLU (SuiteSparse).
 *
 * For circuit matrices with low fill ratio (< 5%), sparse LU gives 10-50x
 * speedup over dense LAPACK. The 150-net Apex4Grid has ~2% fill (450 non-zeros
 * in 22,500 entries).
 *
 * KLU is the SPICE industry-standard sparse LU solver, optimized for circuit
 * matrices (near-diagonal, symmetric pattern, low fill-in).
 * COO-to-CSC conversion is done directly (counting sort, no intermediate).
 *
 * Usage pattern:
 * 1. Stamp conductances (builds triplet list)
 * 2. Call factorize() (one-time symbolic + numeric cost)
 * 3. Call solve() repeatedly with new RHS (O(nnz) per solve)
 * 4. Re-factorize only when circuit topology changes
 */

#include "src/sim/electronics/algorithms/mna/inc/StampContext.hpp"
#include "src/sim/electronics/algorithms/mna/inc/Types.hpp"

extern "C" {
#include <klu.h>
}

#include <cstddef>
#include <string>
#include <vector>

namespace sim::electronics::algorithms::mna {

/**
 * @brief Result from sparse MNA solve operation.
 */
struct MnaResultSparse {
  std::vector<double> nodeVoltages;   ///< Voltage at each net.
  std::vector<double> branchCurrents; ///< Current through each voltage source.
  bool success = false;               ///< True if solved without error.
  std::string errorMessage;           ///< Error description if failed.
};

/**
 * @brief Plain COO entry for sparse matrix stamping.
 *
 * Direct member access is ~5% faster in profiled sparse path
 * (computeMatvec, tripletsMatch, buildCsc).
 * COO-to-CSC is done directly via counting sort + merge.
 */
struct CooEntry {
  int row;
  int col;
  double value;
};

/**
 * @brief Sparse MNA solver using KLU (SuiteSparse).
 *
 * Maintains conductance matrix in COO (triplet) format during stamping,
 * converts to CSC (compressed sparse column) for factorization and solve.
 *
 * For circuits with predictable sparsity patterns (2-5% filled), this gives
 * 10-50x speedup over dense LAPACK by only processing non-zero entries.
 */
class MnaSystemSparse {
public:
  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct sparse MNA system.
   * @param netCount Total number of nets (including ground at index 0).
   * @note NOT RT-safe: allocates triplet and RHS storage.
   */
  explicit MnaSystemSparse(std::size_t netCount);

  ~MnaSystemSparse();

  MnaSystemSparse(const MnaSystemSparse&) = delete;
  MnaSystemSparse& operator=(const MnaSystemSparse&) = delete;
  MnaSystemSparse(MnaSystemSparse&&) = delete;
  MnaSystemSparse& operator=(MnaSystemSparse&&) = delete;

  /* ----------------------------- Stamping API ----------------------------- */

  /**
   * @brief Add a conductance between two nets.
   * @param a First net ID.
   * @param b Second net ID.
   * @param g Conductance value (1/R).
   *
   * @note RT-safe: appends to triplet list (pre-allocated with reserve).
   */
  void addConductance(NetID a, NetID b, double g);

  /**
   * @brief Inject a current between two nets.
   * @param a Source net (current flows out).
   * @param b Sink net (current flows in).
   * @param i Current in amperes.
   *
   * @note RT-safe: stamps into pre-allocated vector.
   */
  void addCurrent(NetID a, NetID b, double i);

  /**
   * @brief Stamp a single matrix entry G[row][col] += value.
   *
   * Unlike addConductance (which enforces symmetric stamps for resistors),
   * this stamps a single asymmetric entry. Required for VCCS elements like
   * MOSFET transconductance gm, where gate controls drain current but drain
   * does not control gate current.
   *
   * @param row Row index (net receiving current).
   * @param col Column index (controlling net voltage).
   * @param value Conductance value to add.
   *
   * @note RT-safe: appends to triplet list.
   */
  void addMatrixEntry(NetID row, NetID col, double value);

  /**
   * @brief Add an ideal voltage source between two nets.
   * @param pos Positive terminal net.
   * @param neg Negative terminal net.
   * @param v Voltage (Vpos - Vneg).
   * @return Index of this voltage source (for current lookup after solve).
   *
   * @note NOT RT-safe: may reallocate voltage source vector.
   */
  std::size_t addVoltageSource(NetID pos, NetID neg, double v);

  /* ----------------------------- Solve ----------------------------- */

  /**
   * @brief Build sparse matrix from stamped triplets and factorize.
   *
   * Converts triplet list (COO) to CSC format, applies ground constraint,
   * augments with voltage sources, and computes LU factorization.
   *
   * @return true on success, false if matrix is singular.
   *
   * @note NOT RT-safe: allocates and factorizes.
   */
  bool factorize();

  /**
   * @brief Solve with pre-factorized matrix (fast, O(nnz)).
   *
   * Uses cached LU factorization to solve for new RHS. Must call factorize()
   * first or after topology change.
   *
   * @return MnaResultSparse with voltages, currents, and status.
   *
   * @note NOT RT-safe: allocates result vectors.
   */
  MnaResultSparse solve();

  /**
   * @brief RT-safe solve into pre-allocated buffers.
   *
   * Writes results directly into provided vectors without allocation.
   *
   * @param nodeVoltages Output vector for node voltages (must be sized >= netCount).
   * @param branchCurrents Output vector for branch currents (must be sized >= vsrcCount).
   * @return true on success, false on solve failure.
   *
   * @note RT-safe if buffers are pre-allocated and factorization is cached.
   */
  bool solveInto(double* nodeVoltages, double* branchCurrents);

  /* ----------------------------- Mutation ----------------------------- */

  /**
   * @brief Clear stamps and prepare for new circuit.
   *
   * @note RT-safe: clears triplets and RHS without deallocation.
   */
  void clear();

  /**
   * @brief Clear stamps but preserve cached LU factorization.
   *
   * Resets triplets, current injections, and voltage sources for re-stamping,
   * but does NOT invalidate the factorized_ flag. Use this in the cached
   * sparse LU path where we want to re-stamp for matvec computation while
   * keeping the old factorization for the back-solve.
   *
   * @note RT-safe: clears without deallocation.
   */
  void clearStamps();

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get number of nets.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t netCount() const noexcept { return netCount_; }

  /**
   * @brief Get number of voltage sources.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t voltageSourceCount() const noexcept { return vsources_.size(); }

  /**
   * @brief Get number of non-zero entries in conductance matrix.
   *
   * Useful for topology change detection (nnz changes when topology changes).
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t nnz() const noexcept { return triplets_.size(); }

  /**
   * @brief Check if matrix has been factorized.
   * @note RT-safe.
   */
  [[nodiscard]] bool isFactorized() const noexcept { return factorized_; }

  /**
   * @brief Check if symbolic pattern analysis has been performed.
   *
   * After the first factorize(), the symbolic elimination tree is cached.
   * Subsequent calls with the same sparsity pattern can use refactorize()
   * for numeric-only factorization (skips symbolic ordering).
   * @note RT-safe.
   */
  [[nodiscard]] bool isPatternAnalyzed() const noexcept { return patternAnalyzed_; }

  /* ----------------------------- Advanced Solve ----------------------------- */

  /**
   * @brief Rebuild matrix from stamps and perform numeric-only factorization.
   *
   * Reuses the cached symbolic analysis from a previous factorize() call.
   * Valid only when the sparsity pattern has not changed (same triplet positions).
   * The numerical values in the matrix and RHS are rebuilt from current stamps.
   *
   * @return true on success, false if factorization fails.
   * @pre isPatternAnalyzed() must be true.
   *
   * @note NOT RT-safe.
   */
  bool refactorize();

  /**
   * @brief Update CSC matrix values in-place and perform numeric-only factorization.
   *
   * Instead of rebuilding the CSC matrix from triplets (copy + sort + merge),
   * this method zeros the existing CSC value array and accumulates new values
   * directly using coeffRef(). Avoids setFromTriplets overhead.
   *
   * @return true on success, false if factorization fails or CSC structure is invalid.
   * @pre isPatternAnalyzed() must be true (CSC structure established by prior factorize).
   *
   * @note NOT RT-safe.
   */
  bool refactorizeInPlace();

  /**
   * @brief Check if current triplets match cached copy from last factorize.
   *
   * When stamps produce identical triplets (same positions and values),
   * the matrix A_ is unchanged and LU factors can be reused. Only the
   * RHS vector needs rebuilding (via updateRhs).
   *
   * @return true if current triplets exactly match the cached copy.
   */
  [[nodiscard]] bool tripletsMatch() const;

  /**
   * @brief Rebuild only the RHS vector, reusing cached LU factorization.
   *
   * Valid when the conductance matrix (triplets) has not changed since
   * the last factorize() or refactorize(). Only rebuilds b_ from current
   * I_ (current injections) and vsources_ (voltage source values).
   *
   * @pre tripletsMatch() must be true.
   *
   * @note RT-safe: no allocation, no factorization.
   */
  void updateRhs();

  /**
   * @brief Compute A*x directly from triplets (sparse matvec).
   *
   * Computes the matrix-vector product A*x without building the CSC matrix.
   * Iterates over the triplet list (O(nnz)) and applies voltage source
   * augmentation and ground constraint. Used for iterative refinement with
   * a cached sparse LU factorization.
   *
   * @param x Input vector (dim = netCount + vsourceCount entries).
   * @param ax Output vector (dim entries, will be zeroed and filled).
   * @param dim Augmented dimension (netCount + vsourceCount).
   *
   * @note RT-safe: no allocation if ax is pre-sized.
   */
  void computeMatvec(const double* x, double* ax, std::size_t dim) const;

  /**
   * @brief Solve with cached LU factorization (no re-factorization).
   *
   * Uses the LU factors from the most recent factorize() or refactorize()
   * to solve A*dx = rhs. The caller provides the RHS (typically a residual
   * vector from iterative refinement).
   *
   * @param rhs Right-hand side vector (dim entries).
   * @param dx Output solution vector (dim entries).
   * @param dim Augmented dimension.
   * @return true on success, false if no factorization cached.
   *
   * @pre isFactorized() must be true.
   * @note RT-safe if buffers are pre-allocated and factorization is cached.
   */
  bool solveCached(const double* rhs, double* dx, std::size_t dim);

  /**
   * @brief Get augmented dimension (netCount + vsourceCount).
   */
  [[nodiscard]] std::size_t augmentedDim() const noexcept { return netCount_ + vsources_.size(); }

  /**
   * @brief Access current injection vector.
   * @return Const reference to internal I_ vector.
   */
  [[nodiscard]] const std::vector<double>& currentVector() const noexcept { return I_; }

  /**
   * @brief Access voltage source descriptors.
   * @return Const reference to internal voltage source list.
   */
  [[nodiscard]] const std::vector<StampContext::VoltageSource>& voltageSources() const noexcept {
    return vsources_;
  }

private:
  std::size_t netCount_;
  std::vector<CooEntry> triplets_; ///< COO format for stamping (POD for hot-loop performance).
  std::vector<double> I_;          ///< Current injection RHS vector.
  std::vector<StampContext::VoltageSource> vsources_;
  std::vector<double> b_; ///< RHS vector for solver.
  std::vector<double> x_; ///< Solution vector.
  bool factorized_ = false;
  bool patternAnalyzed_ = false;
  std::vector<CooEntry> cachedTriplets_; ///< Triplets from last factorize/refactorize.
  std::size_t cachedVsourceCount_ = 0;   ///< Voltage source count from last factorize.

  klu_symbolic* kluSymbolic_ = nullptr; ///< KLU symbolic analysis (elimination tree).
  klu_numeric* kluNumeric_ = nullptr;   ///< KLU numeric factorization (LU factors).
  klu_common kluCommon_;                ///< KLU configuration and statistics.
  std::vector<CooEntry> augCoo_;        ///< Reusable augmented COO workspace.
  std::vector<int> cscAp_;              ///< CSC column pointers.
  std::vector<int> cscAi_;              ///< CSC row indices.
  std::vector<double> cscAx_;           ///< CSC values.
  std::vector<int> colWork_;            ///< Reusable workspace for counting sort.

  // Cached CSC pattern: once buildCsc has established cscAp_/cscAi_ for a
  // given triplet+vsource structure, subsequent calls with the same (row,col)
  // sequence skip the sort/merge and only rescatter values. The mapping from
  // triplet/vsource origin -> cscAx_ slot is recorded on the slow path.
  std::vector<int> tripletToCscIdx_;       ///< Per-triplet slot in cscAx_ (-1 if filtered).
  std::vector<int> vsourceCscIdx_;         ///< 4 slots per vsource: [pos-row, vsrow-pos, neg-row,
                                           ///< vsrow-neg] (-1 if ground).
  int groundCscIdx_ = -1;                  ///< Slot of the {0,0,1.0} ground diagonal entry.
  bool cscPatternCached_ = false;          ///< True iff the cached pattern is valid.
  std::vector<std::int64_t> cachedRowCol_; ///< Packed (row<<32 | col) per triplet for SIMD match.
  std::vector<std::int64_t> cachedVsourceRc_; ///< Packed (pos<<32 | neg) per vsource for match.

  void freeKluFactors();
  bool kluSolve(double* rhsSolution, std::size_t dim);
  void buildCsc();
  bool patternMatchesCached() const noexcept;
  void buildCscFromCachedPattern();
  void recordCscPattern();

  /**
   * @brief Build RHS vector from current injections and voltage sources.
   */
  void buildRhsVector();
};

} // namespace sim::electronics::algorithms::mna

#endif // APEX_MNASYSTEMSPARSE_HPP
