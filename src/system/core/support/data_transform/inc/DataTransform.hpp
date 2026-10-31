#ifndef APEX_SUPPORT_DATA_TRANSFORM_HPP
#define APEX_SUPPORT_DATA_TRANSFORM_HPP
/**
 * @file DataTransform.hpp
 * @brief Support component for runtime data mutation via byte mask proxies.
 *
 * Provides a command-driven interface for arming, configuring, and applying
 * byte-level transforms to registered data blocks. Each transform entry
 * owns a ByteMaskProxy and targets a specific byte range resolved through
 * the registry.
 *
 * Use cases:
 *   - Fault injection (V&V campaigns)
 *   - Operator value overrides (test/debug)
 *   - Safing (force outputs to zero on fault detection)
 *
 * Mask application is command-driven. The action engine's sequencer
 * triggers it via COMMAND routing (APPLY_ENTRY or APPLY_ALL opcodes), and
 * ground operators can send the same commands directly via C2, so a mask
 * lands at exactly the tick the sequencer chooses.
 *
 * An applied mask is held: every hold tick writes it again, so a fault
 * written into a block that its owner rewrites each cycle outlives that
 * rewrite and a consumer that samples once per cycle, such as the action
 * engine's watchpoints, sees it. DISARM_ENTRY, CLEAR_MASKS and CLEAR_ALL
 * end the hold. The hold task runs only where the schedule lists it;
 * without it an apply is a single write.
 *
 * Ownership. Commands can run on the I/O thread (the interface's direct
 * dispatch) or on the task thread while a worker runs hold(), so one
 * mutex owns every write the component makes: the held copies, the
 * target bytes an apply or a hold writes, and the entry table's arming.
 * The command side locks it (APPLY_ENTRY, APPLY_ALL, DISARM_ENTRY,
 * CLEAR_MASKS, CLEAR_ALL, reset) and may therefore wait for one
 * in-progress hold tick, bounded by the tick's work; hold() only tries
 * the lock and skips the whole tick, counted, when a command holds it,
 * so a worker never blocks. When DISARM_ENTRY, CLEAR_MASKS or CLEAR_ALL
 * returns, no hold write of that entry is in progress and none follows
 * until a command applies again. The hold's own counters are atomics the
 * telemetry task reads; the hold never writes the statistics block.
 * Direct entry() configuration is preparation before the first APPLY;
 * after it, only commands change what a hold writes. The target block's
 * owner writes it from its own task: schedule hold() on the same single
 * worker after that task, as the action demo does, or sequence it behind
 * the owner's step; priority alone orders nothing across workers.
 *
 * A low-frequency telemetry task (1 Hz) reports health stats.
 *
 * componentId = 202  (support component range)
 *
 * @note handleCommand is RT-safe for all opcodes. init/reset are NOT RT-safe.
 */

#include "src/system/core/support/data_transform/inc/DataTransformData.hpp"
#include "src/system/core/support/data_transform/inc/FaultCampaignTprm.hpp"

#include "src/system/core/infrastructure/system_component/posix/inc/SupportComponentBase.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>

namespace system_core {
namespace support {

using system_core::system_component::Status;
using system_core::system_component::SupportComponentBase;

/* ----------------------------- ResolvedData ----------------------------- */

/**
 * @struct ResolvedData
 * @brief Result of resolving a DataTarget to a live byte pointer.
 */
struct ResolvedData {
  std::uint8_t* data{nullptr}; ///< Mutable pointer to data bytes.
  std::size_t size{0};         ///< Block size in bytes.
};

/// Delegate type for resolving DataTarget -> mutable byte pointer.
using TransformResolveDelegate =
    apex::concurrency::Delegate<ResolvedData, std::uint32_t, data::DataCategory>;

/* ----------------------------- DataTransform ----------------------------- */

/**
 * @class DataTransform
 * @brief Support component for byte-level data mutation.
 *
 * Command-driven: mask application happens when the action engine's
 * sequencer (or a ground operator) sends APPLY_ENTRY or APPLY_ALL.
 * No periodic apply task -- timing precision comes from the sequencer.
 *
 * Usage:
 * @code
 *   DataTransform transform;
 *   transform.setResolver(registryResolverFn, &registry);
 *   executive.registerSupport(&transform);
 *
 *   // Action engine sequence sends commands at the right time:
 *   //   Step 0 (cycle 500): CMD 0xCA00 ARM_ENTRY [0]
 *   //   Step 1 (cycle 500): CMD 0xCA00 PUSH_ZERO_MASK [0, 0, 0, 4]
 *   //   Step 2 (cycle 500): CMD 0xCA00 APPLY_ENTRY [0]
 *   //   Step 3 (cycle 600): CMD 0xCA00 DISARM_ENTRY [0]
 * @endcode
 */
class DataTransform final : public SupportComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 202;
  static constexpr const char* COMPONENT_NAME = "DataTransform";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "DATA_TRANSFORM"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    TELEMETRY = 1, ///< 1 Hz health stats logging.
    HOLD = 2       ///< Write every held mask again (the block owner's rate, after its step).
  };

  /* ----------------------------- Construction ----------------------------- */

  DataTransform() noexcept = default;
  ~DataTransform() override = default;

  /* ----------------------------- Wiring ----------------------------- */

  /**
   * @brief Set the resolver delegate for target resolution.
   * @param fn Function pointer: (fullUid, category) -> ResolvedData.
   * @param ctx Context pointer passed to fn.
   * @note Must be called before init().
   * @note RT-safe: stores pointer pair.
   */
  void setResolver(TransformResolveDelegate::Fn fn, void* ctx) noexcept { resolver_ = {fn, ctx}; }

  /**
   * @brief Set clock frequency for ATS time conversion.
   * @param hz Clock frequency in Hz (e.g., 10 for 10 Hz).
   * @note When a time provider is wired, ATS delayCycles are microseconds.
   *       This frequency is used to convert triggerCycle to microseconds.
   * @note Must be called before loadTprm().
   */
  void setClockFrequency(std::uint16_t hz) noexcept { clockFrequencyHz_ = hz; }

  /* ----------------------------- Scheduled Tasks ----------------------------- */

  /**
   * @brief Log health stats (1 Hz).
   * @return 0 on success.
   * @note NOT RT-safe: uses fmt::format for logging.
   */
  std::uint8_t telemetry() noexcept;

  /**
   * @brief Write every held mask again.
   *
   * Scheduled at the rate of the block owner's step and after it on the
   * same worker, so the owner's fresh write is masked again before the
   * action phase evaluates the block. Tries the component's lock: when a
   * command holds it the tick is skipped and counted. Under the lock it
   * reads the held copies, resolves each target through the registry and
   * writes at most MASK_MAX_LEN bytes per entry; failures count in the
   * hold's own atomic counters.
   *
   * @return 0 on success.
   * @note RT-safe: try-lock, bounded by the entry table, no logging.
   */
  std::uint8_t hold() noexcept;

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get current stats.
   * @return Reference to stats counters.
   * @note RT-safe: direct member access.
   */
  [[nodiscard]] const TransformStats& stats() const noexcept { return stats_; }

  /// Masks written by hold ticks so far.
  [[nodiscard]] std::uint32_t holdApplies() const noexcept {
    return holdApplies_.load(std::memory_order_relaxed);
  }
  /// Hold ticks skipped because a command held the lock.
  [[nodiscard]] std::uint32_t holdSkips() const noexcept {
    return holdSkips_.load(std::memory_order_relaxed);
  }
  /// Hold ticks that could not resolve a held target.
  [[nodiscard]] std::uint32_t holdResolveFailures() const noexcept {
    return holdResolveFailures_.load(std::memory_order_relaxed);
  }

  /**
   * @brief Get transform entry table.
   * @return Reference to entry array.
   * @note RT-safe: direct member access.
   */
  [[nodiscard]] const std::array<TransformEntry, TRANSFORM_MAX_ENTRIES>& entries() const noexcept {
    return entries_;
  }

  /**
   * @brief Get mutable entry for direct configuration (test/debug).
   *
   * Preparation before that entry's first APPLY: a target or mask written
   * here is not synchronized against hold(), which writes only what a
   * command applied. After an APPLY, change the entry through commands.
   *
   * @param index Entry index.
   * @return Pointer to entry, or nullptr if out of bounds.
   * @note RT-safe.
   */
  TransformEntry* entry(std::size_t index) noexcept {
    return (index < TRANSFORM_MAX_ENTRIES) ? &entries_[index] : nullptr;
  }

  /* ----------------------------- Command Interface ----------------------------- */

  /**
   * @brief Handle ground commands.
   * @param opcode Command opcode (DataTransformOpcode range).
   * @param payload Command payload bytes.
   * @param response Output response buffer.
   * @return Status code.
   * @note NOT RT-safe: APPLY, DISARM and CLEAR take the component's lock
   *       and may wait for one in-progress hold tick.
   */
  std::uint8_t handleCommand(std::uint16_t opcode, apex::compat::rospan<std::uint8_t> payload,
                             std::vector<std::uint8_t>& response) noexcept override;

  /**
   * @brief Load tunable parameters from TPRM binary file.
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success.
   * @note NOT RT-safe: File I/O.
   */
  system_component::TprmIngest loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /**
   * @brief Send LOAD_ATS command to action engine via internal bus.
   *
   * Called after the bus is wired. Sends a command to the action engine
   * to load the ATS file generated during loadTprm().
   *
   * @note NOT RT-safe. Called once during executive init.
   */
  void onBusReady() noexcept override;

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  std::uint8_t doInit() noexcept override;
  void doReset() noexcept override;

private:
  /**
   * @brief Resolve target and apply front mask for a single entry.
   * @param index Entry index.
   * @return true if mask was applied successfully.
   * @note RT-safe: O(mask length).
   */
  bool applyEntry(std::uint8_t index) noexcept;

  /**
   * @brief Build StandaloneSequenceTprm binary from fault campaign entries.
   * @param campaign Source campaign data.
   * @param outBinary Output buffer (1032 bytes).
   * @return true if at least one entry was translated.
   */
  bool buildFaultAts(const FaultCampaignTprm& campaign,
                     std::array<std::uint8_t, 1032>& outBinary) noexcept;

  std::array<TransformEntry, TRANSFORM_MAX_ENTRIES> entries_{};
  TransformStats stats_{};
  TransformResolveDelegate resolver_{};

  FaultCampaignTprm campaign_{};      ///< Loaded fault campaign (from TPRM).
  bool hasCampaign_{false};           ///< True if a fault campaign was loaded.
  std::filesystem::path atsPath_{};   ///< Path to generated ATS file.
  std::uint16_t clockFrequencyHz_{0}; ///< Clock frequency for cycle-to-us conversion.
  /// Owns every write this component makes: the held copies, the target
  /// bytes and the entry table's arming. Commands lock it; hold() tries it.
  std::mutex holdMutex_;
  std::atomic<std::uint32_t> holdApplies_{0}; ///< Masks written by hold ticks.
  std::atomic<std::uint32_t> holdSkips_{0};   ///< Hold ticks skipped: a command held the lock.
  std::atomic<std::uint32_t> holdResolveFailures_{
      0};                                           ///< Hold ticks that could not resolve a target.
  std::atomic<std::uint32_t> holdApplyFailures_{0}; ///< Held masks that did not fit the block.

  /// The copy of an applied mask and its target that hold() writes; written
  /// and read under holdMutex_ only.
  struct HeldMask {
    bool active{false};            ///< A commanded apply published the copy.
    std::uint32_t fullUid{0};      ///< Target block owner.
    data::DataCategory category{}; ///< Target block category.
    std::uint16_t byteOffset{0};   ///< Entry offset into the block.
    data_proxy::MaskEntry mask{};  ///< The applied mask (index, AND, XOR, len).
  };
  std::array<HeldMask, TRANSFORM_MAX_ENTRIES> held_{}; ///< One per entry.

  /// Publish entry index's target and front mask as its held copy (caller holds the lock).
  void publishHeld(std::uint8_t index) noexcept;
  /// Withdraw entry index's held copy (caller holds the lock).
  void withdrawHeld(std::uint8_t index) noexcept { held_[index].active = false; }
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_DATA_TRANSFORM_HPP
