#ifndef APEX_SUPPORT_SHM_RING_BRIDGE_DATA_HPP
#define APEX_SUPPORT_SHM_RING_BRIDGE_DATA_HPP
/**
 * @file ShmRingBridgeData.hpp
 * @brief Tunables, state, and telemetry structs for the ShmRingBridge
 *        SUPPORT component.
 *
 * The bridge is a wire-format-compliant SPSC shared-memory writer
 * (see this lib's README, section 5). It opens a Channel as Side A,
 * resolves a source DataTarget against the registry once at init, then
 * each `bridgeStep` tick memcpys the source bytes into the next ring
 * slot and signals the wakeup semaphore.
 *
 * All structs trivially-copyable for TPRM compatibility.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/DataCategory.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cstddef>
#include <cstdint>

namespace system_core {
namespace support {

/* ----------------------------- Wire format constants ----------------------------- */

/// The framework version stamp written into every ring header.
inline constexpr std::uint16_t BRIDGE_FRAMEWORK_VERSION = 1u;

/// 4-byte magic stamped in every shm region (see the wire format,
/// README section 5.2). Fixed stamp, spelled "HRNB" little-endian.
inline constexpr std::uint32_t BRIDGE_FRAMEWORK_MAGIC = 0x48524E42u;

/// Per the wire format: header (64) + producer cursor (64) + consumer
/// cursor (64). Slots start at offset 192.
inline constexpr std::size_t BRIDGE_RING_HEADER_BYTES = 64u;
inline constexpr std::size_t BRIDGE_CURSOR_BYTES = 64u;
inline constexpr std::size_t BRIDGE_RING_PRELUDE_BYTES =
    BRIDGE_RING_HEADER_BYTES + 2u * BRIDGE_CURSOR_BYTES;

static_assert(BRIDGE_RING_PRELUDE_BYTES == 192u, "the wire format fixes the prelude at 192 bytes");

/* ----------------------------- Path lengths ----------------------------- */

/// Max length of an shm or sem path. POSIX caps at NAME_MAX (255) but
/// the bridge's typical paths are far shorter; 64 is plenty.
inline constexpr std::size_t BRIDGE_PATH_LEN = 64u;

/// Max length of the component label used in log lines.
inline constexpr std::size_t BRIDGE_LABEL_LEN = 16u;

/* ----------------------------- ShmRingBridgeTunables ----------------------------- */

/**
 * @struct ShmRingBridgeTunables
 * @brief TPRM-loadable configuration for one ShmRingBridge instance.
 *
 * 172 bytes; trivially copyable.
 */
struct ShmRingBridgeTunables {
  /* -- Wire-format identity (becomes app.{magic, version} in RingHeader) -- */
  std::uint32_t app_magic{0};   ///< e.g. 'ROVR' = 0x524F5652u for rover.
  std::uint16_t app_version{1}; ///< Bump on payload layout change.
  std::uint16_t reserved0{0};

  /* -- Ring geometry (must agree on both sides) -- */
  std::uint32_t capacity{1024};          ///< Slots per direction; power of two.
  std::uint32_t payload_size{0};         ///< Forward (apex -> consumer) slot size.
  std::uint32_t reverse_payload_size{0}; ///< Reverse (consumer -> apex) slot size.
                                         ///< 0 = same as payload_size (symmetric).
                                         ///< Demo apps that use a smaller "EmptyFrame"
                                         ///< for the unused reverse direction must set
                                         ///< this so the consumer's Channel<Out,In>
                                         ///< template instantiation matches the shm
                                         ///< region layout (else PAYLOAD_SIZE_MISMATCH
                                         ///< at attach time).

  /* -- Source: which registered byte range to publish each tick -- */
  std::uint32_t source_uid{0};     ///< Component fullUid (from registry).
  std::uint8_t source_category{0}; ///< DataCategory enum value (typically OUTPUT=4).
  std::uint8_t reserved1[3]{};
  std::uint16_t source_byte_offset{0}; ///< Start byte within the data block.
  std::uint16_t source_byte_len{0};    ///< Length to copy; 0 = whole block.

  /* -- command-sink (Ring B / UE5 -> apex) configuration. --
   *
   * When sink_enabled = 1, each bridgeStep pops at most one APROTO
   * application-layer frame from Ring B and dispatches it through the
   * internal bus (postInternalCommand) -- same routing path TCP/APROTO
   * commands take through ApexInterface. SLIP/COBS framing is NOT used;
   * the ring slot boundary IS the frame boundary.
   *
   * Per-tick processing limit of 1 frame keeps bridgeStep RT-bounded.
   * Bursts catch up on subsequent ticks since the lock-free internal
   * bus queue absorbs the slack.
   *
   * Default sink_enabled = 0: existing apps see no behavior change.
   * See the wire format (README section 5) for what Ring B carries
   * when this is enabled.
   */
  std::uint8_t sink_enabled{0};    ///< 1 = drain Ring B + dispatch; 0 = ignore.
  std::uint8_t sink_reserved[7]{}; ///< Future: per-opcode allowlist, src uid override, etc.

  /* -- POSIX paths for the shm region + wakeup semaphore -- */
  /// Absolute POSIX shm path (must start with '/'; max 64 chars incl NUL).
  char shm_path[BRIDGE_PATH_LEN]{};

  /// Absolute POSIX semaphore path. Empty = derive shm_path + "_wake"
  /// (the wire format's default; see README section 5.4).
  char wakeup_path[BRIDGE_PATH_LEN]{};

  /* -- Diagnostics -- */
  /// Tag for log lines. Default falls through to the component label.
  char label[BRIDGE_LABEL_LEN]{};
};

static_assert(sizeof(ShmRingBridgeTunables) ==
                  4 + 2 + 2           // magic + version + reserved0
                      + 4 + 4 + 4     // capacity + payload_size + reverse_payload_size
                      + 4 + 1 + 3     // source_uid + category + reserved1
                      + 2 + 2         // byte_offset + byte_len
                      + 1 + 7         // sink_enabled + reserved
                      + 64 + 64 + 16, // paths + label
              "ShmRingBridgeTunables layout drift");

/* ----------------------------- ShmRingBridgeState ----------------------------- */

/**
 * @struct ShmRingBridgeState
 * @brief Internal bookkeeping (STATE category).
 */
struct ShmRingBridgeState {
  std::uint64_t tick_count{0};         ///< bridgeStep invocations (incl skipped).
  std::uint64_t frames_published{0};   ///< Successful pushes (Ring A).
  std::uint64_t pushes_failed_full{0}; ///< Pushes refused because consumer fell behind.
  std::uint64_t signals_failed{0};     ///< sem_post failures (should be ~never).

  /* Ring B (command-sink) counters. */
  std::uint64_t cmds_received{0};        ///< APROTO frames popped from Ring B.
  std::uint64_t cmds_decode_errors{0};   ///< Frames rejected: bad magic / version / size.
  std::uint64_t cmds_dispatch_errors{0}; ///< postInternalCommand returned false.

  std::uint8_t channel_open{0};    ///< 1 once shm + sem are open.
  std::uint8_t source_resolved{0}; ///< 1 once the source DataTarget resolves.
  std::uint8_t reserved[6]{};
};

/* ----------------------------- ShmRingBridgeTlm ----------------------------- */

/**
 * @struct ShmRingBridgeTlm
 * @brief Health telemetry payload (OUTPUT category) refreshed each tick.
 * 32 bytes packed.
 */
struct __attribute__((packed)) ShmRingBridgeTlm {
  std::uint64_t frames_published{0};
  std::uint64_t pushes_failed_full{0};
  std::uint64_t signals_failed{0};
  std::uint8_t channel_open{0};
  std::uint8_t source_resolved{0};
  std::uint8_t reserved[6]{};
};

static_assert(sizeof(ShmRingBridgeTlm) == 32, "ShmRingBridgeTlm size drift");

/* ----------------------------- Source resolver ----------------------------- */

/**
 * @struct ResolvedSource
 * @brief Result of resolving a source DataTarget to a live byte pointer.
 *
 * Mirror of DataTransform's ResolvedData (kept local to avoid cross-
 * dependency between two SUPPORT modules). Same shape; different name
 * to avoid a confusing collision when both are used in one file.
 */
struct ResolvedSource {
  const std::uint8_t* data{nullptr}; ///< Read-only pointer to source bytes.
  std::size_t size{0};               ///< Block size in bytes.
};

/// Delegate that resolves (fullUid, category) to the registered source bytes.
/// Same shape as DataTransform's resolver; the executive wires both from
/// one registry-lookup helper.
using BridgeResolveDelegate =
    apex::concurrency::Delegate<ResolvedSource, std::uint32_t, data::DataCategory>;

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_SHM_RING_BRIDGE_DATA_HPP
