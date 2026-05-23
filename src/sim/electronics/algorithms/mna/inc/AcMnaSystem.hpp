#ifndef APEX_ACMNASYSTEM_HPP
#define APEX_ACMNASYSTEM_HPP
/**
 * @file AcMnaSystem.hpp
 * @brief AC (frequency domain) Modified Nodal Analysis solver.
 *
 * Extends MNA to handle complex impedances for AC circuit analysis.
 * Supports frequency sweeps, Bode plots, and cached LU factorization.
 *
 * Uses LAPACK zgesv/zgetrf/zgetrs for accelerated complex linear algebra
 * when available, falling back to naive complex Gaussian elimination otherwise.
 */

#include "src/sim/electronics/algorithms/mna/inc/Types.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_blas.hpp"

#if COMPAT_HAVE_LAPACKE
#include <complex.h>
#include <lapack.h>
#endif

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstring>
#include <numbers>
#include <string>
#include <vector>

namespace sim::electronics::algorithms::mna {

/* ----------------------------- Types ----------------------------- */

using Complex = std::complex<double>;

/**
 * @brief Result from AC MNA solve operation.
 */
struct AcMnaResult {
  std::vector<Complex> nodeVoltages;   ///< Complex voltage at each net.
  std::vector<Complex> branchCurrents; ///< Complex current through voltage sources.
  bool success = false;
  std::string errorMessage;
};

/**
 * @brief Pre-allocated workspace for RT-safe AC MNA solving.
 *
 * Allocate once at setup time, then reuse for multiple frequency points
 * without allocation in the hot path.
 */
struct AcMnaSolveWorkspace {
  std::vector<Complex> A; ///< Flattened complex matrix (dim x dim).
  std::vector<Complex> b; ///< Complex RHS vector (dim).
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
  [[nodiscard]] bool canHandle(std::size_t dim) const noexcept { return dim <= maxDim; }
};

/**
 * @brief Cached LU factorization for repeated AC solves (frequency sweeps).
 *
 * When circuit topology doesn't change (just frequency changes, thus
 * reactive element values change), cache the LU factorization at one
 * frequency and only do back-substitution per frequency point.
 *
 * Note: This is most useful when the circuit is resistive-dominant and
 * frequency changes don't significantly alter matrix structure. For
 * reactive-dominant circuits, full re-solve may be needed per frequency.
 *
 * Usage:
 * 1. Build circuit at frequency, call factorize() once
 * 2. Call solveFactorized() for nearby frequencies (RT-safe, O(n^2))
 * 3. If frequency range is large, refactorize periodically
 */
struct AcMnaFactorizedWorkspace {
  std::vector<Complex> LU; ///< Factorized complex matrix (dim x dim).
  std::vector<Complex> b;  ///< Working RHS vector.
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
  [[nodiscard]] bool isFactorized() const noexcept { return factorized; }

  /**
   * @brief Invalidate cached factorization.
   * @note RT-safe.
   */
  void invalidate() noexcept { factorized = false; }
};

/**
 * @brief Single frequency point result for sweep.
 */
struct AcFrequencyPoint {
  double frequency;    ///< Frequency in Hz.
  double omega;        ///< Angular frequency (2*pi*f).
  Complex voltage;     ///< Complex voltage at output node.
  double magnitudeDb;  ///< 20*log10(|V|) in dB.
  double phaseDegrees; ///< Phase angle in degrees.
};

/**
 * @brief Result from frequency sweep.
 */
struct AcSweepResult {
  std::vector<AcFrequencyPoint> points;
  NetID inputNet;
  NetID outputNet;
  double inputVoltage; ///< Reference input voltage magnitude.
};

/* ----------------------------- AcMnaSystem ----------------------------- */

/**
 * @brief AC nodal analysis engine with complex admittance matrix.
 *
 * For frequency domain analysis:
 * - Resistor: Y = G (real conductance)
 * - Capacitor: Y = j*omega*C
 * - Inductor: Y = 1/(j*omega*L) = -j/(omega*L)
 */
class AcMnaSystem {
public:
  /**
   * @param netCount Total number of nets (including ground at index 0).
   * @param omega Angular frequency (2*pi*f).
   */
  explicit AcMnaSystem(std::size_t netCount, double omega = 0.0)
      : netCount_(netCount), omega_(omega),
        Y_(netCount, std::vector<Complex>(netCount, Complex(0.0, 0.0))),
        I_(netCount, Complex(0.0, 0.0)) {}

  /**
   * @brief Set operating frequency.
   * @note RT-safe.
   */
  void setFrequency(double frequencyHz) noexcept { omega_ = 2.0 * std::numbers::pi * frequencyHz; }

  /**
   * @brief Get angular frequency.
   * @note RT-safe.
   */
  [[nodiscard]] double omega() const noexcept { return omega_; }

  /**
   * @brief Stamp a conductance (real admittance) between two nets.
   * @note RT-safe: stamps into pre-allocated matrix.
   */
  void stampConductance(NetID a, NetID b, double g) { stampAdmittance(a, b, Complex(g, 0.0)); }

  /**
   * @brief Stamp a complex admittance between two nets.
   * @note RT-safe: stamps into pre-allocated matrix.
   */
  void stampAdmittance(NetID a, NetID b, Complex y) {
    if (a == b) {
      Y_[a][a] += y;
    } else {
      Y_[a][a] += y;
      Y_[b][b] += y;
      Y_[a][b] -= y;
      Y_[b][a] -= y;
    }
  }

  /**
   * @brief Stamp a capacitor (Y = j*omega*C).
   * @note RT-safe: stamps into pre-allocated matrix.
   */
  void stampCapacitor(NetID a, NetID b, double capacitance) {
    Complex y(0.0, omega_ * capacitance);
    stampAdmittance(a, b, y);
  }

  /**
   * @brief Stamp an inductor (Y = -j/(omega*L)).
   * @note RT-safe: stamps into pre-allocated matrix.
   */
  void stampInductor(NetID a, NetID b, double inductance) {
    if (omega_ > 0.0 && inductance > 0.0) {
      Complex y(0.0, -1.0 / (omega_ * inductance));
      stampAdmittance(a, b, y);
    }
    // At DC (omega=0), inductor is short - handle separately
  }

  /**
   * @brief Stamp a complex current source.
   * @note RT-safe: stamps into pre-allocated vector.
   */
  void stampCurrent(NetID a, NetID b, Complex i) {
    I_[a] += i;
    I_[b] -= i;
  }

  /**
   * @brief Add an AC voltage source.
   * @return Index of this voltage source.
   * @note NOT RT-safe: may reallocate voltage source vector.
   */
  std::size_t addVoltageSource(NetID pos, NetID neg, Complex v) {
    voltageSources_.push_back({pos, neg, v});
    return voltageSources_.size() - 1;
  }

  /**
   * @brief Solve for complex node voltages and branch currents.
   *
   * Uses LAPACKE_zgesv (complex LU factorization) when available for
   * 5-10x speedup, falling back to complex Gaussian elimination otherwise.
   *
   * @return AcMnaResult containing voltages, currents, and success status.
   *
   * @note NOT RT-safe: allocates matrix and result vectors.
   */
  [[nodiscard]] AcMnaResult solve() const {
    AcMnaResult result;

    std::size_t n = netCount_;
    std::size_t m = voltageSources_.size();
    std::size_t dim = n + m;

    if (dim == 0) {
      result.success = true;
      return result;
    }

#if COMPAT_HAVE_LAPACKE
    // LAPACK-accelerated path: use zgesv for complex LU solve
    return solveLapack(Y_, I_, voltageSources_, n, m, dim);
#else
    // Fallback: naive complex Gaussian elimination
    return solveNaive(Y_, I_, voltageSources_, n, m, dim);
#endif
  }

  /**
   * @brief RT-safe solve using pre-allocated workspace.
   *
   * Writes results directly into provided vectors without allocation.
   * Workspace must be prepared with sufficient capacity before calling.
   *
   * @param ws Pre-allocated workspace (call ws.prepare() at setup time).
   * @param nodeVoltages Output array for node voltages (must be sized >= netCount).
   * @param branchCurrents Output array for branch currents (must be sized >= vsrcCount).
   * @return true on success, false on singular matrix.
   *
   * @note RT-safe if workspace and output arrays are pre-allocated.
   */
  bool solveInto(AcMnaSolveWorkspace& ws, Complex* nodeVoltages,
                 Complex* branchCurrents) const noexcept {
    std::size_t n = netCount_;
    std::size_t m = voltageSources_.size();
    std::size_t dim = n + m;

    if (dim == 0) {
      return true;
    }

    if (!ws.canHandle(dim)) {
      return false;
    }

#if COMPAT_HAVE_LAPACKE
    return solveIntoLapack(ws, Y_, I_, voltageSources_, n, m, dim, nodeVoltages, branchCurrents);
#else
    return solveIntoNaive(ws, Y_, I_, voltageSources_, n, m, dim, nodeVoltages, branchCurrents);
#endif
  }

  /**
   * @brief Clear all stamps for reuse at different frequency.
   * @note RT-safe: no allocation.
   */
  void clear() noexcept {
    for (auto& row : Y_) {
      std::fill(row.begin(), row.end(), Complex(0.0, 0.0));
    }
    std::fill(I_.begin(), I_.end(), Complex(0.0, 0.0));
    voltageSources_.clear();
  }

  /**
   * @brief Clear only current injections for RHS rebuild.
   *
   * Use this for cached LU optimization: keep admittance matrix intact,
   * only reset current vector before re-stamping current sources.
   *
   * @note RT-safe: no allocation.
   */
  void clearCurrents() noexcept { std::fill(I_.begin(), I_.end(), Complex(0.0, 0.0)); }

  /**
   * @brief Clear currents and voltage sources for RHS rebuild.
   *
   * Use this for cached LU optimization when voltage source VALUES
   * may change but their positions in the circuit stay the same.
   *
   * @note RT-safe: no allocation.
   */
  void clearRHS() noexcept {
    std::fill(I_.begin(), I_.end(), Complex(0.0, 0.0));
    voltageSources_.clear();
  }

  /**
   * @brief Get number of nets in the system.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t netCount() const noexcept { return netCount_; }

  /**
   * @brief Get number of voltage sources stamped.
   * @note RT-safe.
   */
  [[nodiscard]] std::size_t voltageSourceCount() const noexcept { return voltageSources_.size(); }

  /* ----------------------------- Cached LU Solve API ----------------------------- */

  /**
   * @brief Factorize the current circuit matrix (one-time cost).
   *
   * Performs complex LU decomposition and caches the result in the workspace.
   * After factorization, use solveFactorized() for O(n^2) back-substitution.
   *
   * @param ws Workspace to store factorization (must be prepared).
   * @return true on success, false on singular matrix or workspace too small.
   *
   * @note NOT RT-safe: performs O(n^3) complex LU factorization.
   */
  [[nodiscard]] bool factorize(AcMnaFactorizedWorkspace& ws) const noexcept {
    std::size_t n = netCount_;
    std::size_t m = voltageSources_.size();
    std::size_t dim = n + m;

    if (dim == 0 || dim > ws.maxDim) {
      return false;
    }

    ws.dim = dim;
    ws.factorized = false;

#if COMPAT_HAVE_LAPACKE
    return factorizeLapack(ws, Y_, I_, voltageSources_, n, m, dim);
#else
    // Naive fallback doesn't support caching well - just mark as not factorized
    return false;
#endif
  }

  /**
   * @brief Solve using cached LU factorization (RT-safe, O(n^2)).
   *
   * Uses the cached complex LU factors from factorize() to solve with new RHS.
   * Much faster than full solve() when circuit topology is unchanged.
   *
   * @param ws Workspace with valid factorization.
   * @param nodeVoltages Output array for node voltages.
   * @param branchCurrents Output array for branch currents.
   * @return true on success, false if not factorized.
   *
   * @note RT-safe: O(n^2) complex back-substitution only.
   */
  bool solveFactorized(AcMnaFactorizedWorkspace& ws, Complex* nodeVoltages,
                       Complex* branchCurrents) const noexcept {
    if (!ws.factorized) {
      return false;
    }

    std::size_t n = netCount_;
    std::size_t m = voltageSources_.size();
    std::size_t dim = ws.dim;

#if COMPAT_HAVE_LAPACKE
    return solveFactorizedLapack(ws, I_, voltageSources_, n, m, dim, nodeVoltages, branchCurrents);
#else
    (void)nodeVoltages;
    (void)branchCurrents;
    return false;
#endif
  }

private:
  struct VoltageSource {
    NetID pos;
    NetID neg;
    Complex voltage;
  };

  /* ----------------------------- Allocating Solve Methods ----------------------------- */

#if COMPAT_HAVE_LAPACKE
  /**
   * @brief LAPACK-accelerated complex solve using zgesv (column-major, direct LAPACK).
   */
  AcMnaResult solveLapack(const std::vector<std::vector<Complex>>& baseY,
                          const std::vector<Complex>& baseI, const std::vector<VoltageSource>& vsrc,
                          std::size_t n, std::size_t m, std::size_t dim) const {
    AcMnaResult result;

    // Allocate column-major complex matrix A and vector b
    std::vector<lapack_complex_double> A(dim * dim);
    std::vector<lapack_complex_double> b(dim);
    std::vector<lapack_int> ipiv(dim);

    // Zero the matrix
    std::memset(A.data(), 0, dim * dim * sizeof(lapack_complex_double));
    std::memset(b.data(), 0, dim * sizeof(lapack_complex_double));

    // Build column-major A: A_col[col * dim + row] = Y[row][col]
    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        const Complex& Y_VAL = baseY[r][c];
        A[c * dim + r] = lapack_make_complex_double(Y_VAL.real(), Y_VAL.imag());
      }
      const Complex& I_VAL = baseI[r];
      b[r] = lapack_make_complex_double(I_VAL.real(), I_VAL.imag());
    }

    // Stamp voltage source contributions (column-major)
    for (std::size_t k = 0; k < m; ++k) {
      const auto& VS = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      A[srcCol * dim + VS.pos] = lapack_make_complex_double(1.0, 0.0);
      A[srcCol * dim + VS.neg] = lapack_make_complex_double(-1.0, 0.0);
      A[VS.pos * dim + srcRow] = lapack_make_complex_double(1.0, 0.0);
      A[VS.neg * dim + srcRow] = lapack_make_complex_double(-1.0, 0.0);
      b[srcRow] = lapack_make_complex_double(VS.voltage.real(), VS.voltage.imag());
    }

    // Ground constraint (zero row 0 in column-major)
    if (n > 0) {
      for (std::size_t c = 0; c < dim; ++c) {
        A[c * dim] = lapack_make_complex_double(0.0, 0.0);
      }
      A[0] = lapack_make_complex_double(1.0, 0.0);
      b[0] = lapack_make_complex_double(0.0, 0.0);
    }

    // Solve using LAPACK complex solver (column-major)
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    LAPACK_zgesv(&ldim, &nrhs, A.data(), &ldim, ipiv.data(), b.data(), &ldim, &info);

    if (info != 0) {
      result.success = false;
      if (info > 0) {
        result.errorMessage = "Singular matrix at row " + std::to_string(info);
      } else {
        result.errorMessage = "LAPACK error: illegal argument " + std::to_string(-info);
      }
      return result;
    }

    // Extract results (LAPACK's lapack_complex_double is binary-compatible with
    // std::complex<double>)
    auto* b_complex = reinterpret_cast<Complex*>(b.data());
    result.nodeVoltages.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
      result.nodeVoltages[i] = b_complex[i];
    }

    result.branchCurrents.resize(m);
    for (std::size_t i = 0; i < m; ++i) {
      result.branchCurrents[i] = b_complex[n + i];
    }

    result.success = true;
    return result;
  }
#endif

  /**
   * @brief Naive complex Gaussian elimination fallback.
   */
  AcMnaResult solveNaive(const std::vector<std::vector<Complex>>& baseY,
                         const std::vector<Complex>& baseI, const std::vector<VoltageSource>& vsrc,
                         std::size_t n, std::size_t m, std::size_t dim) const {
    AcMnaResult result;

    // Build augmented matrix [A | b]
    std::vector<std::vector<Complex>> aug(dim, std::vector<Complex>(dim + 1, Complex(0.0, 0.0)));

    // Stamp Y matrix and I vector
    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        aug[r][c] = baseY[r][c];
      }
      aug[r][dim] = baseI[r];
    }

    // Stamp voltage sources
    for (std::size_t k = 0; k < m; ++k) {
      const auto& VS = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      aug[VS.pos][srcCol] += Complex(1.0, 0.0);
      aug[VS.neg][srcCol] -= Complex(1.0, 0.0);

      aug[srcRow][VS.pos] = Complex(1.0, 0.0);
      aug[srcRow][VS.neg] = Complex(-1.0, 0.0);
      aug[srcRow][dim] = VS.voltage;
    }

    // Ground constraint: V[0] = 0
    if (n > 0) {
      for (std::size_t c = 0; c <= dim; ++c) {
        aug[0][c] = Complex(0.0, 0.0);
      }
      aug[0][0] = Complex(1.0, 0.0);
    }

    // Gaussian elimination with partial pivoting (complex)
    for (std::size_t pivot = 0; pivot < dim; ++pivot) {
      std::size_t bestRow = pivot;
      double bestVal = std::abs(aug[pivot][pivot]);
      for (std::size_t r = pivot + 1; r < dim; ++r) {
        double val = std::abs(aug[r][pivot]);
        if (val > bestVal) {
          bestVal = val;
          bestRow = r;
        }
      }

      if (bestRow != pivot) {
        std::swap(aug[pivot], aug[bestRow]);
      }

      Complex pv = aug[pivot][pivot];
      if (std::abs(pv) < 1e-15) {
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
        Complex factor = aug[r][pivot];
        for (std::size_t c = pivot; c <= dim; ++c) {
          aug[r][c] -= factor * aug[pivot][c];
        }
      }
    }

    // Extract results
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

  /* ----------------------------- Cached LU Methods ----------------------------- */

#if COMPAT_HAVE_LAPACKE
  /**
   * @brief Build and factorize the complex MNA matrix using zgetrf (column-major, direct
   * LAPACK).
   */
  bool factorizeLapack(AcMnaFactorizedWorkspace& ws, const std::vector<std::vector<Complex>>& baseY,
                       const std::vector<Complex>& /*baseI*/,
                       const std::vector<VoltageSource>& vsrc, std::size_t n, std::size_t m,
                       std::size_t dim) const noexcept {

    lapack_complex_double* LU = reinterpret_cast<lapack_complex_double*>(ws.LU.data());

    // Zero and build column-major matrix
    std::memset(LU, 0, dim * dim * sizeof(lapack_complex_double));

    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        const Complex& Y_VAL = baseY[r][c];
        LU[c * dim + r] = lapack_make_complex_double(Y_VAL.real(), Y_VAL.imag());
      }
    }

    for (std::size_t k = 0; k < m; ++k) {
      const auto& VS = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      LU[srcCol * dim + VS.pos] = lapack_make_complex_double(1.0, 0.0);
      LU[srcCol * dim + VS.neg] = lapack_make_complex_double(-1.0, 0.0);
      LU[VS.pos * dim + srcRow] = lapack_make_complex_double(1.0, 0.0);
      LU[VS.neg * dim + srcRow] = lapack_make_complex_double(-1.0, 0.0);
    }

    // Ground constraint (zero row 0 in column-major)
    if (COMPAT_LIKELY(n > 0)) {
      for (std::size_t c = 0; c < dim; ++c) {
        LU[c * dim] = lapack_make_complex_double(0.0, 0.0);
      }
      LU[0] = lapack_make_complex_double(1.0, 0.0);
    }

    // LU factorization (O(n^3), column-major, direct LAPACK)
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int info = 0;
    LAPACK_zgetrf(&ldim, &ldim, LU, &ldim, ws.ipiv.data(), &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    ws.factorized = true;
    return true;
  }

  /**
   * @brief Solve using cached complex LU factors with zgetrs (O(n^2), column-major, direct
   * LAPACK).
   */
  COMPAT_HOT bool solveFactorizedLapack(AcMnaFactorizedWorkspace& ws,
                                        const std::vector<Complex>& baseI,
                                        const std::vector<VoltageSource>& vsrc, std::size_t n,
                                        std::size_t m, std::size_t dim, Complex* nodeVoltages,
                                        Complex* branchCurrents) const noexcept {

    lapack_complex_double* b = reinterpret_cast<lapack_complex_double*>(ws.b.data());

    // Build RHS vector
    for (std::size_t i = 0; i < n; ++i) {
      const Complex& I_VAL = baseI[i];
      b[i] = lapack_make_complex_double(I_VAL.real(), I_VAL.imag());
    }
    for (std::size_t k = 0; k < m; ++k) {
      const Complex& V_VAL = vsrc[k].voltage;
      b[n + k] = lapack_make_complex_double(V_VAL.real(), V_VAL.imag());
    }

    // Ground constraint
    if (COMPAT_LIKELY(n > 0)) {
      b[0] = lapack_make_complex_double(0.0, 0.0);
    }

    // Back-substitution using cached LU (O(n^2), column-major, direct LAPACK)
    char trans = 'N';
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    lapack_complex_double* LU = reinterpret_cast<lapack_complex_double*>(ws.LU.data());
    LAPACK_zgetrs(&trans, &ldim, &nrhs, LU, &ldim, ws.ipiv.data(), b, &ldim, &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    // Extract results (LAPACK's lapack_complex_double is binary-compatible with
    // std::complex<double>)
    auto* b_complex = reinterpret_cast<Complex*>(b);
    for (std::size_t i = 0; i < n; ++i) {
      nodeVoltages[i] = b_complex[i];
    }
    for (std::size_t i = 0; i < m; ++i) {
      branchCurrents[i] = b_complex[n + i];
    }

    return true;
  }
#endif

  /* ----------------------------- RT-Safe Solve Methods ----------------------------- */

#if COMPAT_HAVE_LAPACKE
  /**
   * @brief RT-safe LAPACK complex solve using pre-allocated workspace.
   */
  COMPAT_HOT bool solveIntoLapack(AcMnaSolveWorkspace& ws,
                                  const std::vector<std::vector<Complex>>& baseY,
                                  const std::vector<Complex>& baseI,
                                  const std::vector<VoltageSource>& vsrc, std::size_t n,
                                  std::size_t m, std::size_t dim, Complex* nodeVoltages,
                                  Complex* branchCurrents) const noexcept {

    lapack_complex_double* A = reinterpret_cast<lapack_complex_double*>(ws.A.data());
    lapack_complex_double* b = reinterpret_cast<lapack_complex_double*>(ws.b.data());

    // Zero the workspace
    std::memset(A, 0, dim * dim * sizeof(lapack_complex_double));

    // Build COLUMN-MAJOR complex admittance matrix
    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        const Complex& Y_VAL = baseY[r][c];
        A[c * dim + r] = lapack_make_complex_double(Y_VAL.real(), Y_VAL.imag());
      }
    }

    // Stamp voltage source contributions (column-major indexing)
    for (std::size_t k = 0; k < m; ++k) {
      const auto& VS = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      A[srcCol * dim + VS.pos] = lapack_make_complex_double(1.0, 0.0);
      A[srcCol * dim + VS.neg] = lapack_make_complex_double(-1.0, 0.0);
      A[VS.pos * dim + srcRow] = lapack_make_complex_double(1.0, 0.0);
      A[VS.neg * dim + srcRow] = lapack_make_complex_double(-1.0, 0.0);
    }

    // Ground constraint: zero row 0 in column-major
    if (COMPAT_LIKELY(n > 0)) {
      for (std::size_t c = 0; c < dim; ++c) {
        A[c * dim] = lapack_make_complex_double(0.0, 0.0);
      }
      A[0] = lapack_make_complex_double(1.0, 0.0);
    }

    // Build RHS vector
    for (std::size_t i = 0; i < n; ++i) {
      const Complex& I_VAL = baseI[i];
      b[i] = lapack_make_complex_double(I_VAL.real(), I_VAL.imag());
    }
    for (std::size_t k = 0; k < m; ++k) {
      const Complex& V_VAL = vsrc[k].voltage;
      b[n + k] = lapack_make_complex_double(V_VAL.real(), V_VAL.imag());
    }
    if (COMPAT_LIKELY(n > 0)) {
      b[0] = lapack_make_complex_double(0.0, 0.0);
    }

    // Solve using LAPACK complex solver (column-major)
    lapack_int ldim = static_cast<lapack_int>(dim);
    lapack_int nrhs = 1;
    lapack_int info = 0;
    LAPACK_zgesv(&ldim, &nrhs, A, &ldim, ws.ipiv.data(), b, &ldim, &info);

    if (COMPAT_UNLIKELY(info != 0)) {
      return false;
    }

    // Extract results (LAPACK's lapack_complex_double is binary-compatible with
    // std::complex<double>)
    auto* b_complex = reinterpret_cast<Complex*>(b);
    for (std::size_t i = 0; i < n; ++i) {
      nodeVoltages[i] = b_complex[i];
    }
    for (std::size_t i = 0; i < m; ++i) {
      branchCurrents[i] = b_complex[n + i];
    }

    return true;
  }
#endif

  /**
   * @brief RT-safe naive complex Gaussian elimination (fallback).
   */
  bool solveIntoNaive(AcMnaSolveWorkspace& /*ws*/, const std::vector<std::vector<Complex>>& baseY,
                      const std::vector<Complex>& baseI, const std::vector<VoltageSource>& vsrc,
                      std::size_t n, std::size_t m, std::size_t dim, Complex* nodeVoltages,
                      Complex* branchCurrents) const noexcept {

    // Note: This allocates - for true RT-safety with naive solver, use fixed-size arrays
    std::vector<std::vector<Complex>> aug(dim, std::vector<Complex>(dim + 1, Complex(0.0, 0.0)));

    for (std::size_t r = 0; r < n; ++r) {
      for (std::size_t c = 0; c < n; ++c) {
        aug[r][c] = baseY[r][c];
      }
      aug[r][dim] = baseI[r];
    }

    for (std::size_t k = 0; k < m; ++k) {
      const auto& VS = vsrc[k];
      std::size_t srcCol = n + k;
      std::size_t srcRow = n + k;

      aug[VS.pos][srcCol] += Complex(1.0, 0.0);
      aug[VS.neg][srcCol] -= Complex(1.0, 0.0);
      aug[srcRow][VS.pos] = Complex(1.0, 0.0);
      aug[srcRow][VS.neg] = Complex(-1.0, 0.0);
      aug[srcRow][dim] = VS.voltage;
    }

    if (n > 0) {
      for (std::size_t c = 0; c <= dim; ++c) {
        aug[0][c] = Complex(0.0, 0.0);
      }
      aug[0][0] = Complex(1.0, 0.0);
    }

    for (std::size_t pivot = 0; pivot < dim; ++pivot) {
      std::size_t bestRow = pivot;
      double bestVal = std::abs(aug[pivot][pivot]);
      for (std::size_t r = pivot + 1; r < dim; ++r) {
        double val = std::abs(aug[r][pivot]);
        if (val > bestVal) {
          bestVal = val;
          bestRow = r;
        }
      }

      if (bestRow != pivot) {
        std::swap(aug[pivot], aug[bestRow]);
      }

      Complex pv = aug[pivot][pivot];
      if (std::abs(pv) < 1e-15) {
        return false;
      }

      for (std::size_t c = pivot; c <= dim; ++c) {
        aug[pivot][c] /= pv;
      }

      for (std::size_t r = 0; r < dim; ++r) {
        if (r == pivot)
          continue;
        Complex factor = aug[r][pivot];
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

  std::size_t netCount_;
  double omega_;
  std::vector<std::vector<Complex>> Y_;
  std::vector<Complex> I_;
  std::vector<VoltageSource> voltageSources_;
};

/* ----------------------------- Frequency Sweep ----------------------------- */

/**
 * @brief Perform logarithmic frequency sweep.
 *
 * @param netCount Number of nets in circuit.
 * @param inputNet Net where AC voltage source is connected.
 * @param outputNet Net to measure output voltage.
 * @param inputVoltage AC source voltage magnitude (typically 1.0).
 * @param startFreq Start frequency in Hz.
 * @param endFreq End frequency in Hz.
 * @param pointsPerDecade Number of points per decade.
 * @param stampCircuit Callback to stamp circuit elements at given frequency.
 * @return Sweep result with magnitude and phase at each frequency.
 */
template <typename StampFunc>
AcSweepResult frequencySweep(std::size_t netCount, NetID inputNet, NetID outputNet,
                             double inputVoltage, double startFreq, double endFreq,
                             std::size_t pointsPerDecade, StampFunc stampCircuit) {

  AcSweepResult result;
  result.inputNet = inputNet;
  result.outputNet = outputNet;
  result.inputVoltage = inputVoltage;

  double logStart = std::log10(startFreq);
  double logEnd = std::log10(endFreq);
  double decades = logEnd - logStart;
  std::size_t totalPoints = static_cast<std::size_t>(decades * pointsPerDecade) + 1;

  for (std::size_t i = 0; i < totalPoints; ++i) {
    double logF = logStart + (logEnd - logStart) * i / (totalPoints - 1);
    double freq = std::pow(10.0, logF);
    double omega = 2.0 * std::numbers::pi * freq;

    AcMnaSystem ac(netCount, omega);
    stampCircuit(ac, omega);
    ac.addVoltageSource(inputNet, 0, Complex(inputVoltage, 0.0));

    auto acResult = ac.solve();
    if (!acResult.success) {
      continue; // Skip failed points
    }

    Complex vOut = acResult.nodeVoltages[outputNet];
    double magnitude = std::abs(vOut) / inputVoltage;
    double magnitudeDb = 20.0 * std::log10(magnitude);
    double phaseDeg = std::arg(vOut) * 180.0 / std::numbers::pi;

    result.points.push_back({freq, omega, vOut, magnitudeDb, phaseDeg});
  }

  return result;
}

/**
 * @brief Find -3dB cutoff frequency from sweep result.
 *
 * @param sweep Frequency sweep result.
 * @return Cutoff frequency in Hz, or -1 if not found.
 */
inline double findCutoffFrequency(const AcSweepResult& sweep) {
  if (sweep.points.empty())
    return -1.0;

  double dcGain = sweep.points[0].magnitudeDb;
  double target = dcGain - 3.0;

  for (std::size_t i = 1; i < sweep.points.size(); ++i) {
    if (sweep.points[i].magnitudeDb <= target) {
      // Linear interpolation between points
      double f1 = sweep.points[i - 1].frequency;
      double f2 = sweep.points[i].frequency;
      double g1 = sweep.points[i - 1].magnitudeDb;
      double g2 = sweep.points[i].magnitudeDb;

      double t = (target - g1) / (g2 - g1);
      return f1 + t * (f2 - f1);
    }
  }

  return -1.0; // Cutoff not found in range
}

} // namespace sim::electronics::algorithms::mna

#endif // APEX_ACMNASYSTEM_HPP
