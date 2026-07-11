#ifndef APEX_SUPPORT_SHM_RING_BRIDGE_HPP
#define APEX_SUPPORT_SHM_RING_BRIDGE_HPP
/**
 * @file ShmRingBridge.hpp
 * @brief Support component that publishes registered data to a
 *        shared-memory ring for visualization consumers.
 *
 * ShmRingBridge implements the Side A (owner) role of the wire format
 * documented in this lib's README (section 5). It opens a SPSC
 * shm region + named semaphore at init, resolves a configured source
 * DataTarget against the registry, and each `bridgeStep` tick memcpys
 * the source bytes into the next ring slot and signals the wakeup
 * semaphore. A 1 Hz `telemetry` task logs health stats.
 *
 * apex owns the wire format; the implementation here is independent of
 * any consumer, with no shared code or library. Consumers implement the
 * same self-described format on their own side.
 *
 * Use cases:
 *   - Stream simulation state to a UE5 visualization (rover_terrain,
 *     aircraft_atmo, satellite_orbit demos).
 *   - Stream model OUTPUT to any out-of-process consumer (recorder,
 *     web dashboard, second simulator) that speaks the wire format.
 *
 * Optional: if no consumer ever attaches, the producer keeps writing
 * happily; the ring's FULL state is observable via `pushes_failed_full`
 * but does not block or drop sim work.
 *
 * componentId = 203  (support component range)
 *
 * @note bridgeStep is RT-safe (memcpy + atomic store + sem_post).
 *       init / loadTprm / onBusReady / telemetry are NOT RT-safe.
 */

#include "src/system/core/support/shm_ring_bridge/inc/ShmRingBridgeData.hpp"

#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SupportComponentBase.hpp"

#include <cstdint>
#include <filesystem>

namespace system_core {
namespace support {

using system_core::system_component::Status;
using system_core::system_component::SupportComponentBase;

/* ----------------------------- ShmRingBridge ----------------------------- */

class ShmRingBridge final : public SupportComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 203;
  static constexpr const char* COMPONENT_NAME = "ShmRingBridge";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "SHM_RING_BRIDGE"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    BRIDGE_STEP = 1, ///< Publish one frame; signal wakeup.
    TELEMETRY = 2,   ///< 1 Hz health log.
  };

  /* ----------------------------- Construction ----------------------------- */

  ShmRingBridge() noexcept = default;
  ~ShmRingBridge() noexcept override;

  ShmRingBridge(const ShmRingBridge&) = delete;
  ShmRingBridge& operator=(const ShmRingBridge&) = delete;

  /* ----------------------------- Wiring ----------------------------- */

  /// Set the registry-lookup delegate that resolves source DataTargets to
  /// live byte pointers. Must be called before init(). Mirror of
  /// DataTransform::setResolver.
  void setResolver(BridgeResolveDelegate::Fn fn, void* ctx) noexcept { resolver_ = {fn, ctx}; }

  /* ----------------------------- Tunables / state accessors ----------------------------- */

  [[nodiscard]] system_core::data::TunableParam<ShmRingBridgeTunables>& tunables() noexcept {
    return tunables_;
  }
  [[nodiscard]] const ShmRingBridgeState& bridgeState() const noexcept { return state_.get(); }
  [[nodiscard]] const ShmRingBridgeTlm& telemetry() const noexcept { return telemetry_.get(); }

  /* ----------------------------- Tasks ----------------------------- */

  /// Publish one frame to the ring + signal the wakeup semaphore.
  /// Returns 0 on success (including the no-op cases: channel not open,
  /// source not resolved, ring FULL -- these increment counters but do
  /// NOT fail the tick).
  std::uint8_t bridgeStep() noexcept;

  /// 1 Hz health log line.
  std::uint8_t telemetryTick() noexcept;

  /* ----------------------------- Lifecycle (public per apex convention)
   * ----------------------------- */

  /// Load tunables from `{tprmDir}/{fullUid:06x}.tprm` (apex convention).
  [[nodiscard]] bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /// Called after the internal bus is wired and the registry is populated.
  /// Opens the shm region + semaphore (Side A role) and resolves the
  /// configured source DataTarget. Failures here log + leave the bridge
  /// in a "channel_open=0" state -- the sim keeps running, just nothing
  /// gets published.
  void onBusReady() noexcept override;

protected:
  std::uint8_t doInit() noexcept override;
  void doReset() noexcept override;

private:
  /* ----------------------------- Wire-format helpers ----------------------------- */

  /// Open the shm region + named semaphore as Side A. Validates the
  /// configured tunables (capacity is power-of-two, paths are absolute).
  /// Lays out the RingHeader + zeroes the cursors. Returns true on success.
  bool openChannel() noexcept;

  /// Resolve the configured source DataTarget through the registry
  /// resolver. Validates length matches payload_size. Returns true on
  /// success. Caches `source_ptr_` + `source_len_`.
  bool resolveSource() noexcept;

  /// Tear down shm + sem (idempotent). Side A unlinks both kernel objects
  /// so the next process start is clean.
  void closeChannel() noexcept;

  /* ----------------------------- Wire-format state ----------------------------- */

  void* mapping_ = nullptr;  ///< mmap base of the shm region.
  std::size_t map_size_ = 0; ///< Total mmap'd bytes.
  void* sem_ = nullptr;      ///< sem_t* (opaque; <semaphore.h> stays in .cpp).

  // Ring A (forward; apex -> consumer): slot bytes start at
  // `mapping_ + BRIDGE_RING_PRELUDE_BYTES`, cursors live in the prelude.
  void* prod_cursor_ = nullptr;   ///< std::atomic<uint64_t>* into mapping (Ring A).
  void* cons_cursor_ = nullptr;   ///< std::atomic<uint64_t>* into mapping (Ring A).
  std::uint8_t* slots_ = nullptr; ///< First slot byte (Ring A).

  // Ring B (reverse; consumer -> apex). Bound when sink_enabled=1.
  // Apex is the consumer; UE5 writes producer. Same prelude+slots layout
  // as Ring A, just starting after Region A's total bytes.
  void* rx_prod_cursor_ = nullptr;   ///< std::atomic<uint64_t>* (UE5 writes).
  void* rx_cons_cursor_ = nullptr;   ///< std::atomic<uint64_t>* (apex writes).
  std::uint8_t* rx_slots_ = nullptr; ///< First slot byte (Ring B).
  std::size_t rx_slot_size_ = 0;     ///< reverse_payload_size (slot bytes).

  const std::uint8_t* source_ptr_ = nullptr; ///< Cached resolved source pointer.
  std::size_t source_len_ = 0;               ///< Cached resolved source length.

  // Identity of the created shm object (fstat at openChannel), used to detect
  // an external unlink/replace of the path while the channel is live.
  std::uint64_t shm_ino_ = 0;
  std::uint64_t shm_dev_ = 0;

  BridgeResolveDelegate resolver_{};

  /// Drain at most one APROTO frame from Ring B, validate header,
  /// dispatch via the internal bus. Increments rx counters in state_.
  /// No-op if sink_enabled=0 or rx_slots_ unset.
  /// Called from bridgeStep after the Ring A push.
  void drainCommands() noexcept;

  /// Detect an external unlink/replace of the live shm path (probe by name,
  /// compare inode identity). Sets state.region_orphaned + logs once; when the
  /// path is gone entirely and orphan_reclaim is set, reopens the channel.
  /// Called from telemetryTick (non-RT) -- bridgeStep is untouched.
  void checkRegionOrphaned() noexcept;

  /* ----------------------------- Apex data registration ----------------------------- */

  system_core::data::TunableParam<ShmRingBridgeTunables> tunables_{};
  system_core::data::State<ShmRingBridgeState> state_{};
  system_core::data::Output<ShmRingBridgeTlm> telemetry_{};
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SHM_RING_BRIDGE_HPP
