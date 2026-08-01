#ifndef APEX_SYSTEM_COMPONENT_PARAM_BANK_HPP
#define APEX_SYSTEM_COMPONENT_PARAM_BANK_HPP
/**
 * @file ParamBank.hpp
 * @brief A/B parameter staging as a composable member.
 *
 * Lock-free double-banked parameter storage any component can own as a
 * member — schedulable components included. One bank is published to
 * readers through an atomic pointer; the other is the writer's staging
 * area. Publishing is a pointer swap, so real-time readers never observe
 * a torn parameter set and never take a lock.
 *
 * Lifecycle (control-plane, single-writer):
 *  1. load(params|path, validate)  - stage + validate a candidate set.
 *  2. publishInitial()             - first publish, before the RT phase.
 *  3. load(...) then apply()       - hot-reload without re-init.
 *  4. rollback()                   - restore the previous active set.
 *
 * Rollback holds one level of history and staging forfeits it: two banks
 * cannot hold three states, so a load() that scribbles the bank the
 * rollback pointer references clears canRollback(). Callers that need
 * the old set after staging a new one must read active() first.
 *
 * RT Constraints:
 *  - active(), canRollback(), generations: RT-safe, O(1), no allocation.
 *  - load(), publishInitial(), apply(), rollback(): control-plane only,
 *    single-writer assumption (no concurrent control-plane calls).
 *
 * TParams must be trivially copyable (no heap members) so bank copies
 * are memcpy and the RT reader can treat the published set as plain data.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <type_traits>

namespace system_core {
namespace system_component {

/* ----------------------------- ParamBank ----------------------------- */

/**
 * @class ParamBank
 * @brief Double-banked parameter storage with atomic publish.
 *
 * @tparam TParams  POD/aggregate parameter set (trivially copyable).
 *
 * Owned as a member; the owning component decides what a valid parameter
 * set is by passing a validator to load(), and wires status codes into
 * its own diagnostics.
 */
template <typename TParams> class ParamBank {
  static_assert(std::is_trivially_copyable_v<TParams>,
                "TParams must be trivially copyable for RT-safe staging (no heap members)");

public:
  /** @brief Both banks default-constructed; nothing staged or published. */
  ParamBank() noexcept : active_(&bankA_), staged_(&bankB_), activeGen_(0), stagedGen_(0) {}

  // Non-copyable, non-movable (atomics + self-referential bank pointers).
  ParamBank(const ParamBank&) = delete;
  ParamBank& operator=(const ParamBank&) = delete;
  ParamBank(ParamBank&&) = delete;
  ParamBank& operator=(ParamBank&&) = delete;

  /* ----------------------------- Staging ----------------------------- */

  /**
   * @brief Stage a parameter set from a struct.
   * @param params   Candidate set (copied into the staged bank).
   * @param validate Callable bool(const TParams&) noexcept; rejects the set.
   * @return Status::SUCCESS or Status::ERROR_LOAD_INVALID.
   * @note NOT RT-safe: control-plane only.
   * @note Scribbles the staged bank even on rejection; forfeits rollback
   *       if the staged bank is the rollback source (see file comment).
   */
  template <typename TValidator>
  [[nodiscard]] Status load(const TParams& params, TValidator&& validate) noexcept {
    static_assert(std::is_nothrow_invocable_r_v<bool, TValidator, const TParams&>,
                  "validator must be noexcept bool(const TParams&)");
    forfeitRollbackIfStagedIsPrev();
    *staged_ = params;
    stagedGen_.fetch_add(1, std::memory_order_relaxed);

    if (!std::forward<TValidator>(validate)(*staged_)) {
      stagedValid_ = false;
      return Status::ERROR_LOAD_INVALID;
    }
    stagedValid_ = true;
    return Status::SUCCESS;
  }

  /** @brief Stage a parameter set from a struct with no validation. */
  [[nodiscard]] Status load(const TParams& params) noexcept {
    return load(params, [](const TParams&) noexcept { return true; });
  }

  /**
   * @brief Stage a parameter set from a v3 payload file.
   * @param path     Payload file ({fullUid:06x}.tprm): 20-byte prelude +
   *                 sizeof(TParams) body (see TprmPayload.hpp).
   * @param fullUid  Target the payload must declare; any prelude check
   *                 failing rejects the stage (lastCheck() carries which).
   * @param validate Callable bool(const TParams&) noexcept.
   * @return Status::SUCCESS or Status::ERROR_LOAD_INVALID.
   * @note NOT RT-safe: file I/O, control-plane only.
   */
  template <typename TValidator>
  [[nodiscard]] Status load(const std::filesystem::path& path, std::uint32_t fullUid,
                            TValidator&& validate) noexcept {
    static_assert(std::is_nothrow_invocable_r_v<bool, TValidator, const TParams&>,
                  "validator must be noexcept bool(const TParams&)");
    forfeitRollbackIfStagedIsPrev();

    lastCheck_ = readTprmPayload(path, fullUid, *staged_);
    if (lastCheck_ != TprmPayloadCheck::OK) {
      stagedValid_ = false;
      return Status::ERROR_LOAD_INVALID;
    }
    stagedGen_.fetch_add(1, std::memory_order_relaxed);

    if (!std::forward<TValidator>(validate)(*staged_)) {
      stagedValid_ = false;
      return Status::ERROR_LOAD_INVALID;
    }
    stagedValid_ = true;
    return Status::SUCCESS;
  }

  /** @brief Stage a parameter set from a v3 payload file, no validation. */
  [[nodiscard]] Status load(const std::filesystem::path& path, std::uint32_t fullUid) noexcept {
    return load(path, fullUid, [](const TParams&) noexcept { return true; });
  }

  /** @brief Prelude verdict of the last file load. @note RT-safe. */
  [[nodiscard]] TprmPayloadCheck lastCheck() const noexcept { return lastCheck_; }

  /* ----------------------------- Publishing ----------------------------- */

  /**
   * @brief First publish: staged becomes active, before the RT phase.
   * @return SUCCESS on the first publish; WARN_NOOP if already published;
   *         ERROR_NOT_CONFIGURED unless the staged bank holds a validated
   *         set (never staged, or the last staging attempt was rejected).
   * @note No rollback exists after the initial publish (nothing to roll
   *       back to). NOT RT-safe: control-plane only.
   */
  [[nodiscard]] Status publishInitial() noexcept {
    if (!stagedValid_) {
      return Status::ERROR_NOT_CONFIGURED;
    }
    if (activeGen_.load(std::memory_order_relaxed) != 0) {
      return Status::WARN_NOOP;
    }
    active_.store(staged_, std::memory_order_release);
    staged_ = otherBank(staged_);
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    return Status::SUCCESS;
  }

  /**
   * @brief Hot-reload: publish the staged set, remember the old one.
   * @return SUCCESS; ERROR_NOT_CONFIGURED unless the staged bank holds a
   *         validated set — a rejected load() cannot be published.
   * @note Publishes staged -> active atomically (release store), then
   *       flips the staging pointer. NOT RT-safe: control-plane only.
   */
  [[nodiscard]] Status apply() noexcept {
    if (!stagedValid_) {
      return Status::ERROR_NOT_CONFIGURED;
    }
    prev_ = active_.load(std::memory_order_relaxed);
    active_.store(staged_, std::memory_order_release);
    staged_ = otherBank(staged_);
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    return Status::SUCCESS;
  }

  /**
   * @brief Restore the previously active set (one level of history).
   * @return SUCCESS if restored; WARN_NOOP if no rollback is available.
   * @note NOT RT-safe: control-plane only.
   */
  [[nodiscard]] Status rollback() noexcept {
    if (prev_ == nullptr) {
      return Status::WARN_NOOP;
    }
    active_.store(prev_, std::memory_order_release);
    staged_ = otherBank(prev_);
    prev_ = nullptr;
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    return Status::SUCCESS;
  }

  /* ----------------------------- Reads ----------------------------- */

  /**
   * @brief Current active parameter set.
   * @return Reference to the published bank (stable for this read).
   * @note RT-safe: single atomic acquire load.
   */
  [[nodiscard]] const TParams& active() const noexcept {
    return *active_.load(std::memory_order_acquire);
  }

  /**
   * @brief Staged parameter set (preview before publish).
   * @note Single-writer assumption; not safe to read concurrently with
   *       a control-plane load().
   */
  [[nodiscard]] const TParams& staged() const noexcept { return *staged_; }

  /** @brief True if rollback() can restore a previous set. @note RT-safe. */
  [[nodiscard]] bool canRollback() const noexcept { return prev_ != nullptr; }

  /** @brief True while the staged bank holds a validated set. @note RT-safe. */
  [[nodiscard]] bool isLoaded() const noexcept { return stagedValid_; }

  /** @brief Count of successful publishes (initial/apply/rollback). @note RT-safe. */
  [[nodiscard]] std::uint64_t activeGeneration() const noexcept {
    return activeGen_.load(std::memory_order_relaxed);
  }

  /** @brief Count of staging attempts (load calls that wrote a bank). @note RT-safe. */
  [[nodiscard]] std::uint64_t stagedGeneration() const noexcept {
    return stagedGen_.load(std::memory_order_relaxed);
  }

private:
  [[nodiscard]] TParams* otherBank(const TParams* bank) noexcept {
    return (bank == &bankA_) ? &bankB_ : &bankA_;
  }

  /* A load() writes the staged bank; when that bank is the rollback
   * source, the history it held is gone and canRollback() must say so. */
  void forfeitRollbackIfStagedIsPrev() noexcept {
    if (staged_ == prev_) {
      prev_ = nullptr;
    }
  }

  alignas(64) TParams bankA_{}; ///< Bank storage (initial publish target).
  alignas(64) TParams bankB_{}; ///< Bank storage (initial staging area).

  std::atomic<const TParams*> active_; ///< Published pointer read by the RT side.
  TParams* staged_;                    ///< Writer-only pointer to the staging bank.
  const TParams* prev_{nullptr};       ///< Previous active bank for rollback.

  std::atomic<std::uint64_t> activeGen_;             ///< Successful publishes.
  std::atomic<std::uint64_t> stagedGen_;             ///< Staging attempts.
  bool stagedValid_{false};                          ///< Staged bank holds a validated set.
  TprmPayloadCheck lastCheck_{TprmPayloadCheck::OK}; ///< Verdict of the last file load.
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_PARAM_BANK_HPP
