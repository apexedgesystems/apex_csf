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
 * 1. Stamp conductances and sources (builds the triplet list)
 * 2. Call factorize(), then solve()
 * 3. Clear, stamp again and call factorize() whenever the matrix changed. It
 *    keeps the symbolic analysis while the pattern is the analysed one and
 *    the pivot order while it suits the values, so the caller does not track
 *    what changed.
 * 4. When tripletsMatch() says the matrix is the factorized one, updateRhs()
 *    replaces factorize() and only the right-hand side is rebuilt.
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
   *
   * Adds i to the injected-current vector at a and subtracts it at b: an
   * independent source that delivers i into net a and draws it from net b.
   * A device whose linearization carries i from net a to net b through
   * itself stamps addCurrent(a, b, -i).
   *
   * @param a Net the current is injected into.
   * @param b Net the current is drawn from.
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
   * What an earlier call left is reused only where it still applies:
   * - the symbolic analysis, while the pattern (the positions stamped and the
   *   voltage sources) is the analysed one; otherwise the pattern is analysed
   *   again;
   * - the pivot order of the numeric factors, under the same pattern, unless
   *   a pivot is zero, KLU's reciprocal pivot growth of the result cannot be
   *   measured, or it is below the square root of a double's epsilon; then
   *   the matrix is pivoted anew under the same analysis. The growth is the
   *   smallest ratio, over the columns of each diagonal block of the
   *   row-scaled matrix, of the block's largest entry in a column to U's
   *   largest in it. The threshold is a heuristic against growth in U: it
   *   certifies neither a condition number nor the error of a solution.
   *
   * Also sizes the storage a solve copies the right-hand side into, so that
   * solveInto() on the factors allocates nothing. solve() still allocates
   * the vectors it returns.
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
   * first, and again after any change to the matrix.
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
   * solve() fails until the next factorize() or updateRhs(). The analysis and
   * factors in hand are kept for factorize() to reuse where they still apply.
   *
   * @note RT-safe: clears triplets and RHS without deallocation.
   */
  void clear();

  /**
   * @brief Clear stamps but preserve cached LU factorization.
   *
   * Resets triplets, current injections, and voltage sources for re-stamping,
   * and leaves isFactorized() as it is. Use this in the cached sparse LU path,
   * where the stamps are rebuilt for computeMatvec() while solveCached() goes
   * on using the factors in hand.
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
   * Counts stamped entries, not distinct positions. A different count means
   * a different pattern; an equal count does not mean the same one.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t nnz() const noexcept { return triplets_.size(); }

  /**
   * @brief Check if matrix has been factorized.
   * @note RT-safe.
   */
  [[nodiscard]] bool isFactorized() const noexcept { return factorized_; }

  /**
   * @brief Check if a symbolic analysis is held.
   *
   * True once factorize() has analysed a pattern. The analysis is that of
   * the pattern last factorized, which need not be the current stamps'.
   * @note RT-safe.
   */
  [[nodiscard]] bool isPatternAnalyzed() const noexcept { return kluSymbolic_ != nullptr; }

  /* ----------------------------- Advanced Solve ----------------------------- */

  /**
   * @brief Check whether the current stamps are the ones the factors were computed from.
   *
   * When they are (same entries and values, voltage sources on the same
   * nets), the matrix is unchanged and the factors can be reused: only the
   * RHS vector needs rebuilding (via updateRhs). Injected currents and the
   * values of voltage sources are free to differ, being right-hand side
   * only. The stamps of every successful factorize() are known: the list is
   * set aside when it is cleared, so nothing is copied to keep it.
   *
   * @return true if the factors in hand belong to the current stamps.
   * @note RT-safe: compares, no allocation.
   */
  [[nodiscard]] bool tripletsMatch() const;

  /**
   * @brief Rebuild only the RHS vector, reusing cached LU factorization.
   *
   * Valid when the matrix has not changed since the last factorize(). Only
   * rebuilds b_ from current I_ (current injections) and vsources_ (voltage
   * source values).
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
   * Uses the LU factors from the most recent factorize() to solve
   * A*dx = rhs. The caller provides the RHS (typically a residual
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
  bool stampsAreFactors_ = false; ///< triplets_ is the list the factors were computed from.
  std::size_t factoredCount_ = 0; ///< Its length at the factorization.
  std::vector<CooEntry> factoredTriplets_; ///< That list, set aside when the stamps were cleared.
  bool factoredTripletsHeld_ = false; ///< factoredTriplets_ are the stamps of the current factors.
  bool analysisCurrent_ = false;      ///< The symbolic analysis describes the current CSC pattern.

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
  bool analyze();        ///< Drop analysis and factors, then analyse the current pattern.
  bool factorNumeric();  ///< Full numeric factorization under the current analysis.
  bool keptOrderSuits(); ///< Whether the refactorization just made grew its pivots acceptably.
  void markFactorized(); ///< The current stamps are the factors'.
  void setStampsAside(); ///< Clear the stamps, keeping the factors' list for comparison.
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
