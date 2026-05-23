#ifndef APEX_TRANSIENTSOLVER_TPP
#define APEX_TRANSIENTSOLVER_TPP
/**
 * @file TransientSolver.tpp
 * @brief TransientSolver implementation.
 */

#include "src/sim/electronics/algorithms/transient/inc/TransientSolver.hpp"

namespace sim::electronics::algorithms::transient {

/* ----------------------------- Construction ----------------------------- */

inline TransientSolver::TransientSolver(std::size_t netCount)
    : netCount_(netCount), prevVoltages_(netCount, 0.0) {
  // Pre-allocate workspace for RT-safe stepping
  // Estimate max dimension: nets + some voltage sources
  workspace_.prepare(netCount + 16);

  // Pre-allocate factorized workspace for cached LU
  factorizedWs_.prepare(netCount + 16);

  // CUDA workspace is lazy-initialized on setCuda(true) to avoid
  // paying cusolverDnCreate + cublasCreate_v2 overhead (~2ms) when
  // using sparse or CPU-only solve paths.
}

/* ----------------------------- Reset ----------------------------- */

inline void TransientSolver::reset() noexcept {
  time_ = 0.0;
  companions_.reset();
  std::fill(prevVoltages_.begin(), prevVoltages_.end(), 0.0);
  invalidateCache();
}

/* ----------------------------- Stamp Callback Dispatch ----------------------------- */

inline void TransientSolver::invokeStampCallback(MnaSystem& mna, double time) {
  if (statefulStampCallback_) {
    statefulStampCallback_(mna, time, prevVoltages_);
  } else if (stampCallback_) {
    stampCallback_(mna, time);
  }
}

/* ----------------------------- DC Operating Point ----------------------------- */

inline TransientStatus TransientSolver::computeDC(TransientState& state) {
  state.resize(netCount_, 0);
  state.time = 0.0;

  // For DC: capacitors are open (no current), inductors are shorted (no voltage)

  // Use sparse solver for DC if sparse mode is enabled
  if (useSparse_ && statefulStampCallbackSparse_) {
    MnaSystemSparse sparseDc(netCount_);

    statefulStampCallbackSparse_(sparseDc, 0.0, prevVoltages_);

    // Inductors: short circuit (stamp as very high conductance)
    for (std::size_t i = 0; i < companions_.inductorCount(); ++i) {
      const auto& IND = companions_.inductor(i);
      sparseDc.addConductance(IND.posNet, IND.negNet, 1000.0);
    }

    if (!sparseDc.factorize()) {
      return TransientStatus::ERROR_DC_FAILED;
    }

    std::size_t n = netCount_;
    std::size_t m = sparseDc.voltageSourceCount();
    state.nodeVoltages.resize(n);
    state.branchCurrents.resize(m);

    if (!sparseDc.solveInto(state.nodeVoltages.data(), state.branchCurrents.data())) {
      return TransientStatus::ERROR_DC_FAILED;
    }
  } else {
    // Dense path: stamp into MnaSystem
    MnaSystem mna(netCount_);

    invokeStampCallback(mna, 0.0);

    // Inductors: short circuit (stamp as very high conductance)
    for (std::size_t i = 0; i < companions_.inductorCount(); ++i) {
      const auto& IND = companions_.inductor(i);
      mna.addConductance(IND.posNet, IND.negNet, 1000.0);
    }

    MnaResult result = mna.solve();

    if (!result.success) {
      return TransientStatus::ERROR_DC_FAILED;
    }

    state.nodeVoltages = std::move(result.nodeVoltages);
    state.branchCurrents = std::move(result.branchCurrents);
  }

  // Track previous voltages for stateful stamp callbacks
  prevVoltages_ = state.nodeVoltages;

  // Initialize companions from DC solution
  companions_.initializeFromDC(state.nodeVoltages);

  return TransientStatus::SUCCESS;
}

/* ----------------------------- Single Step ----------------------------- */

inline TransientStatus TransientSolver::step(double dt, TransientState& state) {
  // Sparse cached LU path
  if (useSparse_ && sparseCacheValid_) {
    return solveCachedStepSparse(dt, state);
  }

  // Dense cached LU path
  if (useCachedLU_ && factorizedWs_.isFactorized() && std::fabs(dt - cachedDt_) < 1e-15 * dt) {
    return solveCachedStep(dt, state);
  }

  // Full solve path (also factorizes for next step if cached LU enabled)
  return solveStep(dt, state);
}

inline TransientStatus TransientSolver::solveStep(double dt, TransientState& state) {
  // Sparse LU path (KLU for low-fill circuits)
  if (useSparse_) {
    if (!sparseMna_) {
      sparseMna_ = std::make_unique<MnaSystemSparse>(netCount_);
    }

    std::size_t n = netCount_;

    // Resize output arrays
    if (state.nodeVoltages.size() < n) {
      state.nodeVoltages.resize(n);
    }

    // Newton-Raphson iteration for nonlinear devices (alwaysReanalyze_ path).
    // Linearize around current voltages, solve, check convergence, repeat.
    if (alwaysReanalyze_) {
      // Use prevVoltages_ as initial NR guess
      if (nrPrevIterV_.size() < n) {
        nrPrevIterV_.resize(n);
      }
      std::copy(prevVoltages_.begin(), prevVoltages_.end(), nrPrevIterV_.begin());

      // nrPreBatchCallback runs AFTER copy (Entry 16: reordering didn't
      // unlock multi-instruction, structural limit is elsewhere).
      if (nrPreBatchCallback_) {
        nrPreBatchCallback_(dt);
      }

      for (std::size_t iter = 0; iter < nrMaxIterations_; ++iter) {
        sparseMna_->clear();

        if (statefulStampCallbackSparse_) {
          statefulStampCallbackSparse_(*sparseMna_, time_ + dt, nrPrevIterV_);
        }
        companions_.stampAll(*sparseMna_, dt, method_);

        if (!sparseMna_->factorize()) {
          return TransientStatus::ERROR_STEP_FAILED;
        }

        std::size_t m = sparseMna_->voltageSourceCount();
        if (state.branchCurrents.size() < m) {
          state.branchCurrents.resize(m);
        }

        if (!sparseMna_->solveInto(state.nodeVoltages.data(), state.branchCurrents.data())) {
          return TransientStatus::ERROR_STEP_FAILED;
        }

        // NaN/Inf protection + 5V NR damping.
        // Limits max voltage change per NR iteration to 5V to prevent
        // divergence. Per-device fetlim/limvds handles most limiting during
        // stamp evaluation; this is a safety net for the full circuit.
        // See COMPONENT_ARCHITECTURE.md Entry 10 for pass gate analysis.
        {
          double maxChange = 0;
          for (std::size_t i = 0; i < n; ++i) {
            if (std::isnan(state.nodeVoltages[i]) || std::isinf(state.nodeVoltages[i])) {
              state.nodeVoltages[i] = nrPrevIterV_[i];
              continue;
            }
            double c = std::fabs(state.nodeVoltages[i] - nrPrevIterV_[i]);
            if (c > maxChange)
              maxChange = c;
          }
          if (maxChange > 5.0) {
            double damp = 5.0 / maxChange;
            for (std::size_t i = 0; i < n; ++i) {
              state.nodeVoltages[i] =
                  nrPrevIterV_[i] + damp * (state.nodeVoltages[i] - nrPrevIterV_[i]);
            }
          }
        }

        if (nrLimitCallback_) {
          nrLimitCallback_(state.nodeVoltages, nrPrevIterV_);
        }

        // Convergence check
        double maxDelta = 0.0;
        constexpr double RELTOL = 1e-3;
        for (std::size_t i = 0; i < n; ++i) {
          double tol =
              RELTOL * std::max(std::fabs(state.nodeVoltages[i]), std::fabs(nrPrevIterV_[i])) +
              nrVoltageTolerance_;
          double delta = std::fabs(state.nodeVoltages[i] - nrPrevIterV_[i]);
          if (delta > tol && delta > maxDelta) {
            maxDelta = delta;
          }
        }

        std::copy(state.nodeVoltages.begin(), state.nodeVoltages.begin() + n, nrPrevIterV_.begin());

        if (maxDelta == 0.0) {
          break; // Converged
        }
      }

      // Accept result (converged or not) and advance time
      prevVoltages_ = state.nodeVoltages;
      companions_.updateAll(state.nodeVoltages, dt);
      time_ += dt;

      sparseCacheValid_ = false;
      sparseCachedDim_ = n + sparseMna_->voltageSourceCount();
      state.time = time_;

      return TransientStatus::SUCCESS;
    }

    // Linear sparse path (binary switches, constant topology)
    std::size_t prevNnz = sparseMna_->nnz();

    sparseMna_->clear();

    // Stamp static elements via sparse callback
    if (statefulStampCallbackSparse_) {
      statefulStampCallbackSparse_(*sparseMna_, time_ + dt, prevVoltages_);
    }

    // Stamp companion models for reactive elements
    companions_.stampAll(*sparseMna_, dt, method_);

    // 3-level factorization caching:
    //  1. Pattern changed (nnz differs): full factorize
    //  2. Values changed (same nnz, different triplets): numeric-only refactorize
    //  3. Identical stamps (same triplets): RHS-only update, reuse LU factors
    std::size_t currentNnz = sparseMna_->nnz();
    bool patternChanged = !sparseMna_->isPatternAnalyzed() || (currentNnz != prevNnz);

    if (patternChanged) {
      if (!sparseMna_->factorize()) {
        return TransientStatus::ERROR_STEP_FAILED;
      }
    } else if (!sparseMna_->tripletsMatch()) {
      if (!sparseMna_->refactorizeInPlace()) {
        return TransientStatus::ERROR_STEP_FAILED;
      }
    } else {
      sparseMna_->updateRhs();
    }

    std::size_t m = sparseMna_->voltageSourceCount();
    if (state.branchCurrents.size() < m) {
      state.branchCurrents.resize(m);
    }

    // Solve into pre-allocated buffers
    if (!sparseMna_->solveInto(state.nodeVoltages.data(), state.branchCurrents.data())) {
      return TransientStatus::ERROR_STEP_FAILED;
    }

    sparseCacheValid_ = true;
    sparseCachedDim_ = n + m;

    // Update state
    state.time = time_ + dt;
    prevVoltages_ = state.nodeVoltages;
    companions_.updateAll(state.nodeVoltages, dt);
    time_ += dt;

    return TransientStatus::SUCCESS;
  }

  // Build MNA system.
  // Stamp conductances directly into LAPACK workspace (column-major)
  // to eliminate intermediate G_ allocation and copy overhead.
  const std::size_t LD = workspace_.maxDim;
  std::memset(workspace_.A.data(), 0, LD * LD * sizeof(double));
  MnaSystem mna(netCount_, workspace_.A.data(), LD);

  // Stamp static elements (resistors, sources)
  invokeStampCallback(mna, time_ + dt);

  // Stamp companion models for reactive elements
  companions_.stampAll(mna, dt, method_);

  // Solve using pre-allocated workspace (avoids per-solve allocation)
  std::size_t n = mna.netCount();
  std::size_t m = mna.voltageSourceCount();

  // Ensure output arrays are sized (only resizes if needed)
  if (state.nodeVoltages.size() < n) {
    state.nodeVoltages.resize(n);
  }
  if (state.branchCurrents.size() < m) {
    state.branchCurrents.resize(m);
  }

  bool ok = mna.solveInto(workspace_, state.nodeVoltages.data(), state.branchCurrents.data());

  if (!ok) {
    return TransientStatus::ERROR_STEP_FAILED;
  }

  // Update state
  state.time = time_ + dt;

  // Track previous voltages for stateful stamp callbacks
  prevVoltages_ = state.nodeVoltages;

  companions_.updateAll(state.nodeVoltages, dt);

  time_ += dt;

  // captureLU copies the LU factors that dgesv already computed in
  // workspace_.A, avoiding a separate O(n^3) buildAndFactorize call on the
  // next step.
  if (useCachedLU_) {
    std::size_t dim = n + m;
    captureLU(dt, dim);
  }

  return TransientStatus::SUCCESS;
}

/* ----------------------------- Cached LU Methods ----------------------------- */

inline bool TransientSolver::buildAndFactorize(double dt) {
  // Create or reuse cached MNA system
  if (!cachedMna_) {
    cachedMna_ = std::make_unique<MnaSystem>(netCount_);
  }

  cachedMna_->clear();

  // Stamp static elements (conductances only)
  invokeStampCallback(*cachedMna_, time_ + dt);

  // Stamp companion conductances (not currents)
  companions_.stampConductanceAll(*cachedMna_, dt, method_);

  // Factorize the matrix
  bool ok = cachedMna_->factorize(factorizedWs_);

  if (ok) {
    cachedDt_ = dt;
  }

  return ok;
}

/* ----------------------------- Capture LU from Workspace ----------------------------- */

inline void TransientSolver::captureLU(double dt, std::size_t dim) {
  const std::size_t LD = workspace_.maxDim;

  // Copy LU factors from workspace (stride LD) to factorized workspace (stride dim).
  // After dgesv, workspace_.A contains the LU decomposition in column-major format.
  // The factorized workspace uses stride = dim, so we copy column by column.
  for (std::size_t c = 0; c < dim; ++c) {
    std::memcpy(factorizedWs_.LU.data() + c * dim, workspace_.A.data() + c * LD,
                dim * sizeof(double));
  }
  std::memcpy(factorizedWs_.ipiv.data(), workspace_.ipiv.data(), dim * sizeof(int));

  factorizedWs_.dim = dim;
  factorizedWs_.factorized = true;
  cachedDt_ = dt;
}

inline TransientStatus TransientSolver::solveCachedStep(double dt, TransientState& state) {
  // Sparse matvec iterative refinement using cached LU factorization.
  //
  // Instead of building the full dense augmented matrix A_new and using dgemv,
  // we compute A_new * x_prev directly through the stamp callbacks. Each
  // stampConductance(a,b,g) accumulates g*(x[a]-x[b]) into the result vector.
  // This replaces O(n^2) memset + matrix build + dgemv with O(nnz) sparse ops.
  //
  // 1. Assemble x_prev = [nodeVoltages | branchCurrents]
  // 2. Stamp in matvec mode: conductances compute A*x contributions
  // 3. Augment: voltage source and ground constraint rows of A*x
  // 4. Compute residual r = b - A*x (element-wise)
  // 5. Solve cached_LU * dx = r  (O(n^2) back-substitution)
  // 6. Update x_new = x_prev + dx

  std::size_t n = netCount_;
  std::size_t dim = factorizedWs_.dim;
  std::size_t m = dim - n;

  // Safety: ensure state vectors are large enough for x_prev assembly
  if (state.nodeVoltages.size() < n || state.branchCurrents.size() < m) {
    return solveStep(dt, state);
  }

  // 1. Assemble x_prev = [nodeVoltages | branchCurrents]
  double* __restrict__ x = workspace_.b.data();
  std::memcpy(x, state.nodeVoltages.data(), n * sizeof(double));
  std::memcpy(x + n, state.branchCurrents.data(), m * sizeof(double));

  // 2. Zero Ax accumulator (dim doubles ~ 1.2KB vs 180KB dense matrix)
  double* __restrict__ ax = workspace_.A.data();
  std::memset(ax, 0, dim * sizeof(double));

  // 3. Stamp in matvec mode: conductances compute g*(x[a]-x[b]) directly
  MnaSystem mna(netCount_, x, ax);
  invokeStampCallback(mna, time_ + dt);
  companions_.stampAll(mna, dt, method_);

  // Dimension mismatch with cached LU: fall back to full solve
  if (n + mna.voltageSourceCount() != dim) {
    return solveStep(dt, state);
  }

  // 4. Augment Ax with voltage source and ground constraint contributions
  const auto& VSRC = mna.voltageSources();
  for (std::size_t k = 0; k < m; ++k) {
    const auto& VS = VSRC[k];
    // Column n+k has +1 at VS.pos, -1 at VS.neg
    ax[VS.pos] += x[n + k];
    ax[VS.neg] -= x[n + k];
    // Row n+k: KVL constraint V_pos - V_neg
    ax[n + k] = x[VS.pos] - x[VS.neg];
  }
  // Ground constraint: row 0 is [1 0 0...0], so Ax[0] = x[0]
  if (COMPAT_LIKELY(n > 0)) {
    ax[0] = x[0];
  }

  // 5. Build residual r = b - Ax directly into factorizedWs_.b
  double* __restrict__ r = factorizedWs_.b.data();
  const double* __restrict__ I_DATA = mna.currentVector().data();
  for (std::size_t i = 0; i < n; ++i) {
    r[i] = I_DATA[i] - ax[i];
  }
  for (std::size_t k = 0; k < m; ++k) {
    r[n + k] = VSRC[k].v - ax[n + k];
  }
  // Ground: b[0]=0, so r[0] = 0 - Ax[0]
  if (COMPAT_LIKELY(n > 0)) {
    r[0] = -ax[0];
  }

  // 6. Solve cached LU * dx = residual (O(n^2) back-substitution)
  char trans = 'N';
  lapack_int ldim = static_cast<lapack_int>(dim);
  lapack_int nrhs = 1;
  lapack_int info = 0;
  LAPACK_dgetrs(&trans, &ldim, &nrhs, factorizedWs_.LU.data(), &ldim, factorizedWs_.ipiv.data(), r,
                &ldim, &info);

  if (COMPAT_UNLIKELY(info != 0)) {
    return solveStep(dt, state);
  }

  // 7. Update solution: x_new = x_prev + dx
  for (std::size_t i = 0; i < n; ++i) {
    state.nodeVoltages[i] += r[i];
  }
  for (std::size_t i = 0; i < m; ++i) {
    state.branchCurrents[i] += r[n + i];
  }

  // 8. Update state
  state.time = time_ + dt;
  prevVoltages_ = state.nodeVoltages;
  companions_.updateAll(state.nodeVoltages, dt);
  time_ += dt;

  return TransientStatus::SUCCESS;
}

/* ----------------------------- Cached Sparse LU ----------------------------- */

inline TransientStatus TransientSolver::solveCachedStepSparse(double dt, TransientState& state) {
  // Sparse iterative refinement using cached SparseLU factorization.
  //
  // Instead of rebuilding the CSC matrix and re-factorizing each sub-step,
  // we compute A_new * x_prev directly from triplets (O(nnz)), then solve
  // the residual with the cached SparseLU factors. This replaces the expensive
  // buildAugmentedMatrix + refactorize with a triplet matvec + back-solve.
  //
  // Cost per cached step: ~0.4ms (stamp + triplet matvec + back-solve)
  // vs full sparse step: ~5ms (stamp + build CSC + refactorize + solve)

  std::size_t n = netCount_;

  // Safety: ensure state vectors and sparse solver exist
  if (!sparseMna_ || state.nodeVoltages.size() < n) {
    return solveStep(dt, state);
  }

  // Clear stamps but preserve cached LU factorization
  sparseMna_->clearStamps();

  if (statefulStampCallbackSparse_) {
    statefulStampCallbackSparse_(*sparseMna_, time_ + dt, prevVoltages_);
  }

  companions_.stampAll(*sparseMna_, dt, method_);

  // Dimension check against cached factorization
  std::size_t m = sparseMna_->voltageSourceCount();
  std::size_t dim = n + m;
  if (dim != sparseCachedDim_ || state.branchCurrents.size() < m) {
    sparseCacheValid_ = false;
    return solveStep(dt, state);
  }

  // 1. Assemble x_prev = [nodeVoltages | branchCurrents]
  std::vector<double>& xBuf = workspace_.b;
  if (xBuf.size() < dim) {
    xBuf.resize(dim);
  }
  std::memcpy(xBuf.data(), state.nodeVoltages.data(), n * sizeof(double));
  std::memcpy(xBuf.data() + n, state.branchCurrents.data(), m * sizeof(double));

  // 2. Compute A*x_prev via triplet matvec (O(nnz), no CSC build)
  std::vector<double>& axBuf = workspace_.A;
  if (axBuf.size() < dim) {
    axBuf.resize(dim);
  }
  sparseMna_->computeMatvec(xBuf.data(), axBuf.data(), dim);

  // 3. Build RHS b
  const auto& I_DATA = sparseMna_->currentVector();
  const auto& VSRC = sparseMna_->voltageSources();

  // 4. Compute residual r = b - Ax
  //    Reuse workspace_.b for residual (we no longer need x_prev in it)
  //    Actually, we still need x_prev for the update step, so use a separate buffer
  std::vector<double>& rBuf = factorizedWs_.b;
  if (rBuf.size() < dim) {
    rBuf.resize(dim);
  }

  // Current injection rows
  for (std::size_t i = 0; i < n; ++i) {
    rBuf[i] = I_DATA[i] - axBuf[i];
  }
  // Voltage source rows
  for (std::size_t k = 0; k < m; ++k) {
    rBuf[n + k] = VSRC[k].v - axBuf[n + k];
  }
  // Ground constraint: b[0] = 0, so r[0] = 0 - ax[0] = -ax[0]
  if (COMPAT_LIKELY(n > 0)) {
    rBuf[0] = -axBuf[0];
  }

  // 5. Solve cached SparseLU * dx = residual
  //    Reuse axBuf as dx output (no longer needed)
  if (!sparseMna_->solveCached(rBuf.data(), axBuf.data(), dim)) {
    // Cache solve failed, fall back to full solve
    sparseCacheValid_ = false;
    return solveStep(dt, state);
  }

  // 6. Update solution: x_new = x_prev + dx
  for (std::size_t i = 0; i < n; ++i) {
    state.nodeVoltages[i] += axBuf[i];
  }
  for (std::size_t i = 0; i < m; ++i) {
    state.branchCurrents[i] += axBuf[n + i];
  }

  // 7. Update state
  state.time = time_ + dt;
  prevVoltages_ = state.nodeVoltages;
  companions_.updateAll(state.nodeVoltages, dt);
  time_ += dt;

  return TransientStatus::SUCCESS;
}

/* ----------------------------- Dual-LU Caching ----------------------------- */

inline TransientStatus TransientSolver::stepDual(double dt, int stateIdx, TransientState& state) {
  // Validate state index
  if (stateIdx < 0 || stateIdx > 1) {
    return TransientStatus::ERROR_STEP_FAILED;
  }

  // Check if we have a valid cache for this state
  if (dualFactorizedWs_[stateIdx].isFactorized() && std::fabs(dt - dualCachedDt_) < 1e-15 * dt) {
    return solveDualCachedStep(dt, stateIdx, state);
  }

  // Need to build and factorize for this state
  if (!buildAndFactorizeDual(dt, stateIdx)) {
    // Fall back to regular solve
    return solveStep(dt, state);
  }

  dualCachedDt_ = dt;
  return solveDualCachedStep(dt, stateIdx, state);
}

inline bool TransientSolver::buildAndFactorizeDual(double dt, int stateIdx) {
  // Create or reuse cached MNA system for this state
  if (!dualCachedMna_[stateIdx]) {
    dualCachedMna_[stateIdx] = std::make_unique<MnaSystem>(netCount_);
  }

  // Prepare factorized workspace
  if (!dualFactorizedWs_[stateIdx].isFactorized()) {
    dualFactorizedWs_[stateIdx].prepare(netCount_ + 16);
  }

  dualCachedMna_[stateIdx]->clear();

  // Stamp static elements
  invokeStampCallback(*dualCachedMna_[stateIdx], time_ + dt);

  // Stamp companion conductances
  companions_.stampConductanceAll(*dualCachedMna_[stateIdx], dt, method_);

  // Factorize
  bool ok = dualCachedMna_[stateIdx]->factorize(dualFactorizedWs_[stateIdx]);

  return ok;
}

inline TransientStatus TransientSolver::solveDualCachedStep(double dt, int stateIdx,
                                                            TransientState& state) {
  auto& mna = *dualCachedMna_[stateIdx];
  auto& ws = dualFactorizedWs_[stateIdx];

  // Clear and re-stamp (matrix structure unchanged)
  mna.clear();

  invokeStampCallback(mna, time_ + dt);

  companions_.stampAll(mna, dt, method_);

  // Ensure output arrays are sized
  if (state.nodeVoltages.size() < netCount_) {
    state.nodeVoltages.resize(netCount_);
  }
  if (state.branchCurrents.size() < mna.voltageSourceCount()) {
    state.branchCurrents.resize(mna.voltageSourceCount());
  }

  // Solve using cached LU (O(n^2) back-substitution)
  bool ok = mna.solveFactorized(ws, state.nodeVoltages.data(), state.branchCurrents.data());

  if (!ok) {
    return solveStep(dt, state);
  }

  state.time = time_ + dt;

  // Track previous voltages for stateful stamp callbacks
  prevVoltages_ = state.nodeVoltages;

  companions_.updateAll(state.nodeVoltages, dt);
  time_ += dt;

  return TransientStatus::SUCCESS;
}

/* ----------------------------- Run Simulation ----------------------------- */

inline TransientResult TransientSolver::run(const TransientConfig& config, bool recordHistory) {
  TransientResult result;

  // Validate configuration
  if (config.tStep <= 0.0 || config.tEnd <= config.tStart) {
    result.success = false;
    result.errorMessage = "Invalid time configuration";
    return result;
  }

  // Set integration method from configuration
  method_ = config.method;

  // Reset time (but NOT companions - they hold initial conditions)
  time_ = config.tStart;

  // Initialize state
  TransientState state;
  state.resize(netCount_, 0);

  // Compute DC operating point if requested
  if (config.dcOpPoint) {
    TransientStatus dcStatus = computeDC(state);
    if (dcStatus != TransientStatus::SUCCESS) {
      result.success = false;
      result.errorMessage = "DC operating point solve failed";
      return result;
    }
  } else {
    // Initialize with zeros
    state.time = config.tStart;
    std::fill(state.nodeVoltages.begin(), state.nodeVoltages.end(), 0.0);
    std::fill(state.branchCurrents.begin(), state.branchCurrents.end(), 0.0);
  }

  // Reserve history if recording
  if (recordHistory) {
    std::size_t estimatedSteps = config.stepCount();
    result.history.reserve(estimatedSteps + 1);
    result.history.push_back(state); // Initial state
  }

  // Time-stepping loop
  double dt = config.tStep;

  while (time_ < config.tEnd) {
    // Adjust last step to hit end exactly
    if (time_ + dt > config.tEnd) {
      dt = config.tEnd - time_;
    }

    TransientStatus stepStatus = step(dt, state);

    if (stepStatus != TransientStatus::SUCCESS) {
      result.success = false;
      result.errorMessage = "Time step failed at t=" + std::to_string(time_);
      result.finalTime = time_;
      result.finalState = state;
      return result;
    }

    result.stepsTaken++;

    if (recordHistory) {
      result.history.push_back(state);
    }

    // Reset dt for next iteration
    dt = config.tStep;
  }

  result.success = true;
  result.finalTime = time_;
  result.finalState = std::move(state);

  return result;
}

} // namespace sim::electronics::algorithms::transient

#endif // APEX_TRANSIENTSOLVER_TPP
