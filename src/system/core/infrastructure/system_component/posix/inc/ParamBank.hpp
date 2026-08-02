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
 * Rollback holds one level of history and staging a validated set over
 * the bank the rollback index references forfeits it: two banks cannot
 * hold three states. Callers that need the old set after staging a new
 * one must read active() first.
 *
 * Reader protection is a seqlock over atomic-word bank storage: the
 * writer brackets every bank write with an odd/even generation, and
 * active() copies words then revalidates the generation, retrying if a
 * write overlapped. A reader preempted mid-copy across a full
 * apply-then-load cycle therefore never returns torn data, and every
 * access is atomic at word granularity -- race-free by construction, not
 * by suppression. Reads return by value; a reference into a bank could
 * not survive the writer's reuse of it.
 *
 * RT Constraints:
 *  - active(), canRollback(), generations: RT-safe, wait-free in the
 *    absence of a concurrent publish; bounded retries against the single
 *    control-plane writer otherwise.
 *  - load(), publishInitial(), apply(), rollback(): control-plane only,
 *    single-writer assumption (no concurrent control-plane calls).
 *
 * TParams must be trivially copyable (no heap members), with alignment
 * no stricter than the word storage.
 */

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
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
  static_assert(alignof(TParams) <= alignof(std::uint64_t),
                "TParams alignment must not exceed the word storage alignment");

  /// Bank storage in 64-bit words so every reader/writer access is atomic.
  static constexpr std::size_t WORDS = (sizeof(TParams) + 7U) / 8U;

public:
  /** @brief Both banks default-constructed; nothing staged or published. */
  ParamBank() noexcept : activeGen_(0), stagedGen_(0) {
    const TParams DEFAULTS{};
    storeBank(0, DEFAULTS);
    storeBank(1, DEFAULTS);
  }

  // Non-copyable, non-movable (atomic storage).
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
   * @note Validates before writing: a rejected set never touches bank
   *       storage. An accepted stage over the rollback source forfeits
   *       rollback (see file comment).
   */
  template <typename TValidator>
  [[nodiscard]] Status load(const TParams& params, TValidator&& validate) noexcept {
    static_assert(std::is_nothrow_invocable_r_v<bool, TValidator, const TParams&>,
                  "validator must be noexcept bool(const TParams&)");
    stagedGen_.fetch_add(1, std::memory_order_relaxed);

    // Validate before any bank write: a rejected set never touches
    // storage (and therefore never forfeits rollback).
    if (!std::forward<TValidator>(validate)(params)) {
      stagedValid_ = false;
      return Status::ERROR_LOAD_INVALID;
    }

    forfeitRollbackIfStagedIsPrev();
    storeBankSeqlocked(stagedIdx_, params);
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

    // Decode into a local: prelude or body failure never touches bank
    // storage, and an accepted payload goes through the same seqlocked
    // stage as a struct load.
    TParams incoming{};
    lastCheck_ = readTprmPayload(path, fullUid, incoming);
    if (lastCheck_ != TprmPayloadCheck::OK) {
      stagedValid_ = false;
      return Status::ERROR_LOAD_INVALID;
    }
    return load(incoming, std::forward<TValidator>(validate));
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
    activeIdx_.store(stagedIdx_, std::memory_order_release);
    stagedIdx_ ^= 1U;
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
    prevIdx_ = static_cast<int>(activeIdx_.load(std::memory_order_relaxed));
    activeIdx_.store(stagedIdx_, std::memory_order_release);
    stagedIdx_ ^= 1U;
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    return Status::SUCCESS;
  }

  /**
   * @brief Restore the previously active set (one level of history).
   * @return SUCCESS if restored; WARN_NOOP if no rollback is available.
   * @note NOT RT-safe: control-plane only.
   */
  [[nodiscard]] Status rollback() noexcept {
    if (prevIdx_ < 0) {
      return Status::WARN_NOOP;
    }
    const std::uint32_t PREV = static_cast<std::uint32_t>(prevIdx_);
    activeIdx_.store(PREV, std::memory_order_release);
    stagedIdx_ = PREV ^ 1U;
    prevIdx_ = -1;
    activeGen_.fetch_add(1, std::memory_order_relaxed);
    return Status::SUCCESS;
  }

  /* ----------------------------- Reads ----------------------------- */

  /**
   * @brief Consistent copy of the current active parameter set.
   * @return The published set by value; a reference into bank storage
   *         could not survive the writer's eventual reuse of the bank.
   * @note RT-safe: word-wise atomic copy under seqlock validation.
   *       Wait-free when no publish is in flight; bounded retries against
   *       the single control-plane writer otherwise.
   */
  [[nodiscard]] TParams active() const noexcept {
    std::uint64_t words[WORDS];
    for (;;) {
      const std::uint32_t S0 = seq_.load(std::memory_order_acquire);
      if ((S0 & 1U) != 0U) {
        continue; // A bank write is in flight; its bracket is short.
      }
      const std::uint32_t IDX = activeIdx_.load(std::memory_order_acquire);
      for (std::size_t w = 0; w < WORDS; ++w) {
        words[w] = banks_[IDX][w].load(std::memory_order_relaxed);
      }
      std::atomic_thread_fence(std::memory_order_acquire);
      if (seq_.load(std::memory_order_relaxed) == S0) {
        break; // No writer overlapped the copy.
      }
    }
    TParams out;
    std::memcpy(&out, words, sizeof(TParams));
    return out;
  }

  /**
   * @brief Copy of the staged parameter set (preview before publish).
   * @note Single-writer assumption; call from the control plane only.
   */
  [[nodiscard]] TParams staged() const noexcept {
    std::uint64_t words[WORDS];
    for (std::size_t w = 0; w < WORDS; ++w) {
      words[w] = banks_[stagedIdx_][w].load(std::memory_order_relaxed);
    }
    TParams out;
    std::memcpy(&out, words, sizeof(TParams));
    return out;
  }

  /** @brief True if rollback() can restore a previous set. @note RT-safe. */
  [[nodiscard]] bool canRollback() const noexcept { return prevIdx_ >= 0; }

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
  /** @brief Plain word store (construction only, before any reader exists). */
  void storeBank(std::uint32_t idx, const TParams& src) noexcept {
    std::uint64_t words[WORDS] = {};
    std::memcpy(words, &src, sizeof(TParams));
    for (std::size_t w = 0; w < WORDS; ++w) {
      banks_[idx][w].store(words[w], std::memory_order_relaxed);
    }
  }

  /**
   * @brief Bank write under the seqlock bracket.
   *
   * The odd generation warns concurrent readers off; readers that copied
   * while the bracket was open revalidate and retry. Single writer, so
   * two increments bound the bracket.
   */
  void storeBankSeqlocked(std::uint32_t idx, const TParams& src) noexcept {
    seq_.fetch_add(1, std::memory_order_acq_rel); // odd: write in flight
    storeBank(idx, src);
    seq_.fetch_add(1, std::memory_order_release); // even: stable
  }

  /* An accepted load() writes the staged bank; when that bank is the
   * rollback source, the history it held is gone and canRollback() must
   * say so. */
  void forfeitRollbackIfStagedIsPrev() noexcept {
    if (prevIdx_ >= 0 && static_cast<std::uint32_t>(prevIdx_) == stagedIdx_) {
      prevIdx_ = -1;
    }
  }

  alignas(64) std::atomic<std::uint64_t> banks_[2][WORDS]; ///< Word-atomic bank storage.

  std::atomic<std::uint32_t> seq_{0};       ///< Seqlock generation (odd = write in flight).
  std::atomic<std::uint32_t> activeIdx_{0}; ///< Published bank index read by the RT side.
  std::uint32_t stagedIdx_{1};              ///< Writer-only staging bank index.
  int prevIdx_{-1};                         ///< Previous active bank for rollback (-1 = none).

  std::atomic<std::uint64_t> activeGen_;             ///< Successful publishes.
  std::atomic<std::uint64_t> stagedGen_;             ///< Staging attempts.
  bool stagedValid_{false};                          ///< Staged bank holds a validated set.
  TprmPayloadCheck lastCheck_{TprmPayloadCheck::OK}; ///< Verdict of the last file load.
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_PARAM_BANK_HPP
