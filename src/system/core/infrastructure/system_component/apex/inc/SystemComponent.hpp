#ifndef APEX_SYSTEM_COMPONENT_HPP
#define APEX_SYSTEM_COMPONENT_HPP
/**
 * @file SystemComponent.hpp
 * @brief Templated system component with A/B parameter staging and file loading.
 *
 * Design:
 *  - Inherits SystemComponentBase for lifecycle (init, reset, status).
 *  - Adds A/B bank staging for lock-free RT parameter access.
 *  - load() reads binary params from file via hex2cpp.
 *  - apply() performs hot-reload by swapping staged → active.
 *  - TParams must be trivially copyable (enforced via static_assert).
 *
 * RT Lifecycle Constraints:
 *  - activeParams() is RT-safe (single atomic acquire load).
 *  - load(), apply(), init(), reset() are control-plane only.
 *  - Do not call control-plane ops concurrently (single-writer assumption).
 *
 * Lifecycle:
 *  1. ctor()       - Empty vessel, no params, cannot init.
 *  2. load(path)   - hex2cpp into staged bank (B), validate, mark configured.
 *  3. init()       - Fails if not configured. Applies staged → active, calls doInit().
 *  4. [RT phase]   - activeParams() is RT-safe for readers.
 *  5. apply()      - Hot-reload: load new params, swap A/B without re-init.
 *  6. reset()      - Clear state, allow re-init with same params.
 *
 * Derived must implement:
 *  - bool validateParams(const TParams&) const noexcept;
 *  - uint8_t doInit() noexcept;
 */

#include "src/system/core/infrastructure/system_component/apex/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>

namespace system_core {
namespace system_component {

/* ----------------------------- SystemComponent ----------------------------- */

/**
 * @class SystemComponent
 * @brief Templated component with A/B parameter staging and file loading.
 *
 * @tparam TParams  POD/aggregate parameters (trivially copyable, no heap members).
 *
 * Derived classes must implement:
 *  - bool validateParams(const TParams&) const noexcept;
 *  - uint8_t doInit() noexcept override;
 *
 * Optionally override:
 *  - void doReset() noexcept override;
 *  - const char* label() const noexcept override;
 *
 * @note RT-safe reads: activeParams(), stagedParams(), activeGeneration().
 * @note NOT RT-safe: load(), apply(), init(), reset() (control-plane, single-writer).
 */
template <typename TParams> class SystemComponent : public SystemComponentBase {
  static_assert(std::is_trivially_copyable_v<TParams>,
                "TParams must be trivially copyable for RT-safe staging (no heap members)");

public:
  /** @brief Default constructor. */
  SystemComponent() noexcept : active_(&bankA_), staged_(&bankB_), activeGen_(0), stagedGen_(0) {}

  /** @brief Virtual destructor. */
  ~SystemComponent() override = default;

  /**
   * @brief Load parameters from binary file into staged bank.
   * @param tprmPath Path to binary parameter file (.bin/.tprm).
   * @return Status::SUCCESS on success; ERROR_LOAD_INVALID on failure.
   * @note NOT RT-safe: File I/O, control-plane only.
   * @note On success, marks component as configured (ready for init).
   * @note Uses hex2cpp to load binary directly into TParams struct.
   */
  [[nodiscard]] std::uint8_t load(const std::filesystem::path& tprmPath) noexcept {
    std::string loadError;
    std::optional<std::reference_wrapper<std::string>> errorRef{loadError};

    if (!apex::helpers::files::hex2cpp(tprmPath.string(), *staged_, errorRef)) {
      setLastError(loadError.empty() ? "Failed to load params from file" : "File load error");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID));
      return status();
    }

    if (!validateParams(*staged_)) {
      setLastError("Params validation failed");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID));
      return status();
    }

    stagedGen_.fetch_add(1, std::memory_order_relaxed);
    setConfigured(true);
    setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
    return status();
  }

  /**
   * @brief Load parameters from struct into staged bank.
   * @param params Parameters to stage (copied into B).
   * @return Status::SUCCESS on success; ERROR_LOAD_INVALID on validation failure.
   * @note NOT RT-safe: Control-plane only.
   * @note On success, marks component as configured (ready for init).
   */
  [[nodiscard]] std::uint8_t load(const TParams& params) noexcept {
    *staged_ = params;

    if (!validateParams(*staged_)) {
      setLastError("Params validation failed");
      setStatus(static_cast<std::uint8_t>(Status::ERROR_LOAD_INVALID));
      stagedGen_.fetch_add(1, std::memory_order_relaxed);
      return status();
    }

    stagedGen_.fetch_add(1, std::memory_order_relaxed);
    setConfigured(true);
    setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
    return status();
  }

  /**
   * @brief Apply staged params to active bank (hot-reload).
   * @return Status::SUCCESS on success; ERROR_NOT_CONFIGURED if no staged params.
   * @note NOT RT-safe: Control-plane only, single-writer assumption.
   * @note Publishes staged → active atomically, then flips banks.
   * @note Does NOT re-initialize; use for runtime param updates.
   */
  [[nodiscard]] std::uint8_t apply() noexcept {
    if (!isConfigured()) {
      setStatus(static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
      return status();
    }

    prev_ = active_.load(std::memory_order_relaxed);
    active_.store(staged_, std::memory_order_release);
    staged_ = (staged_ == &bankA_) ? &bankB_ : &bankA_;
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
    return status();
  }

  /**
   * @brief Rollback to previous active parameters.
   * @return Status::SUCCESS if rollback performed; Status::WARN_NOOP if no previous state.
   * @note Only valid after at least one apply(). Restores previous active bank.
   */
  [[nodiscard]] std::uint8_t rollback() noexcept {
    if (prev_ == nullptr) {
      setStatus(static_cast<std::uint8_t>(Status::WARN_NOOP));
      return status();
    }
    active_.store(prev_, std::memory_order_release);
    staged_ = (prev_ == &bankA_) ? &bankB_ : &bankA_;
    prev_ = nullptr;
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
    return status();
  }

  /**
   * @brief Snapshot view of current active parameters.
   * @return Reference to active parameters (stable for this read).
   * @note RT-safe: Single atomic acquire load.
   */
  [[nodiscard]] const TParams& activeParams() const noexcept {
    const TParams* ptr = active_.load(std::memory_order_acquire);
    return *ptr;
  }

  /**
   * @brief Snapshot view of staged parameters (preview before apply).
   * @return Reference to staged parameters.
   * @note Single-writer assumption; not safe for concurrent reads during load().
   */
  [[nodiscard]] const TParams& stagedParams() const noexcept { return *staged_; }

  /** @brief True if rollback() can restore a previous configuration. */
  [[nodiscard]] bool canRollback() const noexcept { return prev_ != nullptr; }

  /** @brief Generation counter for successful publishes (apply/init swaps). */
  [[nodiscard]] std::uint64_t activeGeneration() const noexcept {
    return activeGen_.load(std::memory_order_relaxed);
  }

  /** @brief Generation counter for staging attempts (load calls). */
  [[nodiscard]] std::uint64_t stagedGeneration() const noexcept {
    return stagedGen_.load(std::memory_order_relaxed);
  }

protected:
  /**
   * @brief Validate parameters before accepting them.
   * @param params Parameters to validate.
   * @return true if valid; false to reject.
   * @note Called by load() after hex2cpp succeeds.
   * @note Derived classes implement validation logic here.
   */
  [[nodiscard]] virtual bool validateParams(const TParams& params) const noexcept = 0;

  /**
   * @brief Pre-init hook: apply staged params to active before doInit().
   * @note Called by base init() logic. Swaps staged → active.
   * @note Derived doInit() can then use activeParams().
   * @note Does NOT set prev_ during first init (no rollback available after just init).
   */
  void preInit() noexcept override {
    if (activeGen_.load(std::memory_order_relaxed) == 0) {
      // First init: swap staged → active (no prev_ - rollback not available after just init)
      active_.store(staged_, std::memory_order_release);
      staged_ = (staged_ == &bankA_) ? &bankB_ : &bankA_;
      activeGen_.fetch_add(1, std::memory_order_relaxed);
    }
  }

private:
  alignas(64) TParams bankA_{}; ///< Active bank storage (initial publish target).
  alignas(64) TParams bankB_{}; ///< Staged bank storage.

  std::atomic<const TParams*> active_; ///< Published pointer to active bank.
  TParams* staged_;                    ///< Writer-only pointer to staged bank.
  const TParams* prev_{nullptr};       ///< Previous active for rollback.

  std::atomic<std::uint64_t> activeGen_; ///< Count of successful publishes.
  std::atomic<std::uint64_t> stagedGen_; ///< Count of staging attempts.
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_HPP
