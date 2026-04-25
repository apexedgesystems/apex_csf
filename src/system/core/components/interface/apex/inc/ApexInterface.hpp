#ifndef APEX_SYSTEM_CORE_INTERFACE_APEX_HPP
#define APEX_SYSTEM_CORE_INTERFACE_APEX_HPP
/**
 * @file ApexInterface.hpp
 * @brief Interface component with protocol-agnostic internal bus and APROTO external boundary.
 *
 * Design:
 * - Single TCP socket for bidirectional cmd/tlm traffic.
 * - Configurable framing: SLIP (default) or COBS.
 * - Internal bus is protocol-free: MessageBuffer carries payload + metadata fields.
 * - APROTO encoding/decoding happens only at the external boundary:
 *   - RX: Decode APROTO, strip header, route payload + metadata to components.
 *   - TX: Wrap payload + metadata in APROTO header, frame with SLIP/COBS, send.
 * - Scheduler calls InterfaceBase::pollSockets(timeoutMs) on cadence.
 *
 * Command Routing:
 * - System opcodes (0x0000-0x00FF) are handled locally (PING, GET_STATUS, etc.).
 * - Component opcodes (0x0100+) are routed to component via Registry lookup.
 * - ACK/NAK responses are built and queued for TX.
 */

#include "src/system/core/components/interface/apex/inc/ApexInterfaceTunables.hpp"
#include "src/system/core/components/interface/apex/inc/BufferPool.hpp"
#include "src/system/core/components/interface/apex/inc/ComponentQueues.hpp"
#include "src/system/core/components/interface/apex/inc/FileTransferHandler.hpp"
#include "src/system/core/components/interface/apex/inc/InterfaceBase.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoTypes.hpp"
#include "src/system/core/infrastructure/protocols/framing/cobs/inc/COBSFraming.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IInternalBus.hpp"

#include <cstdint>

#include <array>
#include <memory>

namespace system_core {

// Forward declarations
namespace system_component {
class IComponentResolver;
class SystemComponentBase;
} // namespace system_component

namespace interface {

/* ----------------------------- Constants ----------------------------- */

/// NAK status code for dropped frames due to framing errors.
inline constexpr std::uint8_t NAK_STATUS_FRAMES_DROPPED = 0x07;

/* ----------------------------- ApexInterface ----------------------------- */

/**
 * @class ApexInterface
 * @brief Concrete interface with configurable framing and APROTO command routing.
 *
 * Provides a single TCP socket for bidirectional command/telemetry traffic.
 * Framing protocol (SLIP or COBS) is configurable via tunables.
 * Commands are routed to registered components via the Registry.
 *
 * Also implements IInternalBus for internal component-to-component messaging.
 */
class ApexInterface : public InterfaceBase, public system_component::IInternalBus {
public:
  /// Maximum decoded frame size. Must accommodate the largest possible APROTO packet:
  /// APROTO header (14) + crypto metadata (8, optional) + file chunk payload (2 + chunk_size)
  /// + CRC (4). With default 4096-byte chunks this is 4124 bytes; 8192 gives headroom.
  static constexpr std::size_t FRAME_BUFFER_MAX = 8192;

  ApexInterface() noexcept = default;

  ~ApexInterface() override = default;

  /**
   * @brief Build a SocketConfiguration from tunables and configure the base.
   *        Creates a single TCP server socket with the configured framing.
   *        Caller must shutdown() before reconfiguring.
   */
  [[nodiscard]] Status configure(const ApexInterfaceTunables& t) noexcept;

  /**
   * @brief Load interface tunables from TPRM directory.
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success, false if load failed (uses defaults).
   * @note Reads "{componentId:06x}.tprm" (e.g., "000004.tprm") and calls configure().
   * @note NOT RT-safe: File I/O.
   */
  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override;

  /**
   * @brief Set component resolver for command routing.
   * @param resolver Pointer to component resolver (must remain valid during operation).
   * @note Call after configure() and before polling.
   * @note If not set, component routing returns ERROR_COMPONENT_NOT_FOUND.
   * @note The resolver is typically a RegistryBase or Executive adapter.
   */
  void setComponentResolver(system_component::IComponentResolver* resolver) noexcept {
    componentResolver_ = resolver;
  }

  /**
   * @brief Set filesystem root for file transfer operations.
   * @param fsRoot Root directory for file staging and delivery (typically .apex_fs/).
   * @note Call after configure() and before polling.
   * @note File transfer destinations are relative to this root.
   */
  void setFileSystemRoot(const std::filesystem::path& fsRoot) noexcept {
    fileTransfer_ = FileTransferHandler(fsRoot); // Move-assign from temporary.
  }

  /**
   * @brief Get the configured framing type.
   * @return Current framing type (SLIP or COBS).
   */
  [[nodiscard]] FramingType framingType() const noexcept { return tunables_.framing; }

  /**
   * @brief Handle commands addressed to the interface.
   * @param opcode Command opcode.
   * @param payload Command payload.
   * @param response Response payload (output).
   * @return 0 on success, nonzero error code on failure.
   * @note Handles system opcodes (NOOP, PING, GET_STATUS, RESET).
   */
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override;

  /* ----------------------------- Queue Management ----------------------------- */

  /**
   * @brief Allocate command/telemetry queues for a component.
   * @param fullUid Component's fullUid.
   * @return Pointer to allocated queues, or nullptr on failure.
   * @note NOT RT-safe: Call during registration, before freeze.
   */
  ComponentQueues* allocateQueues(std::uint32_t fullUid) noexcept {
    return queueMgr_.allocate(fullUid);
  }

  /**
   * @brief Get queues for a component.
   * @param fullUid Component's fullUid.
   * @return Pointer to queues, or nullptr if not found.
   * @note RT-safe.
   */
  [[nodiscard]] ComponentQueues* getQueues(std::uint32_t fullUid) noexcept {
    return queueMgr_.get(fullUid);
  }

  /**
   * @brief Freeze queue allocation (call after all components registered).
   * @note After freeze, allocateQueues() returns nullptr.
   */
  void freezeQueues() noexcept { queueMgr_.freeze(); }

  /**
   * @brief Drain telemetry from all component outboxes and transmit.
   * @note RT-safe. Call periodically (e.g., during pollSockets).
   */
  void drainTelemetryOutboxes() noexcept;

  /**
   * @brief Process pending commands for all registered components.
   * @param maxPerComponent Maximum commands to process per component (0 = unlimited).
   * @return Total number of commands processed across all components.
   * @note RT-safe. Call at start of scheduler tick before task execution.
   * @note Each component's processCommandQueue() is called via registry iteration.
   */
  std::size_t drainCommandsToComponents(std::size_t maxPerComponent = 0) noexcept;

  /* ----------------------------- Internal Messaging ----------------------------- */

  /**
   * @brief Post internal command to a component (model-to-model messaging).
   * @param srcFullUid Source component's fullUid.
   * @param dstFullUid Target component's fullUid.
   * @param opcode Command opcode.
   * @param payload Command payload.
   * @return true if queued successfully, false if queue full or component not found.
   * @note RT-safe: Lock-free queue push, no allocation.
   * @note Sets internalOrigin=1 flag to distinguish from external commands.
   * @note Implements IInternalBus interface.
   */
  bool postInternalCommand(std::uint32_t srcFullUid, std::uint32_t dstFullUid, std::uint16_t opcode,
                           apex::compat::rospan<std::uint8_t> payload) noexcept override;

  /**
   * @brief Post internal telemetry to external interface (broadcast to socket).
   * @param srcFullUid Source component's fullUid.
   * @param opcode Telemetry opcode.
   * @param payload Telemetry payload.
   * @return true if queued successfully.
   * @note RT-safe: Lock-free queue push, no allocation.
   * @note Sets internalOrigin=1 flag.
   * @note Implements IInternalBus interface.
   */
  bool postInternalTelemetry(std::uint32_t srcFullUid, std::uint16_t opcode,
                             apex::compat::rospan<std::uint8_t> payload) noexcept override;

  /**
   * @brief Post command to multiple components (multicast).
   * @param srcFullUid Source component's fullUid.
   * @param dstFullUids Span of target component fullUids.
   * @param opcode Command opcode.
   * @param payload Command payload (shared read-only by all recipients).
   * @return Number of components successfully queued.
   * @note RT-safe: Lock-free operations, zero-copy (single buffer, refcounted).
   * @note Implements IInternalBus interface.
   */
  std::size_t postMulticastCommand(std::uint32_t srcFullUid,
                                   apex::compat::rospan<std::uint32_t> dstFullUids,
                                   std::uint16_t opcode,
                                   apex::compat::rospan<std::uint8_t> payload) noexcept override;

  /**
   * @brief Post command to all registered components (broadcast).
   * @param srcFullUid Source component's fullUid.
   * @param opcode Command opcode.
   * @param payload Command payload (shared read-only by all recipients).
   * @return Number of components successfully queued.
   * @note RT-safe: Lock-free operations, zero-copy.
   * @note Excludes source component from recipients.
   * @note Implements IInternalBus interface.
   */
  std::size_t postBroadcastCommand(std::uint32_t srcFullUid, std::uint16_t opcode,
                                   apex::compat::rospan<std::uint8_t> payload) noexcept override;

protected:
  // Decode inbound bytes using configured framing.
  void processBytesRx(std::uint8_t serverId, apex::compat::rospan<std::uint8_t> rxData) override;

  // Produce encoded bytes using configured framing.
  std::size_t processBytesTx(std::uint8_t serverId,
                             apex::compat::mutable_bytes_span outBuf) override;

private:
  /* ----------------------------- APROTO Processing ----------------------------- */

  /**
   * @brief Process a complete decoded frame as an APROTO packet.
   * @param serverId Server that received the frame.
   * @param frame Decoded frame bytes (raw APROTO packet).
   * @note Validates packet, routes commands, builds responses.
   */
  void processAprotoPacket(std::uint8_t serverId,
                           apex::compat::rospan<std::uint8_t> frame) noexcept;

  /**
   * @brief Handle system-level opcodes (0x0000-0x00FF).
   * @param serverId Server for response.
   * @param view Validated packet view.
   * @return true if handled (response queued), false if not a system opcode.
   */
  bool handleSystemOpcode(std::uint8_t serverId,
                          const protocols::aproto::PacketView& view) noexcept;

  /**
   * @brief Route command to component via Registry.
   * @param serverId Server for response.
   * @param view Validated packet view.
   * @note Looks up component by fullUid, calls handleCommand(), queues ACK/NAK.
   */
  void routeToComponent(std::uint8_t serverId, const protocols::aproto::PacketView& view) noexcept;

  /**
   * @brief Build and enqueue an APROTO ACK/NAK response.
   * @param serverId Server for response.
   * @param cmdHeader Original command header.
   * @param statusCode Response status (0=ACK, nonzero=NAK error code).
   * @param responsePayload Optional response payload data.
   */
  void enqueueAckNak(std::uint8_t serverId, const protocols::aproto::AprotoHeader& cmdHeader,
                     std::uint8_t statusCode,
                     apex::compat::rospan<std::uint8_t> responsePayload = {}) noexcept;

  /**
   * @brief Send NAK for dropped frames if threshold reached.
   * @param serverId Server for response.
   * @note Called after framing errors. Sends NAK with dropped count in payload.
   */
  void reportDroppedFramesIfNeeded(std::uint8_t serverId) noexcept;

  /* ----------------------------- Data Members ----------------------------- */

  // Component resolver for lookup (not owned, must be set before polling).
  system_component::IComponentResolver* componentResolver_{nullptr};

  // RT-safe buffer pool for zero-copy messaging.
  BufferPool bufferPool_;

  // Per-component queue pairs for async routing.
  QueueManager queueMgr_;

  // Active tunables.
  ApexInterfaceTunables tunables_{};

  // Streaming decoder state (one active based on framing selection).
  struct SlipState {
    apex::protocols::slip::DecodeState st{};
    apex::protocols::slip::DecodeConfig cfg{};
  } slip_{};

  struct CobsState {
    apex::protocols::cobs::DecodeState st{};
    apex::protocols::cobs::DecodeConfig cfg{};
  } cobs_{};

  // Reusable scratch buffers for RX and TX paths.
  // Sized to FRAME_BUFFER_MAX (not GLOBAL_BUFFER_MAX) so decoded APROTO packets with large
  // payloads (e.g. 4096-byte file chunks + 14-byte header + CRC) fit without truncation.
  alignas(64) std::array<std::uint8_t, FRAME_BUFFER_MAX> frameBuf_{};
  alignas(64) std::array<std::uint8_t, FRAME_BUFFER_MAX> responseBuf_{};

  // Dropped frame tracking for framing error reporting.
  std::uint32_t droppedFrameCount_{0};

  // Internal messaging sequence counter.
  std::uint16_t internalSeq_{0};

  // File transfer handler for chunked file delivery over APROTO.
  FileTransferHandler fileTransfer_;

public:
  /* ----------------------------- Statistics ----------------------------- */

  /// Statistics counters for command/telemetry tracking.
  struct Stats {
    // External interface
    std::uint32_t packetsReceived{0}; ///< Valid APROTO packets received from socket.
    std::uint32_t packetsInvalid{0};  ///< Invalid packets (too small, parse error).
    std::uint32_t systemCommands{0};  ///< System opcodes handled (NOOP, PING, etc.).
    std::uint32_t routedCommands{0};  ///< Commands routed to components.
    std::uint32_t responsesQueued{0}; ///< ACK/NAK responses queued.
    std::uint32_t telemetryFrames{0}; ///< Telemetry frames transmitted to socket.
    std::uint32_t framingErrors{0};   ///< Framing decode errors (SLIP/COBS).

    // Internal bus
    std::uint32_t internalCommandsSent{0};  ///< Internal commands posted (model-to-model).
    std::uint32_t internalTelemetrySent{0}; ///< Internal telemetry posted (model-to-external).
    std::uint32_t internalCommandsFailed{
        0};                                 ///< Internal commands failed (queue full or not found).
    std::uint32_t multicastCommandsSent{0}; ///< Multicast commands posted.
    std::uint32_t broadcastCommandsSent{0}; ///< Broadcast commands posted.

    // Queue health
    std::uint32_t cmdQueueOverflows{0}; ///< Command inbox full events.
    std::uint32_t tlmQueueOverflows{0}; ///< Telemetry outbox full events.
  };

  /** @brief Get current statistics (read-only). */
  [[nodiscard]] const Stats& stats() const noexcept { return stats_; }

  /** @brief Log statistics summary. Call during shutdown. */
  void logStatsSummary() noexcept;

private:
  Stats stats_{};
};

} // namespace interface
} // namespace system_core

#endif // APEX_SYSTEM_CORE_INTERFACE_APEX_HPP
