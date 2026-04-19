#ifndef APEX_SIM_ELECTRONICS_MNA_MNASYSTEM_HPP
#define APEX_SIM_ELECTRONICS_MNA_MNASYSTEM_HPP
/**
 * @file MnaSystem.hpp
 * @brief Modified Nodal Analysis solver for linear circuits.
 *
 * Solves G*V = I augmented with ideal voltage sources.
 * Returns both node voltages and branch currents through voltage sources.
 *
 * Uses LAPACKE_dgesv for accelerated LU decomposition when available,
 * falling back to naive Gaussian elimination otherwise.
 */

#include "src/sim/electronics/algorithms/mna/inc/StampContext.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_blas.hpp"

#if COMPAT_HAVE_LAPACKE
#include <lapack.h>
#endif

#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace sim::electronics::mna {

/**
 * @brief Result from MNA solve operation.
 */
struct MnaResult {
  std::vector<double> nodeVoltages;   ///< Voltage at each net.
  std::vector<double> branchCurrents; ///< Current through each voltage source.
  bool success = false;               ///< True if solved without error.
  std::string errorMessage;           ///< Error description if failed.
};

/**
 * @brief Pre-allocated workspace for RT-safe MNA solving.
 *
 * Allocate once at setup time, then reuse for multiple solves without
 * allocation in the hot path.
 */
struct MnaSolveWorkspace {
  std::vector<double> A;  ///< Flattened matrix (dim x dim).
  std::vector<double> b;  ///< RHS vector (dim).
  std::vector<int> ipiv;  ///< Pivot indices for LAPACK.
  std::size_t maxDim = 0; ///< Maximum dimension this workspace supports.

  /**
   * @brief Prepare workspace for a given maximum dimension.
   * @param dim Maximum matrix dimension (netCount + voltageSourceCount).
   * @note NOT RT-safe: allocates memory.
   */
  void prepare(std::size_t dim) {
    if (dim > maxDim) {
      A.resize(dim * dim);
      b.resize(dim);
      ipiv.resize(dim);
      maxDim = dim;
    }
  }

  /**
   * @brief Check if workspace can handle given dimension.
   * @note RT-safe.
   */
  bool canHandle(std::size_t dim) const noexcept { return dim <= maxDim; }
};

/**
 * @brief Cached LU factorization for repeated solves.
 *
 * When the circuit topology doesn't change (same matrix structure),
 * cache the LU factorization and only do back-substitution per solve.
 * This reduces per-solve cost from O(n^3) to O(n^2).
 *
 * Usage:
 * 1. Build circuit, call factorize() once
 * 2. Call solveFactorized() repeatedly (RT-safe, O(n^2))
 * 3. If topology changes, call factorize() again
 */
struct MnaFactorizedWorkspace {
  std::vector<double> LU;  ///< Factorized matrix (dim x dim).
  std::vector<double> b;   ///< Working RHS vector.
  std::vector<int> ipiv;   ///< Pivot indices from factorization.
  std::size_t dim = 0;     ///< Current dimension.
  std::size_t maxDim = 0;  ///< Maximum supported dimension.
  bool factorized = false; ///< True if LU is valid.

  /**
   * @brief Prepare workspace for a given maximum dimension.
   * @param maxSize Maximum matrix dimension.
   * @note NOT RT-safe: allocates memory.
   */
  void prepare(std::size_t maxSize) {
    if (maxSize > maxDim) {
      LU.resize(maxSize * maxSize);
      b.resize(maxSize);
      ipiv.resize(maxSize);
      maxDim = maxSize;
    }
    factorized = false;
  }

  /**
   * @brief Check if factorization is cached and valid.
   * @note RT-safe.
   */
  bool isFactorized() const noexcept { return factorized; }

  /**
   * @brief Invalidate cached factorization.
   * @note RT-safe.
   */
  void invalidate() noexcept { factorized = false; }
};

/**
 * @brief Nodal analysis engine: stamps elements and solves G*V = I.
 *
 * Supports conductances, current sources, and ideal voltage sources.
 * Voltage sources are handled via matrix augmentation (MNA technique).
 */
class MnaSystem {
public:
  using Matrix = StampContext::Matrix;
  using Vector = StampContext::Vector;

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @param netCount Total number of nets (including ground at index 0).
   * @note NOT RT-safe: allocates conductance matrix and current vector.
   */
  explicit MnaSystem(std::size_t netCount) : ctx_(netCount) {}

  /**
   * @brief Construct with external column-major LAPACK workspace.
   *
   * Conductance stamps go directly into the pre-zeroed external matrix
   * (column-major with leading dimension augDim), avoiding the intermediate
   * G_ allocation and the G_ to A copy in the solve path.
   *
   * @param netCount Total number of nets (including ground at index 0).
   * @param externalA Pre-zeroed column-major matrix (augDim x augDim doubles).
   * @param augDim Leading dimension of external matrix.
   * @note externalA must remain valid for the lifetime of this MnaSystem.
   */
  MnaSystem(std::size_t netCount, double* externalA, std::size_t augDim)
      : ctx_(netCount, externalA, augDim) {}

  /**
   * @brief Construct in matvec mode for sparse A*x computation.
   *
   * Stamp callbacks compute A*x contributions directly instead of building
   * the conductance matrix. Used for iterative refinement in cached LU path.
   *
   * @param netCount Total number of nets (including ground at index 0).
   * @param x Input vector (pre-assembled [voltages | branch currents]).
   * @param ax Output accumulator for A*x (must be pre-zeroed).
   * @note x and ax must remain valid for the lifetime of this MnaSystem.
   */
  MnaSystem(std::size_t netCount, const double* x, double* ax) : ctx_(netCount, x, ax) {}

  /* ----------------------------- Stamping API ----------------------------- */

  /**
   * @brief Add a conductance between two nets.
   * @param a First net ID.
   * @param b Second net ID.
   * @param g Conductance value (1/R).
   *
   * @note RT-safe: stamps into pre-allocated matrix.
   */
  void addConductance(NetID a, NetID b, double g) { ctx_.stampConductance(a, b, g); }

  /**
   * @brief Stamp a single asymmetric G matrix entry: G[row][col] += value.
   *
   * For VCCS elements (e.g., MOSFET transconductance gm) where the
   * current at one terminal depends on voltage at another without
   * the symmetric reciprocal dependency of a resistor.
   *
   * @param row Row net (current equation affected).
   * @param col Column net (voltage dependency).
   * @param value Conductance increment.
   * @note RT-safe: stamps into pre-allocated matrix.
   */
  void addGEntry(NetID row, NetID col, double value) { ctx_.stampGEntry(row, col, value); }

  /**
   * @brief Inject a current between two nets.
   * @param a Source net (current flows out).
   * @param b Sink net (current flows in).
   * @param i Current in amperes.
   *
   * @note RT-safe: stamps into pre-allocated vector.
   */
  void addCurrent(NetID a, NetID b, double i) { ctx_.stampCurrent(a, b, i); }

  /**
   * @brief Add an ideal voltage source between two nets.
   * @param pos Positive terminal net.
   * @param neg Negative terminal net.
   * @param v Voltage (Vpos - Vneg).
   * @return Index of this voltage source (for current lookup after solve).
   *
   * @note NOT RT-safe: may reallocate voltage source vector.
   */
  std::size_t addVoltageSource(NetID pos, NetID neg, double v) {
    ctx_.stampVoltageSource(pos, neg, v);
    return ctx_.voltageSources().size() - 1;
  }

  /* ----------------------------- Solve ----------------------------- */

  /**
   * @brief Solve for node voltages and branch currents.
   *
   * Uses LAPACKE_dgesv (LU factorization) when available for 5-10x speedup,
   * falling back to Gaussian elimination with partial pivoting otherwise.
   *
   * @return MnaResult containing voltages, currents, and success status.
   *
   * @note NOT RT-safe: allocates matrix and result vectors.
   */
  MnaResult solve() const {
    MnaResult result;

    const auto& baseG = ctx_.g();
    const auto& baseI = ctx_.i();
    std::size_t n = ctx_.netCount();
    const auto& vsrc = ctx_.voltageSources();
    std::size_t m = vsrc.size();
    std::size_t dim = n + m;

    if (dim == 0) {
      result.success = true;
      return result;
    }

#if COMPAT_HAVE_LAPACKE
    // LAPACK-accelerated path: use dgesv for LU solve
    return solveLapack(baseG, baseI, vsrc, n, m, dim);
#else
    // Fallback: naive Gaussian elimination
    return solveNaive(baseG, baseI, vsrc, n, m, dim);
#endif
  }

  /**
   * @brief RT-safe solve using pre-allocated workspace.
   *
   * Writes results directly into provided vectors without allocation.
   * Workspace must be prepared with sufficient capacity before calling.
   *
   * @param ws Pre-allocated workspace (call ws.prepare() at setup time).
   * @param nodeVoltages Output vector for node voltages (must be sized >= netCount).
   * @param branchCurrents Output vector for branch currents (must be sized >= vsrcCount).
   * @return true on success, false on singular matrix.
   *
   * @note RT-safe if workspace and output vectors are pre-allocated.
   */
  bool solveInto(MnaSolveWorkspace& ws, double* nodeVoltages,
                 double* branchCurrents) const noexcept {
    std::size_t n = ctx_.netCount();
    const auto& vsrc = ctx_.voltageSources();
    std::size_t m = vsrc.size();
    std::size_t dim = n + m;

    if (dim == 0) {
      return true;
    }

    if (!ws.canHandle(dim)) {
      return false;
    }

#if COMPAT_HAVE_LAPACKE
    if (ctx_.hasExternalMatrix()) {
      return solveIntoLapackDirect(ws, ctx_.i(), vsrc, n, m, dim, nodeVoltages, branchCurrents);
    }
    return solveIntoLapack(ws, ctx_.g(), ctx_.i(), vsrc, n, m, dim, nodeVoltages, branchCurrents);
#else
    return solveIntoNaive(ws, ctx_.g(), ctx_.i(), vsrc, n, m, dim, nodeVoltages, branchCurrents);
#endif
  }

  /* ----------------------------- Mutation ----------------------------- */

  /**
   * @brief Clear stamps for reuse.
   *
   * Call this before re-stamping a circuit for a new simulation.
   *
   * @note RT-safe: no allocation.
   */
  void clear() noexcept { ctx_.clear(); }

  /**
   * @brief Clear only current injections for RHS rebuild.
   *
   * Use this for cached LU optimization: keep conductance matrix intact,
   * only reset current vector before re-stamping current sources.
   *
   * @note RT-safe: no allocation.
   */
  void clearCurrents() noexcept { ctx_.clearCurrents(); }

  /**
   * @brief Clear currents and voltage sources for RHS rebuild.
   *
   * Use this for cached LU optimization when voltage source VALUES
   * may change but their positions in the circuit stay the same.
   *
   * @note RT-safe: no allocation.
   */
  void clearRHS() noexcept { ctx_.clearRHS(); }

  /* ----------------------- Augmented Matrix Build ----------------------- */

  /**
   * @brief Build the augmented MNA matrix for external solvers.
   *
   * Fills flat row-major matrix A (dim x dim) and RHS vector b (dim)
   * from the current stamps. Applies ground constraint (V[0]=0).
   * Caller must pre-zero A and b before calling.
   *
   * @param A Output matrix (dim * dim doubles, must be zeroed).
   * @param b Output RHS vector (dim doubles, must be zeroed).
   * @note RT-safe if output arrays are pre-allocated.
   */
  void buildAugmentedMatrix(double* __restrict__ A, double* __restrict__ b) const noexcept {
    const double* __restrict__ gData = ctx_.g().data();
    const double* __restrict__ iData = ctx_.i().data();
    std::size_t n = ctx_.netCount();
    const auto& vsrc = ctx_.voltageSources();
    std::size_t m = vsrc.size();
    std::size_t dim = n + m;

    for (std::size_t r = 0; r < n; ++r) {
      std::memcpy(A + r * dim, gData + r * n, n * sizeof(double));
      b[r] = iData[r];
    }

    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;
      A[vs.pos * dim + srcCol] += 1.0;
      A[vs.neg * dim + srcCol] -= 1.0;
      A[srcRow * dim + vs.pos] = 1.0;
      A[srcRow * dim + vs.neg] = -1.0;
      b[srcRow] = vs.v;
    }

    if (COMPAT_LIKELY(n > 0)) {
      std::memset(A, 0, dim * sizeof(double));
      A[0] = 1.0;
      b[0] = 0.0;
    }
  }

  /**
   * @brief Get augmented matrix dimension (nets + voltage sources).
   * @note RT-safe.
   */
  std::size_t augmentedDim() const noexcept {
    return ctx_.netCount() + ctx_.voltageSources().size();
  }

  /* ----------------------- Cached LU Solve API ----------------------- */

  /**
   * @brief Factorize the current circuit matrix (one-time cost).
   *
   * Performs LU decomposition and caches the result in the workspace.
   * After factorization, use solveFactorized() for O(n^2) back-substitution.
   *
   * @param ws Workspace to store factorization (must be prepared).
   * @return true on success, false on singular matrix or workspace too small.
   *
   * @note NOT RT-safe: performs O(n^3) LU factorization.
   */
  bool factorize(MnaFactorizedWorkspace& ws) const noexcept {
    const auto& baseG = ctx_.g();
    const auto& baseI = ctx_.i();
    std::size_t n = ctx_.netCount();
    const auto& vsrc = ctx_.voltageSources();
    std::size_t m = vsrc.size();
    std::size_t dim = n + m;

    if (dim == 0 || dim > ws.maxDim) {
      return false;
    }

    ws.dim = dim;
    ws.factorized = false;

#if COMPAT_HAVE_LAPACKE
    return factorizeLapack(ws, baseG, baseI, vsrc, n, m, dim);
#else
    // Naive fallback doesn't support caching well - just mark as not factorized
    return false;
#endif
  }

  /**
   * @brief Solve using cached LU factorization (RT-safe, O(n^2)).
   *
   * Uses the cached LU factors from factorize() to solve with new RHS.
   * Much faster than full solve() when circuit topology is unchanged.
   *
   * @param ws Workspace with valid factorization.
   * @param nodeVoltages Output array for node voltages.
   * @param branchCurrents Output array for branch currents.
   * @return true on success, false if not factorized.
   *
   * @note RT-safe: O(n^2) back-substitution only.
   */
  bool solveFactorized(MnaFactorizedWorkspace& ws, double* nodeVoltages,
                       double* branchCurrents) const noexcept {
    if (!ws.factorized) {
      return false;
    }

    const auto& baseI = ctx_.i();
    const auto& vsrc = ctx_.voltageSources();
    std::size_t n = ctx_.netCount();
    std::size_t m = vsrc.size();
    std::size_t dim = ws.dim;

#if COMPAT_HAVE_LAPACKE
    return solveFactorizedLapack(ws, baseI, vsrc, n, m, dim, nodeVoltages, branchCurrents);
#else
    (void)nodeVoltages;
    (void)branchCurrents;
    return false;
#endif
  }

private:
  using VoltageSource = StampContext::VoltageSource;

  /* ----------------------- Allocating Solve Methods ----------------------- */

  /**
   * @brief LAPACK-accelerated solve using dgesv (column-major, direct LAPACK).
   */
#if COMPAT_HAVE_LAPACKE
  MnaResult solveLapack(const Matrix& baseG, const Vector& baseI,
                        const std::vector<VoltageSource>& vsrc, std::size_t n, std::size_t m,
                        std::size_t dim) const {
    MnaResult result;

    // Allocate column-major matrix A and vector b
    std::vector<double> A(dim * dim, 0.0);
    std::vector<double> b(dim, 0.0);
    std::vector<lapack_int> ipiv(dim);

    const double* __restrict__ gData = baseG.data();
    const double* __restrict__ iData = baseI.data();

    // Build column-major A: A_col[col * dim + row] = G[row * n + col]
    for (std::size_t r = 0; r < n; ++r) {
      const double* __restrict__ gRow = gData + r * n;
      for (std::size_t c = 0; c < n; ++c) {
        A[c * dim + r] = gRow[c];
      }
      b[r] = iData[r];
    }

    // Stamp voltage source contributions (column-major)
    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      A[srcCol * dim + vs.pos] += 1.0;
      A[srcCol * dim + vs.neg] -= 1.0;
      A[vs.pos * dim + srcRow] = 1.0;
      A[vs.neg * dim + srcRow] = -1.0;
      b[srcRow] = vs.v;
    }

    // Ground constraint (zero row 0 in column-major)
    if (n > 0) {
      for (std::size_t c = 0; c < dim; ++c) {
        A[c * dim] = 0.0;
      }
      A[0] = 1.0;
      b[0] = 0.0;
    }

    // Solve using LAPACK directly (column-major, no LAPACKE overhead)
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    LAPACK_dgesv(&ldim, &nrhs, A.data(), &ldim, ipiv.data(), b.data(), &ldim, &info);

    if (info != 0) {
      result.success = false;
      if (info > 0) {
        result.errorMessage = "Singular matrix at row " + std::to_string(info);
      } else {
        result.errorMessage = "LAPACK error: illegal argument " + std::to_string(-info);
      }
      return result;
    }

    // Extract results
    result.nodeVoltages.assign(b.data(), b.data() + n);
    result.branchCurrents.assign(b.data() + n, b.data() + n + m);
    result.success = true;
    return result;
  }
#endif

  /**
   * @brief Naive Gaussian elimination fallback.
   */
  MnaResult solveNaive(const Matrix& baseG, const Vector& baseI,
                       const std::vector<VoltageSource>& vsrc, std::size_t n, std::size_t m,
                       std::size_t dim) const {
    MnaResult result;

    // Build augmented matrix [A | b]
    std::vector<std::vector<double>> aug(dim, std::vector<double>(dim + 1, 0.0));

    // Stamp G matrix and I vector
    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        aug[r][c] = baseG[r * n + c];
      }
      aug[r][dim] = baseI[r];
    }

    // Stamp voltage source contributions
    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      aug[vs.pos][srcCol] += 1.0;
      aug[vs.neg][srcCol] -= 1.0;

      aug[srcRow][vs.pos] = 1.0;
      aug[srcRow][vs.neg] = -1.0;
      aug[srcRow][dim] = vs.v;
    }

    // Ground constraint
    if (n > 0) {
      for (std::size_t c = 0; c <= dim; ++c) {
        aug[0][c] = 0.0;
      }
      aug[0][0] = 1.0;
    }

    // Gaussian elimination with partial pivoting
    for (std::size_t pivot = 0; pivot < dim; ++pivot) {
      std::size_t bestRow = pivot;
      double bestVal = std::fabs(aug[pivot][pivot]);
      for (std::size_t r = pivot + 1; r < dim; ++r) {
        double val = std::fabs(aug[r][pivot]);
        if (val > bestVal) {
          bestVal = val;
          bestRow = r;
        }
      }

      if (bestRow != pivot) {
        std::swap(aug[pivot], aug[bestRow]);
      }

      double pv = aug[pivot][pivot];
      if (std::fabs(pv) < 1e-15) {
        result.success = false;
        result.errorMessage = "Singular matrix at row " + std::to_string(pivot);
        return result;
      }

      for (std::size_t c = pivot; c <= dim; ++c) {
        aug[pivot][c] /= pv;
      }

      for (std::size_t r = 0; r < dim; ++r) {
        if (r == pivot)
          continue;
        double factor = aug[r][pivot];
        for (std::size_t c = pivot; c <= dim; ++c) {
          aug[r][c] -= factor * aug[pivot][c];
        }
      }
    }

    result.nodeVoltages.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      result.nodeVoltages[i] = aug[i][dim];
    }

    result.branchCurrents.resize(m);
    for (std::size_t i = 0; i < m; ++i) {
      result.branchCurrents[i] = aug[n + i][dim];
    }

    result.success = true;
    return result;
  }

  /* ----------------------- Cached LU Methods ----------------------- */

#if COMPAT_HAVE_LAPACKE
  /**
   * @brief Build and factorize the MNA matrix using dgetrf (column-major, direct LAPACK).
   */
  bool factorizeLapack(MnaFactorizedWorkspace& ws, const Matrix& baseG, const Vector& /*baseI*/,
                       const std::vector<VoltageSource>& vsrc, std::size_t n, std::size_t m,
                       std::size_t dim) const noexcept {

    double* __restrict__ LU = ws.LU.data();
    const double* __restrict__ gData = baseG.data();

    // Zero and build column-major matrix
    std::memset(LU, 0, dim * dim * sizeof(double));

    for (std::size_t r = 0; r < n; ++r) {
      const double* __restrict__ gRow = gData + r * n;
      for (std::size_t c = 0; c < n; ++c) {
        LU[c * dim + r] = gRow[c];
      }
    }

    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      LU[srcCol * dim + vs.pos] += 1.0;
      LU[srcCol * dim + vs.neg] -= 1.0;
      LU[vs.pos * dim + srcRow] = 1.0;
      LU[vs.neg * dim + srcRow] = -1.0;
    }

    // Ground constraint (zero row 0 in column-major)
    if (COMPAT_LIKELY(n > 0)) {
      for (std::size_t c = 0; c < dim; ++c) {
        LU[c * dim] = 0.0;
      }
      LU[0] = 1.0;
    }

    // LU factorization (O(n^3), column-major, direct LAPACK)
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int info = 0;
    LAPACK_dgetrf(&ldim, &ldim, LU, &ldim, ws.ipiv.data(), &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    ws.factorized = true;
    return true;
  }

  /**
   * @brief Solve using cached LU factors with dgetrs (O(n^2), column-major, direct LAPACK).
   */
  COMPAT_HOT bool solveFactorizedLapack(MnaFactorizedWorkspace& ws, const Vector& baseI,
                                        const std::vector<VoltageSource>& vsrc, std::size_t n,
                                        std::size_t m, std::size_t dim,
                                        double* __restrict__ nodeVoltages,
                                        double* __restrict__ branchCurrents) const noexcept {

    double* __restrict__ b = ws.b.data();
    const double* __restrict__ iData = baseI.data();

    // Build RHS vector
    std::memcpy(b, iData, n * sizeof(double));
    for (std::size_t k = 0; k < m; ++k) {
      b[n + k] = vsrc[k].v;
    }

    // Ground constraint
    if (COMPAT_LIKELY(n > 0)) {
      b[0] = 0.0;
    }

    // Back-substitution using cached LU (O(n^2), column-major, direct LAPACK)
    char trans = 'N';
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    LAPACK_dgetrs(&trans, &ldim, &nrhs, ws.LU.data(), &ldim, ws.ipiv.data(), b, &ldim, &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    // Extract results
    std::memcpy(nodeVoltages, b, n * sizeof(double));
    std::memcpy(branchCurrents, b + n, m * sizeof(double));

    return true;
  }
#endif

  /* ----------------------- RT-Safe Solve Methods ----------------------- */

  /**
   * @brief RT-safe LAPACK solve using pre-allocated workspace.
   *
   * Builds column-major augmented matrix and calls LAPACK directly
   * (bypasses LAPACKE row-major transpose and NaN check overhead).
   */
#if COMPAT_HAVE_LAPACKE
  COMPAT_HOT bool solveIntoLapack(MnaSolveWorkspace& ws, const Matrix& baseG, const Vector& baseI,
                                  const std::vector<VoltageSource>& vsrc, std::size_t n,
                                  std::size_t m, std::size_t dim, double* __restrict__ nodeVoltages,
                                  double* __restrict__ branchCurrents) const noexcept {

    double* __restrict__ A = ws.A.data();
    double* __restrict__ b = ws.b.data();
    const double* __restrict__ gData = baseG.data();
    const double* __restrict__ iData = baseI.data();

    // Zero the workspace (reuse without allocation)
    std::memset(A, 0, dim * dim * sizeof(double));

    // Build COLUMN-MAJOR augmented matrix: A_col[col * dim + row] = G_row[row * n + col]
    // MNA G matrix is symmetric (G[r][c] == G[c][r]), so row-major G read
    // can be written directly as column-major A without reorder for the G block.
    // We iterate in G_ row order for sequential reads.
    for (std::size_t r = 0; r < n; ++r) {
      const double* __restrict__ gRow = gData + r * n;
      for (std::size_t c = 0; c < n; ++c) {
        A[c * dim + r] = gRow[c];
      }
    }

    // Stamp voltage source contributions (column-major indexing)
    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      // KCL: current from voltage source (column srcCol, rows vs.pos/vs.neg)
      A[srcCol * dim + vs.pos] += 1.0;
      A[srcCol * dim + vs.neg] -= 1.0;

      // KVL: V_pos - V_neg = v (row srcRow = column srcRow in A^T)
      A[vs.pos * dim + srcRow] = 1.0;
      A[vs.neg * dim + srcRow] = -1.0;
    }

    // Ground constraint: row 0 -> zero row 0 in column-major = zero element [0] in each column
    if (COMPAT_LIKELY(n > 0)) {
      for (std::size_t c = 0; c < dim; ++c) {
        A[c * dim] = 0.0;
      }
      A[0] = 1.0;
    }

    // Build RHS vector
    std::memcpy(b, iData, n * sizeof(double));
    for (std::size_t k = 0; k < m; ++k) {
      b[n + k] = vsrc[k].v;
    }
    if (COMPAT_LIKELY(n > 0)) {
      b[0] = 0.0;
    }

    // Solve using LAPACK directly (column-major, no LAPACKE transpose/NaN overhead)
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    LAPACK_dgesv(&ldim, &nrhs, A, &ldim, ws.ipiv.data(), b, &ldim, &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    // Extract results
    std::memcpy(nodeVoltages, b, n * sizeof(double));
    std::memcpy(branchCurrents, b + n, m * sizeof(double));

    return true;
  }

  /**
   * @brief RT-safe LAPACK solve when conductances are pre-stamped into workspace.
   *
   * Assumes workspace A already has conductance stamps in column-major format
   * with leading dimension from StampContext::externalDim(). Skips the memset
   * and G_ to A copy, reducing overhead for the hot solve path.
   */
  COMPAT_HOT bool solveIntoLapackDirect(MnaSolveWorkspace& ws, const Vector& baseI,
                                        const std::vector<VoltageSource>& vsrc, std::size_t n,
                                        std::size_t m, std::size_t dim,
                                        double* __restrict__ nodeVoltages,
                                        double* __restrict__ branchCurrents) const noexcept {

    double* __restrict__ A = ws.A.data();
    double* __restrict__ b = ws.b.data();
    const double* __restrict__ iData = baseI.data();
    const std::size_t LD = ctx_.externalDim();

    // Conductances already stamped into A by stamp callbacks (column-major, stride LD).
    // Stamp voltage source contributions (column-major with stride LD).
    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      A[srcCol * LD + vs.pos] += 1.0;
      A[srcCol * LD + vs.neg] -= 1.0;
      A[vs.pos * LD + srcRow] = 1.0;
      A[vs.neg * LD + srcRow] = -1.0;
    }

    // Ground constraint: zero row 0 across all active columns
    if (COMPAT_LIKELY(n > 0)) {
      for (std::size_t c = 0; c < dim; ++c) {
        A[c * LD] = 0.0;
      }
      A[0] = 1.0;
    }

    // Build RHS vector
    std::memcpy(b, iData, n * sizeof(double));
    for (std::size_t k = 0; k < m; ++k) {
      b[n + k] = vsrc[k].v;
    }
    if (COMPAT_LIKELY(n > 0)) {
      b[0] = 0.0;
    }

    // Solve using LAPACK directly (column-major, lda = LD >= dim)
    lapack_int N = static_cast<lapack_int>(dim);
    lapack_int lda = static_cast<lapack_int>(LD);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    LAPACK_dgesv(&N, &nrhs, A, &lda, ws.ipiv.data(), b, &N, &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    // Extract results
    std::memcpy(nodeVoltages, b, n * sizeof(double));
    std::memcpy(branchCurrents, b + n, m * sizeof(double));

    return true;
  }
#endif

  /**
   * @brief RT-safe naive Gaussian elimination (fallback).
   *
   * Note: This version still allocates for the augmented matrix.
   * For true RT-safety without LAPACK, consider using a fixed-size matrix.
   */
  bool solveIntoNaive(MnaSolveWorkspace& /*ws*/, const Matrix& baseG, const Vector& baseI,
                      const std::vector<VoltageSource>& vsrc, std::size_t n, std::size_t m,
                      std::size_t dim, double* nodeVoltages,
                      double* branchCurrents) const noexcept {

    // Note: This allocates - for true RT-safety, use LAPACK or fixed-size arrays
    std::vector<std::vector<double>> aug(dim, std::vector<double>(dim + 1, 0.0));

    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        aug[r][c] = baseG[r * n + c];
      }
      aug[r][dim] = baseI[r];
    }

    for (std::size_t k = 0; k < m; ++k) {
      const auto& vs = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      aug[vs.pos][srcCol] += 1.0;
      aug[vs.neg][srcCol] -= 1.0;
      aug[srcRow][vs.pos] = 1.0;
      aug[srcRow][vs.neg] = -1.0;
      aug[srcRow][dim] = vs.v;
    }

    if (n > 0) {
      for (std::size_t c = 0; c <= dim; ++c) {
        aug[0][c] = 0.0;
      }
      aug[0][0] = 1.0;
    }

    for (std::size_t pivot = 0; pivot < dim; ++pivot) {
      std::size_t bestRow = pivot;
      double bestVal = std::fabs(aug[pivot][pivot]);
      for (std::size_t r = pivot + 1; r < dim; ++r) {
        double val = std::fabs(aug[r][pivot]);
        if (val > bestVal) {
          bestVal = val;
          bestRow = r;
        }
      }

      if (bestRow != pivot) {
        std::swap(aug[pivot], aug[bestRow]);
      }

      double pv = aug[pivot][pivot];
      if (std::fabs(pv) < 1e-15) {
        return false;
      }

      for (std::size_t c = pivot; c <= dim; ++c) {
        aug[pivot][c] /= pv;
      }

      for (std::size_t r = 0; r < dim; ++r) {
        if (r == pivot)
          continue;
        double factor = aug[r][pivot];
        for (std::size_t c = pivot; c <= dim; ++c) {
          aug[r][c] -= factor * aug[pivot][c];
        }
      }
    }

    for (std::size_t i = 0; i < n; ++i) {
      nodeVoltages[i] = aug[i][dim];
    }
    for (std::size_t i = 0; i < m; ++i) {
      branchCurrents[i] = aug[n + i][dim];
    }

    return true;
  }

  StampContext ctx_;

public:
  /**
   * @brief Get number of nets in the system.
   * @note RT-safe.
   */
  std::size_t netCount() const noexcept { return ctx_.netCount(); }

  /**
   * @brief Get number of voltage sources stamped.
   * @note RT-safe.
   */
  std::size_t voltageSourceCount() const noexcept { return ctx_.voltageSources().size(); }

  /**
   * @brief Get the conductance matrix (G).
   * @note RT-safe.
   */
  const StampContext::Matrix& conductanceMatrix() const noexcept { return ctx_.g(); }

  /**
   * @brief Get the current injection vector (I).
   * @note RT-safe.
   */
  const StampContext::Vector& currentVector() const noexcept { return ctx_.i(); }

  /**
   * @brief Get voltage source descriptors.
   * @note RT-safe.
   */
  const std::vector<StampContext::VoltageSource>& voltageSources() const noexcept {
    return ctx_.voltageSources();
  }
};

} // namespace sim::electronics::mna

#endif // APEX_SIM_ELECTRONICS_MNA_MNASYSTEM_HPP
