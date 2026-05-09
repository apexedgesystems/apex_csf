#ifndef APEX_STAMPCONTEXT_HPP
#define APEX_STAMPCONTEXT_HPP
/**
 * @file StampContext.hpp
 * @brief Context structure for MNA matrix stamping operations.
 */

#include "src/sim/electronics/algorithms/mna/inc/Types.hpp"

#include <cassert>
#include <cstddef>
#include <vector>

namespace sim::electronics::algorithms::mna {

/**
 * @brief Context for stamping circuit elements into an MNA system.
 *
 * Maintains the conductance matrix (G), current injection vector (I),
 * and records ideal voltage sources for later augmentation.
 */
class StampContext {
public:
  using Matrix = std::vector<double>; ///< Flat row-major (netCount x netCount).
  using Vector = std::vector<double>;

  /* ----------------------------- Construction ----------------------------- */

  /**
   * @brief Construct the stamping context.
   * @param netCount Total number of nets (including ground).
   * @note NOT RT-safe: allocates G matrix and I vector.
   */
  explicit StampContext(std::size_t netCount)
      : netCount_(netCount), G_(netCount * netCount, 0.0), I_(netCount, 0.0), vsources_() {}

  /**
   * @brief Construct with external column-major conductance matrix.
   *
   * Stamps conductances directly into a pre-zeroed column-major matrix
   * (the LAPACK workspace), avoiding the intermediate G_ allocation and copy.
   * Only I_ and voltage sources are maintained internally.
   *
   * @param netCount Total number of nets.
   * @param externalA Pre-zeroed column-major matrix (augDim x augDim doubles).
   * @param augDim Augmented dimension (netCount + voltageSourceCount estimate).
   * @note NOT RT-safe: allocates I vector. The external matrix must remain
   *       valid for the lifetime of this context.
   */
  StampContext(std::size_t netCount, double* externalA, std::size_t augDim)
      : netCount_(netCount), I_(netCount, 0.0), vsources_(), extA_(externalA), extDim_(augDim) {}

  /**
   * @brief Construct in matvec mode for sparse A*x computation.
   *
   * Instead of building a matrix, stampConductance computes A*x contributions
   * directly into the output vector. This eliminates the O(n^2) matrix build
   * and dense dgemv, replacing them with O(nnz) sparse operations.
   *
   * @param netCount Total number of nets.
   * @param x Input vector (dim entries: node voltages + branch currents).
   * @param ax Output accumulator for A*x (must be pre-zeroed, dim entries).
   * @note NOT RT-safe: allocates I vector. x and ax must remain valid for
   *       the lifetime of this context.
   */
  StampContext(std::size_t netCount, const double* x, double* ax)
      : netCount_(netCount), I_(netCount, 0.0), vsources_(), matvecX_(x), matvecAx_(ax) {}

  /* ----------------------------- Stamping API ----------------------------- */

  /**
   * @brief Stamp a resistor (conductance) between two nets.
   * @note RT-safe: arithmetic on pre-allocated arrays only.
   */
  void stampConductance(NetID a, NetID b, double g) {
    assert(a < netCount_ && b < netCount_);
    if (matvecX_) {
      // Matvec mode: accumulate g*(x[a]-x[b]) into Ax directly
      if (a == b) {
        matvecAx_[a] += g * matvecX_[a];
      } else {
        double gDiff = g * (matvecX_[a] - matvecX_[b]);
        matvecAx_[a] += gDiff;
        matvecAx_[b] -= gDiff;
      }
    } else if (extA_) {
      // Direct column-major stamp into LAPACK workspace: A[col * dim + row]
      std::size_t d = extDim_;
      if (a == b) {
        extA_[a * d + a] += g;
      } else {
        extA_[a * d + a] += g;
        extA_[b * d + b] += g;
        extA_[b * d + a] -= g; // Element (row=a, col=b) in column-major
        extA_[a * d + b] -= g; // Element (row=b, col=a) in column-major
      }
    } else {
      // Internal row-major G_ matrix
      if (a == b) {
        G_[a * netCount_ + a] += g;
      } else {
        G_[a * netCount_ + a] += g;
        G_[b * netCount_ + b] += g;
        G_[a * netCount_ + b] -= g;
        G_[b * netCount_ + a] -= g;
      }
    }
  }

  /**
   * @brief Stamp a single asymmetric G matrix entry: G[row][col] += value.
   *
   * Unlike stampConductance (which enforces symmetric stamps for resistors),
   * this stamps a single entry. Required for VCCS elements like MOSFET
   * transconductance gm, where gate controls drain current but drain does
   * not control gate current.
   *
   * @param row Row net (current equation).
   * @param col Column net (voltage dependency).
   * @param value Conductance increment.
   * @note RT-safe: arithmetic on pre-allocated array only.
   */
  void stampGEntry(NetID row, NetID col, double value) {
    assert(row < netCount_ && col < netCount_);
    if (matvecX_) {
      matvecAx_[row] += value * matvecX_[col];
    } else if (extA_) {
      extA_[col * extDim_ + row] += value; // column-major: A[row, col]
    } else {
      G_[row * netCount_ + col] += value; // row-major
    }
  }

  /**
   * @brief Stamp a current injection between two nets.
   * @note RT-safe: arithmetic on pre-allocated vector only.
   */
  void stampCurrent(NetID a, NetID b, double i) {
    assert(a < netCount_ && b < netCount_);
    I_[a] += i;
    I_[b] -= i;
  }

  /**
   * @brief Stamp an ideal voltage source between two nets.
   *
   * Records it for matrix augmentation during solve().
   * @note NOT RT-safe: may reallocate voltage source vector.
   */
  void stampVoltageSource(NetID pos, NetID neg, double v) {
    assert(pos < netCount_ && neg < netCount_);
    vsources_.push_back({pos, neg, v});
  }

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Access conductance matrix.
   * @note RT-safe.
   */
  const Matrix& g() const noexcept { return G_; }

  /**
   * @brief Access current injection vector.
   * @note RT-safe.
   */
  const Vector& i() const noexcept { return I_; }

  /**
   * @brief Number of nets (matrix dimension).
   * @note RT-safe.
   */
  std::size_t netCount() const noexcept { return netCount_; }

  /**
   * @brief Descriptor for an ideal voltage source.
   */
  struct VoltageSource {
    NetID pos; ///< Positive net ID.
    NetID neg; ///< Negative net ID.
    double v;  ///< Voltage (Vpos - Vneg).
  };

  /**
   * @brief Retrieve all stamped ideal voltage sources.
   * @note RT-safe.
   */
  const std::vector<VoltageSource>& voltageSources() const noexcept { return vsources_; }

  /* ----------------------------- Mutation ----------------------------- */

  /**
   * @brief Clear all stamps for reuse.
   *
   * Resets G matrix to zeros, I vector to zeros, and clears voltage sources.
   * Does not deallocate memory - just resets values.
   *
   * @note RT-safe: no allocation, just memset-like operations.
   */
  void clear() noexcept {
    std::fill(G_.begin(), G_.end(), 0.0);
    std::fill(I_.begin(), I_.end(), 0.0);
    vsources_.clear();
  }

  /**
   * @brief Check if stamping directly into external matrix.
   * @note RT-safe.
   */
  bool hasExternalMatrix() const noexcept { return extA_ != nullptr; }

  /**
   * @brief Get external matrix leading dimension.
   * @note RT-safe. Only meaningful when hasExternalMatrix() is true.
   */
  std::size_t externalDim() const noexcept { return extDim_; }

  /**
   * @brief Clear only current injections for RHS rebuild.
   *
   * Keeps G matrix and voltage sources intact. Use this when the circuit
   * topology is unchanged but current sources have new values.
   *
   * @note RT-safe: no allocation.
   */
  void clearCurrents() noexcept { std::fill(I_.begin(), I_.end(), 0.0); }

  /**
   * @brief Clear currents and voltage sources for RHS rebuild.
   *
   * Keeps G matrix intact. Use this for cached LU when voltage source
   * VALUES may change but positions stay the same.
   *
   * @note RT-safe: no allocation (just clears, doesn't deallocate).
   */
  void clearRHS() noexcept {
    std::fill(I_.begin(), I_.end(), 0.0);
    vsources_.clear();
  }

  /**
   * @brief Update voltage source value without changing topology.
   *
   * Used for cached LU solves where the matrix is unchanged but
   * source values have been updated.
   *
   * @param idx Index of voltage source to update.
   * @param v New voltage value.
   * @note RT-safe if idx is valid.
   */
  void updateVoltageSourceValue(std::size_t idx, double v) noexcept {
    if (idx < vsources_.size()) {
      vsources_[idx].v = v;
    }
  }

private:
  std::size_t netCount_;                ///< Number of nets.
  Matrix G_;                            ///< Conductance matrix.
  Vector I_;                            ///< Current injection vector.
  std::vector<VoltageSource> vsources_; ///< Recorded voltage sources.
  double* extA_ = nullptr;              ///< External column-major LAPACK workspace (nullable).
  std::size_t extDim_ = 0;              ///< Augmented dimension of external workspace.
  const double* matvecX_ = nullptr;     ///< Input vector for matvec mode (nullable).
  double* matvecAx_ = nullptr;          ///< Output Ax accumulator for matvec mode (nullable).
};

} // namespace sim::electronics::algorithms::mna

#endif // APEX_STAMPCONTEXT_HPP
