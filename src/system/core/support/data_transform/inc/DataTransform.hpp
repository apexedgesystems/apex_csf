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
 * DataTransform is interrupt-driven, not scheduled. The action engine's
 * sequencer triggers mask application via COMMAND routing (APPLY_ENTRY or
 * APPLY_ALL opcodes). Ground operators can also send commands directly
 * via C2. This ensures mask application happens at exactly the right tick,
 * with timing precision inherited from the action engine's sequencer.
 *
 * A low-frequency telemetry task (1 Hz) reports health stats.
 *
 * componentId = 202  (support component range)
 *
 * @note handleCommand is RT-safe for all opcodes. init/reset are NOT RT-safe.
 */

#include "src/system/core/support/data_transform/inc/DataTransformData.hpp"
#include "src/system/core/support/data_transform/inc/FaultCampaignTprm.hpp"

#include "src/system/core/infrastructure/system_component/apex/inc/SupportComponentBase.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cstdint>
#include <filesystem>

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
    TELEMETRY = 1 ///< 1 Hz health stats logging.
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

  /* ----------------------------- Accessors ----------------------------- */

  /**
   * @brief Get current stats.
   * @return Reference to stats counters.
   * @note RT-safe: direct member access.
   */
  [[nodiscard]] const TransformStats& stats() const noexcept { return stats_; }

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
   * @note NOT RT-safe.
   */
  std::uint8_t handleCommand(std::uint16_t opcode, apex::compat::rospan<std::uint8_t> payload,
                             std::vector<std::uint8_t>& response) noexcept override;

  /**
   * @brief Load tunable parameters from TPRM binary file.
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success.
   * @note NOT RT-safe: File I/O.
   */
  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

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
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_DATA_TRANSFORM_HPP
