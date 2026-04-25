#ifndef APEX_SYSTEM_CORE_ACTION_ACTIONCOMPONENT_HPP
#define APEX_SYSTEM_CORE_ACTION_ACTIONCOMPONENT_HPP
/**
 * @file ActionComponent.hpp
 * @brief Core component wrapping ActionInterface for executive ownership.
 *
 * ActionComponent is a CoreComponentBase that owns an ActionInterface and
 * provides managed lifecycle (init, reset, processCycle). The executive
 * registers this component and calls tick() each scheduler frame.
 *
 * Wiring:
 *   1. Executive creates ActionComponent.
 *   2. Executive connects resolver delegate (registry lookup).
 *   3. Executive connects commandHandler delegate (bus routing).
 *   4. Executive calls init().
 *   5. If action engine TPRM exists, loadTprm() populates tables.
 *   6. Each frame, executive calls tick(currentCycle).
 *
 * The component exposes the underlying ActionInterface for direct table
 * configuration (watchpoints, groups, sequences, notifications, actions).
 *
 * All public methods are RT-safe unless noted otherwise.
 */

#include "src/system/core/components/action/apex/inc/ActionComponentStatus.hpp"
#include "src/system/core/components/action/apex/inc/ActionInterface.hpp"
#include "src/system/core/components/action/apex/inc/ResourceCatalog.hpp"
#include "src/system/core/components/action/apex/inc/SequenceCatalog.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace system_core {
namespace action {

struct ActionEngineTprm;       // Forward declaration for TPRM deserialization.
struct StandaloneSequenceTprm; // Forward declaration for RTS/ATS loading.

/* ----------------------------- ActionComponent ----------------------------- */

/**
 * @class ActionComponent
 * @brief Core infrastructure component for runtime action orchestration.
 *
 * Owns an ActionInterface and drives its processCycle() each frame.
 * Single-instance, managed directly by the executive.
 */
class ActionComponent : public system_component::CoreComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  /// Component type identifier (5 = Action, system component range 1-100).
  static constexpr std::uint16_t COMPONENT_ID = 5;

  /// Component name for collision detection.
  static constexpr const char* COMPONENT_NAME = "Action";

  /** @brief Get component type identifier. */
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }

  /** @brief Get component name. */
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /** @brief Component label for diagnostics. */
  [[nodiscard]] const char* label() const noexcept override { return "ACTION"; }

  /* ----------------------------- Construction ----------------------------- */

  /** @brief Default constructor. Auto-configured (TPRM is optional). */
  ActionComponent() noexcept { setConfigured(true); }

  /* ----------------------------- Delegate Wiring ----------------------------- */

  /**
   * @brief Set the data resolver delegate.
   * @param fn Resolver function pointer.
   * @param ctx Resolver context (e.g., registry pointer).
   * @note Must be called before init().
   * @note RT-safe: Stores pointer pair.
   */
  void setResolver(data::DataResolveDelegate::Fn fn, void* ctx) noexcept {
    iface_.resolver = {fn, ctx};
  }

  /**
   * @brief Set the command handler delegate.
   * @param fn Command routing function pointer.
   * @param ctx Handler context (e.g., bus pointer).
   * @note Must be called before init().
   * @note RT-safe: Stores pointer pair.
   */
  void setCommandHandler(data::CommandDelegate::Fn fn, void* ctx) noexcept {
    iface_.commandHandler = {fn, ctx};
  }

  /* ----------------------------- Runtime ----------------------------- */

  /**
   * @brief Run one cycle of the action engine.
   *
   * Calls processCycle() and then dispatches built-in log notifications
   * for any events that fired this cycle (TPRM-driven, no callbacks needed).
   *
   * @param currentCycle Current scheduler cycle count.
   * @note RT-safe: Bounded by static table sizes.
   * @note Call once per scheduler frame from the executive.
   */
  void tick(std::uint32_t currentCycle) noexcept {
    // Snapshot invoke counts for log-based notifications before processing.
    std::array<std::uint32_t, data::EVENT_NOTIFICATION_TABLE_SIZE> prevCounts{};
    for (std::size_t i = 0; i < data::EVENT_NOTIFICATION_TABLE_SIZE; ++i) {
      prevCounts[i] = iface_.notifications[i].invokeCount;
    }

    data::processCycle(iface_, currentCycle);

    // Dispatch built-in log for notifications that fired this cycle.
    for (std::size_t i = 0; i < data::EVENT_NOTIFICATION_TABLE_SIZE; ++i) {
      const auto& NOTE = iface_.notifications[i];
      if (NOTE.invokeCount > prevCounts[i] && NOTE.hasLogMessage() && !NOTE.callback) {
        dispatchLogNotifications(NOTE.eventId, NOTE.invokeCount);
      }
    }
  }

  /* ----------------------------- Table Access ----------------------------- */

  /**
   * @brief Get mutable reference to the underlying ActionInterface.
   * @return Reference to the owned interface.
   * @note RT-safe: Direct member access.
   * @note Use for direct table configuration (watchpoints, sequences, etc.).
   */
  [[nodiscard]] data::ActionInterface& iface() noexcept { return iface_; }

  /**
   * @brief Get const reference to the underlying ActionInterface.
   * @return Const reference to the owned interface.
   * @note RT-safe: Direct member access.
   */
  [[nodiscard]] const data::ActionInterface& iface() const noexcept { return iface_; }

  /**
   * @brief Get engine statistics.
   * @return Const reference to cumulative stats.
   * @note RT-safe: Direct member access.
   */
  [[nodiscard]] const data::EngineStats& stats() const noexcept { return iface_.stats; }

  /* ----------------------------- TPRM ----------------------------- */

  /**
   * @brief Load action engine configuration from binary TPRM.
   *
   * Deserializes ActionEngineTprm into the live ActionInterface tables
   * (watchpoints, groups, sequences, notifications, timed actions).
   * Built-in log notifications use logLabel/logMessage/logSeverity
   * instead of requiring external callback delegates.
   *
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true if TPRM loaded successfully, false if not found or error.
   * @note NOT RT-safe: File I/O.
   */
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /* ----------------------------- RTS/ATS Loading ----------------------------- */

  /**
   * @brief Load a standalone RTS file into a sequence slot.
   * @param slot Sequence table index (0..SEQUENCE_TABLE_SIZE-1).
   * @param path Path to the .rts file.
   * @return true on success, false on error (invalid slot, file not found, parse error).
   * @note NOT RT-safe: File I/O.
   */
  [[nodiscard]] bool loadRts(std::uint8_t slot, const std::filesystem::path& path) noexcept;

  /**
   * @brief Load a standalone ATS file into a sequence slot.
   * @param slot Sequence table index (0..SEQUENCE_TABLE_SIZE-1).
   * @param path Path to the .ats file.
   * @return true on success, false on error.
   * @note NOT RT-safe: File I/O.
   */
  [[nodiscard]] bool loadAts(std::uint8_t slot, const std::filesystem::path& path) noexcept;

  /* ----------------------------- Catalog-Based Operations ----------------------------- */

  /**
   * @brief Scan filesystem directories and populate the sequence catalog.
   * @param rtsDir Path to rts/ directory.
   * @param atsDir Path to ats/ directory.
   * @return Total entries added.
   * @note NOT RT-safe: File I/O.
   */
  std::size_t scanCatalog(const std::filesystem::path& rtsDir,
                          const std::filesystem::path& atsDir) noexcept;

  /**
   * @brief Start an RTS by sequence ID (catalog lookup + cached load).
   *
   * Looks up the sequence in the catalog, checks blocking, finds a free
   * execution slot (or preempts a lower-priority sequence), loads the
   * cached binary via memcpy (no filesystem I/O), and starts execution.
   *
   * @param sequenceId Sequence ID to start.
   * @return Slot index where loaded, or 0xFF on failure.
   * @note RT-safe: O(log N) lookup + memcpy + deserialize. No I/O.
   */
  std::uint8_t startRtsById(std::uint16_t sequenceId) noexcept;

  /**
   * @brief Stop a running RTS by sequence ID.
   * @param sequenceId Sequence ID to stop.
   * @return true if found and stopped, false if not running.
   * @note RT-safe: O(SEQUENCE_TABLE_SIZE) scan.
   */
  bool stopRtsById(std::uint16_t sequenceId) noexcept;

  /**
   * @brief Load ATS entries from the catalog into execution slots.
   *
   * Iterates the catalog for ATS entries and loads them into the ATS
   * execution slot range (slots RTS_SLOT_COUNT .. SEQUENCE_TABLE_SIZE-1).
   * Entries with armed=true are started immediately.
   *
   * @return Number of ATS entries loaded.
   * @note NOT RT-safe: deserializes cached binaries.
   */
  std::uint8_t loadAtsFromCatalog() noexcept;

  /**
   * @brief Get the sequence catalog (read-only).
   * @return Const reference to the catalog.
   * @note RT-safe: Direct member access.
   */
  [[nodiscard]] const data::SequenceCatalog& catalog() const noexcept { return catalog_; }

  /* ----------------------------- Command Handling ----------------------------- */

  /**
   * @brief Handle ground commands directed to the action component.
   *
   * Slot-based opcodes (0x0500 range):
   *   - 0x0500: LOAD_RTS  (payload: u8 slot, char[63] filename)
   *   - 0x0501: START_RTS (payload: u8 slot)
   *   - 0x0502: STOP_RTS  (payload: u8 slot)
   *   - 0x0503: LOAD_ATS  (payload: u8 slot, char[63] filename)
   *   - 0x0504: START_ATS (payload: u8 slot)
   *   - 0x0505: STOP_ATS  (payload: u8 slot)
   *   - 0x0506: ABORT_ALL_RTS (no payload)
   *
   * ID-based opcodes (catalog lookup):
   *   - 0x0510: START_RTS_BY_ID  (payload: u16 sequenceId)
   *   - 0x0511: STOP_RTS_BY_ID   (payload: u16 sequenceId)
   *   - 0x0512: SET_PRIORITY      (payload: u16 sequenceId, u8 priority)
   *   - 0x0513: SET_BLOCKING      (payload: u16 seqId, u8 count, u16[] blockIds)
   *   - 0x0514: SET_ABORT_EVENT   (payload: u16 seqId, u16 abortEventId)
   *   - 0x0515: SET_EXCLUSION_GROUP (payload: u16 seqId, u8 group)
   *
   * Catalog/status opcodes:
   *   - 0x0520: RESCAN_CATALOG (no payload)
   *   - 0x0521: GET_CATALOG    (no payload, response: catalog dump)
   *   - 0x0522: GET_STATUS     (payload: u16 seqId, response: exec status)
   *
   * Resource catalog opcodes:
   *   - 0x0530: ACTIVATE_WP           (payload: u16 watchpointId)
   *   - 0x0531: DEACTIVATE_WP         (payload: u16 watchpointId)
   *   - 0x0532: ACTIVATE_GROUP        (payload: u16 groupId)
   *   - 0x0533: DEACTIVATE_GROUP      (payload: u16 groupId)
   *   - 0x0534: ACTIVATE_NOTIFICATION (payload: u16 notificationId)
   *   - 0x0535: DEACTIVATE_NOTIFICATION (payload: u16 notificationId)
   *
   * @note RT-safe for START/STOP/ABORT. NOT RT-safe for LOAD/RESCAN (I/O).
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override;

protected:
  /* ----------------------------- Lifecycle Hooks ----------------------------- */

  /**
   * @brief Initialize the action component.
   * @return Status code (SUCCESS on success, ERROR_NO_RESOLVER if no resolver set).
   * @note NOT RT-safe: Called once at boot.
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override;

  /**
   * @brief Reset all tables and counters.
   * @note NOT RT-safe: Called during reset phase.
   * @note Preserves resolver and commandHandler delegates.
   */
  void doReset() noexcept override;

private:
  /**
   * @brief Deserialize ActionEngineTprm into live tables.
   * @param tprm Loaded TPRM structure.
   */
  void deserializeTprm(const ActionEngineTprm& tprm) noexcept;

  /**
   * @brief Deserialize a standalone sequence into a specific slot.
   * @param slot Target sequence slot index.
   * @param tprm Loaded standalone sequence structure.
   * @param forceType Override type (RTS or ATS).
   */
  void deserializeStandaloneSequence(std::uint8_t slot, const StandaloneSequenceTprm& tprm,
                                     data::SequenceType forceType) noexcept;

  /**
   * @brief Dispatch built-in log notifications for a fired event.
   * @param eventId Event that fired.
   * @param fireCount Number of times the event has fired.
   * @note Logs to componentLog() at the configured severity.
   */
  void dispatchLogNotifications(std::uint16_t eventId, std::uint32_t fireCount) noexcept;

  data::ActionInterface iface_{};
  data::SequenceCatalog catalog_;         ///< Sequence catalog (metadata + cached binaries).
  data::WatchpointCatalog wpCatalog_;     ///< Watchpoint definitions.
  data::GroupCatalog grpCatalog_;         ///< Group definitions.
  data::NotificationCatalog noteCatalog_; ///< Notification definitions.
  std::filesystem::path catalogRtsDir_{}; ///< Stored rts/ path for rescan.
  std::filesystem::path catalogAtsDir_{}; ///< Stored ats/ path for rescan.
};

} // namespace action
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_ACTIONCOMPONENT_HPP
