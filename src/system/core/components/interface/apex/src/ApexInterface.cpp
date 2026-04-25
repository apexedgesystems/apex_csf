/**
 * @file ApexInterface.cpp
 * @brief Implementation of ApexInterface with protocol-agnostic internal bus.
 *
 * APROTO encoding/decoding is confined to the external boundary:
 * - RX path: processAprotoPacket() decodes, routes payload + metadata internally.
 * - TX path: drainTelemetryOutboxes() wraps payload in APROTO for the wire.
 * - Internal messaging: payload-only buffers with metadata in struct fields.
 */

#include "src/system/core/components/interface/apex/inc/ApexInterface.hpp"
#include "src/system/core/components/interface/apex/inc/ComponentQueues.hpp"
#include "src/system/core/components/interface/apex/inc/InterfaceTlm.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/IComponentResolver.hpp"
#include "src/system/core/infrastructure/protocols/framing/cobs/inc/COBSFraming.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/utilities/compatibility/inc/compat_attributes.hpp"

#include <cstring>

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include <fmt/format.h>

namespace system_core {
namespace interface {

namespace aproto = protocols::aproto;

/* ----------------------------- ApexInterface Methods ----------------------------- */

Status ApexInterface::configure(const ApexInterfaceTunables& t) noexcept {
  tunables_ = t;

  // Log TPRM configuration.
  auto* log = componentLog();
  if (log != nullptr) {
    const char* framingStr = (t.framing == FramingType::SLIP) ? "SLIP" : "COBS";
    log->info(label(), "=== TPRM Configuration ===");
    log->info(label(), fmt::format("host={}, port={}", t.host.data(), t.port));
    log->info(label(), fmt::format("framing={}, crcEnabled={}", framingStr, t.crcEnabled));
    log->info(label(), fmt::format("cmdQueueCapacity={}, tlmQueueCapacity={}", t.cmdQueueCapacity,
                                   t.tlmQueueCapacity));
    log->info(label(), fmt::format("maxPayloadBytes={}, pollTimeoutMs={}", t.maxPayloadBytes,
                                   t.pollTimeoutMs));
    log->info(label(),
              fmt::format("droppedFrameReportThreshold={}", t.droppedFrameReportThreshold));
    log->info(label(), "==========================");
  }

  SocketConfiguration cfg{};
  cfg.maxIoBufferBytes = GLOBAL_BUFFER_MAX;

  // Single TCP endpoint with configured port.
  SocketEndpoint ep{};
  ep.transport = SocketTransport::TCP;
  ep.bindAddress = std::string(t.host.data());
  ep.port = t.port;

  cfg.endpoints.reserve(1);
  cfg.endpoints.push_back(std::move(ep));

  // Configure base (creates server).
  const Status ST = InterfaceBase::configureSockets(cfg);
  if (ST != Status::SUCCESS) {
    return ST;
  }

  // Decoder frame-size caps must accommodate the largest APROTO packet (header + payload + CRC),
  // which exceeds the I/O buffer size when file chunks use the full I/O buffer for data alone.
  const auto FRAME_CAP = static_cast<std::uint32_t>(FRAME_BUFFER_MAX);
  slip_.cfg.maxFrameSize = FRAME_CAP;
  cobs_.cfg.maxFrameSize = FRAME_CAP;

  return Status::SUCCESS;
}

bool ApexInterface::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  // Generate filename from fullUid (componentId << 8 | instance 0)
  // Note: Interface is always instance 0 and may load TPRM before registration
  const std::uint32_t FULL_UID = static_cast<std::uint32_t>(componentId()) << 8;
  std::filesystem::path tprmPath = tprmDir / tprmFilename(FULL_UID);

  ApexInterfaceTunables tunables{};

  if (!std::filesystem::exists(tprmPath)) {
    // No TPRM file - use defaults (normal for apps that don't ship an interface TPRM)
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("No TPRM found at {}, using defaults", tprmPath.string()));
    }
  } else {
    // TPRM file exists - it must be valid
    std::ifstream file(tprmPath, std::ios::binary);
    if (!file) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->error(label(), 1, fmt::format("Failed to open TPRM: {}", tprmPath.string()));
      }
      return false;
    }

    file.read(reinterpret_cast<char*>(&tunables), sizeof(tunables));
    if (file.gcount() != sizeof(tunables)) {
      auto* log = componentLog();
      if (log != nullptr) {
        log->error(label(), 2,
                   fmt::format("TPRM size mismatch (got {}, expected {}): {}", file.gcount(),
                               sizeof(tunables), tprmPath.string()));
      }
      return false;
    }
  }

  // Configure sockets with tunables (defaults or loaded).
  const Status ST = configure(tunables);
  if (ST != Status::SUCCESS) {
    return false;
  }

  // Register tunables for INSPECT readback
  registerData(data::DataCategory::TUNABLE_PARAM, "tunableParams", &tunables_,
               sizeof(ApexInterfaceTunables));

  setConfigured(true);
  return true;
}

/* ----------------------------- Frame Processing ----------------------------- */

COMPAT_HOT
void ApexInterface::processBytesRx(std::uint8_t serverId,
                                   apex::compat::rospan<std::uint8_t> rxData) {
  const std::size_t IN_SIZE = rxData.size();
  if (COMPAT_UNLIKELY(IN_SIZE == 0U))
    return;

  // We only have one server (serverId 0).
  if (COMPAT_UNLIKELY(serverId != 0U))
    return;

  const bool USE_SLIP = (tunables_.framing == FramingType::SLIP);

  std::size_t consumed = 0U;
  while (consumed < IN_SIZE) {
    const std::size_t REMAINING = IN_SIZE - consumed;
    apex::compat::rospan<std::uint8_t> chunk(rxData.data() + consumed, REMAINING);

    if (USE_SLIP) {
      // Resume writing where previous partial decode left off. The decoder's w starts at 0
      // Each call advances the output pointer by frameLen (bytes already decoded) and
      // reduce capacity accordingly. On frame completion, frameLen gives the total size.
      const std::size_t ALREADY = slip_.st.frameLen;
      std::uint8_t* const OUT_PTR = frameBuf_.data() + ALREADY;
      const std::size_t OUT_CAP = (ALREADY < FRAME_BUFFER_MAX) ? (FRAME_BUFFER_MAX - ALREADY) : 0;

      const auto R = apex::protocols::slip::decodeChunk(
          slip_.st, slip_.cfg, apex::compat::bytes_span{chunk.data(), chunk.size()}, OUT_PTR,
          OUT_CAP);

      consumed += static_cast<std::size_t>(R.bytesConsumed);

      if (R.status == apex::protocols::slip::Status::NEED_MORE) {
        break;
      }
      if (COMPAT_UNLIKELY(R.status == apex::protocols::slip::Status::OUTPUT_FULL)) {
        reportDroppedFramesIfNeeded(serverId);
        continue;
      }
      if (COMPAT_UNLIKELY(R.status == apex::protocols::slip::Status::ERROR_OVERSIZE ||
                          R.status == apex::protocols::slip::Status::ERROR_MISSING_DELIMITER)) {
        reportDroppedFramesIfNeeded(serverId);
        continue;
      }
      if (R.frameCompleted) {
        // Total decoded frame size = bytes from prior calls + bytes from this call.
        const std::size_t TOTAL = ALREADY + static_cast<std::size_t>(R.bytesProduced);
        if (TOTAL > 0) {
          processAprotoPacket(serverId,
                              apex::compat::rospan<std::uint8_t>{frameBuf_.data(), TOTAL});
        }
        slip_.st = apex::protocols::slip::DecodeState{};
      }
    } else { // COBS
      const std::size_t ALREADY = cobs_.st.frameLen;
      std::uint8_t* const OUT_PTR = frameBuf_.data() + ALREADY;
      const std::size_t OUT_CAP = (ALREADY < FRAME_BUFFER_MAX) ? (FRAME_BUFFER_MAX - ALREADY) : 0;

      const auto R = apex::protocols::cobs::decodeChunk(
          cobs_.st, cobs_.cfg, apex::compat::bytes_span{chunk.data(), chunk.size()}, OUT_PTR,
          OUT_CAP);

      consumed += static_cast<std::size_t>(R.bytesConsumed);

      if (R.status == apex::protocols::cobs::Status::NEED_MORE) {
        break;
      }
      if (COMPAT_UNLIKELY(R.status == apex::protocols::cobs::Status::OUTPUT_FULL)) {
        reportDroppedFramesIfNeeded(serverId);
        continue;
      }
      if (COMPAT_UNLIKELY(R.status == apex::protocols::cobs::Status::ERROR_OVERSIZE ||
                          R.status == apex::protocols::cobs::Status::ERROR_DECODE ||
                          R.status == apex::protocols::cobs::Status::ERROR_MISSING_DELIMITER)) {
        reportDroppedFramesIfNeeded(serverId);
        continue;
      }
      if (R.frameCompleted) {
        const std::size_t TOTAL = ALREADY + static_cast<std::size_t>(R.bytesProduced);
        if (TOTAL > 0) {
          processAprotoPacket(serverId,
                              apex::compat::rospan<std::uint8_t>{frameBuf_.data(), TOTAL});
        }
        cobs_.st = apex::protocols::cobs::DecodeState{};
      }
    }
  }
}

/* ----------------------------- TX Encoding ----------------------------- */

COMPAT_HOT
std::size_t ApexInterface::processBytesTx(std::uint8_t serverId,
                                          apex::compat::mutable_bytes_span outBuf) {
  const std::size_t CAP = outBuf.size();
  if (COMPAT_UNLIKELY(CAP == 0U))
    return 0U;

  // We only have one server (serverId 0).
  if (COMPAT_UNLIKELY(serverId != 0U))
    return 0U;

  MessageBuffer* buf = nullptr;
  if (COMPAT_UNLIKELY(!tryDequeueTxMessage(serverId, buf) || buf == nullptr || buf->length == 0)) {
    return 0U;
  }

  const bool USE_SLIP = (tunables_.framing == FramingType::SLIP);
  std::size_t produced = 0;

  if (USE_SLIP) {
    const auto R = apex::protocols::slip::encode(apex::compat::bytes_span{buf->data, buf->length},
                                                 outBuf.data(), CAP);
    produced = static_cast<std::size_t>(R.bytesProduced);
  } else {
    // COBS encode with trailing delimiter (default true adds 0x00 at end).
    const auto R = apex::protocols::cobs::encode(apex::compat::bytes_span{buf->data, buf->length},
                                                 outBuf.data(), CAP, true);
    produced = static_cast<std::size_t>(R.bytesProduced);
  }

  // Release buffer back to TX pool.
  releaseTxBuffer(buf);
  return produced;
}

/* ----------------------------- APROTO Processing ----------------------------- */

void ApexInterface::processAprotoPacket(std::uint8_t serverId,
                                        apex::compat::rospan<std::uint8_t> frame) noexcept {
  // Minimum packet size check.
  if (frame.size() < aproto::APROTO_HEADER_SIZE) {
    ++stats_.packetsInvalid;
    return;
  }

  // Validate packet and create view.
  aproto::PacketView view{};
  const aproto::Status ST = aproto::createPacketView(frame, view);
  if (ST != aproto::Status::SUCCESS) {
    ++stats_.packetsInvalid;
    return;
  }

  ++stats_.packetsReceived;

  // Responses are ignored; only commands are processed.
  if (view.isResponse()) {
    // Make response available to app via RX pipe (telemetry passthrough).
    std::vector<std::uint8_t> msg(frame.data(), frame.data() + frame.size());
    pushRxMessage(serverId, std::move(msg));
    return;
  }

  // Try system opcodes first.
  if (handleSystemOpcode(serverId, view)) {
    ++stats_.systemCommands;
    return;
  }

  // Route to component.
  ++stats_.routedCommands;
  routeToComponent(serverId, view);
}

bool ApexInterface::handleSystemOpcode(std::uint8_t serverId,
                                       const aproto::PacketView& view) noexcept {
  const std::uint16_t OPCODE = view.header.opcode;

  // System opcodes are in range 0x0000-0x00FF.
  if (OPCODE > 0x00FF) {
    return false; // Not a system opcode.
  }

  switch (static_cast<aproto::SystemOpcode>(OPCODE)) {
  case aproto::SystemOpcode::NOOP:
    // Simple ACK, no payload.
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, 0);
    }
    return true;

  case aproto::SystemOpcode::PING:
    // Echo payload back.
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, 0, view.payload);
    }
    return true;

  case aproto::SystemOpcode::GET_STATUS:
    // Return interface status (could be extended with more data).
    if (view.ackRequested()) {
      std::array<std::uint8_t, 4> statusData{};
      statusData[0] = static_cast<std::uint8_t>(InterfaceBase::interfaceStatus());
      statusData[1] = componentResolver_ != nullptr ? 1 : 0; // Resolver attached.
      statusData[2] = static_cast<std::uint8_t>(tunables_.framing);
      statusData[3] = 0;
      enqueueAckNak(serverId, view.header, 0,
                    apex::compat::rospan<std::uint8_t>{statusData.data(), statusData.size()});
    }
    return true;

  case aproto::SystemOpcode::RESET:
    // Reset decoder state.
    slip_.st = apex::protocols::slip::DecodeState{};
    cobs_.st = apex::protocols::cobs::DecodeState{};
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, 0);
    }
    return true;

  case aproto::SystemOpcode::ACK:
  case aproto::SystemOpcode::NAK:
    // These are response opcodes - shouldn't arrive as commands.
    return true;

    /* ----------------------------- File Transfer Opcodes ----------------------------- */

  case aproto::SystemOpcode::FILE_BEGIN: {
    const std::uint8_t STATUS = fileTransfer_.handleBegin(view.payload.data(), view.payload.size());
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS);
    }
    return true;
  }

  case aproto::SystemOpcode::FILE_CHUNK: {
    const std::uint8_t STATUS = fileTransfer_.handleChunk(view.payload.data(), view.payload.size());
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS);
    }
    return true;
  }

  case aproto::SystemOpcode::FILE_END: {
    std::array<std::uint8_t, sizeof(aproto::FileEndResponse)> respData{};
    std::size_t respLen = 0;
    const std::uint8_t STATUS = fileTransfer_.handleEnd(respData.data(), respLen);
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS,
                    apex::compat::rospan<std::uint8_t>{respData.data(), respLen});
    }
    return true;
  }

  case aproto::SystemOpcode::FILE_ABORT: {
    const std::uint8_t STATUS = fileTransfer_.handleAbort();
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS);
    }
    return true;
  }

  case aproto::SystemOpcode::FILE_STATUS: {
    std::array<std::uint8_t, sizeof(aproto::FileStatusResponse)> respData{};
    std::size_t respLen = 0;
    const std::uint8_t STATUS = fileTransfer_.handleStatus(respData.data(), respLen);
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS,
                    apex::compat::rospan<std::uint8_t>{respData.data(), respLen});
    }
    return true;
  }

    /* ----------------------------- File Download Opcodes ----------------------------- */

  case aproto::SystemOpcode::FILE_GET: {
    std::array<std::uint8_t, sizeof(aproto::FileGetResponse)> respData{};
    std::size_t respLen = 0;
    const std::uint8_t STATUS =
        fileTransfer_.handleGet(view.payload.data(), view.payload.size(), respData.data(), respLen);
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS,
                    apex::compat::rospan<std::uint8_t>{respData.data(), respLen});
    }
    return true;
  }

  case aproto::SystemOpcode::FILE_READ_CHUNK: {
    // Response buffer sized to max chunk (4096) to hold the chunk data.
    std::array<std::uint8_t, 4096> respData{};
    std::size_t respLen = 0;
    const std::uint8_t STATUS = fileTransfer_.handleReadChunk(
        view.payload.data(), view.payload.size(), respData.data(), respLen);
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, STATUS,
                    apex::compat::rospan<std::uint8_t>{respData.data(), respLen});
    }
    return true;
  }

  default:
    // Opcodes 0x0080-0x00FF are base component opcodes (GET_COMMAND_COUNT,
    // GET_STATUS_INFO, etc.) handled by SystemComponentBase::handleCommand.
    // Route these to the target component instead of NAK'ing.
    if (OPCODE >= 0x0080) {
      return false; // Fall through to routeToComponent().
    }
    // Unknown system opcode below 0x0080 - NAK if requested.
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, 2); // Unknown opcode error.
    }
    return true;
  }
}

void ApexInterface::routeToComponent(std::uint8_t serverId,
                                     const aproto::PacketView& view) noexcept {
  const std::uint32_t FULL_UID = view.header.fullUid;

  // Check resolver availability.
  if (COMPAT_UNLIKELY(componentResolver_ == nullptr)) {
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, 3); // No resolver error.
    }
    return;
  }

  // Lookup component.
  auto* comp = componentResolver_->getComponent(FULL_UID);
  if (COMPAT_UNLIKELY(comp == nullptr)) {
    if (view.ackRequested()) {
      enqueueAckNak(serverId, view.header, 4); // Component not found.
    }
    return;
  }

  // Try async queue routing first (if component has allocated queues).
  ComponentQueues* queues = queueMgr_.get(FULL_UID);
  if (queues != nullptr) {
    // Allocate MessageBuffer from pool (payload only, no protocol header).
    MessageBuffer* buf = bufferPool_.acquire(view.payload.size());
    if (buf == nullptr) {
      // Pool exhausted - fall through to sync handling.
      ++stats_.cmdQueueOverflows;
    } else {
      // Copy payload only into buffer; metadata in struct fields.
      if (!view.payload.empty()) {
        std::memcpy(buf->data, view.payload.data(), view.payload.size());
      }
      buf->length = view.payload.size();
      buf->fullUid = view.header.fullUid;
      buf->opcode = view.header.opcode;
      buf->sequence = view.header.sequence;
      buf->internalOrigin = false; // External command.

      // Push pointer to component's command inbox.
      if (COMPAT_LIKELY(queues->cmdInbox.tryPush(buf))) {
        // Queued successfully - component will process in step().
        // Send immediate ACK if requested (command accepted, not processed yet).
        if (view.ackRequested()) {
          enqueueAckNak(serverId, view.header, 0); // ACK = command queued.
        }
        return;
      }
      // Queue full - release buffer and fall through to sync handling.
      bufferPool_.release(buf);
      ++stats_.cmdQueueOverflows;
    }
  }

  // Sync fallback: Dispatch command directly.
  std::vector<std::uint8_t> response;
  const std::uint8_t STATUS = comp->handleCommand(view.header.opcode, view.payload, response);

  // Build and queue response if requested.
  if (view.ackRequested()) {
    apex::compat::rospan<std::uint8_t> respSpan{};
    if (!response.empty()) {
      respSpan = apex::compat::rospan<std::uint8_t>{response.data(), response.size()};
    }
    enqueueAckNak(serverId, view.header, STATUS, respSpan);
  }

  // Also push raw command to RX pipe for app-level visibility.
  std::vector<std::uint8_t> cmdCopy(view.raw.data(), view.raw.data() + view.raw.size());
  pushRxMessage(serverId, std::move(cmdCopy));
}

void ApexInterface::enqueueAckNak(std::uint8_t serverId, const aproto::AprotoHeader& cmdHeader,
                                  std::uint8_t statusCode,
                                  apex::compat::rospan<std::uint8_t> responsePayload) noexcept {
  // Build response header.
  const bool IS_ACK = (statusCode == 0);
  const std::uint16_t RESP_OPCODE = IS_ACK ? static_cast<std::uint16_t>(aproto::SystemOpcode::ACK)
                                           : static_cast<std::uint16_t>(aproto::SystemOpcode::NAK);

  // Calculate payload size: standard AckPayload + optional response data.
  const std::size_t ACK_SIZE = aproto::APROTO_ACK_PAYLOAD_SIZE;
  const std::size_t TOTAL_PAYLOAD = ACK_SIZE + responsePayload.size();

  if (TOTAL_PAYLOAD > aproto::APROTO_MAX_PAYLOAD) {
    // Payload too large - drop response.
    return;
  }

  // Build AckPayload.
  aproto::AckPayload ack{};
  ack.cmdOpcode = cmdHeader.opcode;
  ack.cmdSequence = cmdHeader.sequence;
  ack.status = statusCode;
  std::memset(ack.reserved, 0, sizeof(ack.reserved));

  // Assemble full payload into frameBuf_ scratch (no heap allocation).
  std::memcpy(frameBuf_.data(), &ack, ACK_SIZE);
  if (!responsePayload.empty()) {
    std::memcpy(frameBuf_.data() + ACK_SIZE, responsePayload.data(), responsePayload.size());
  }

  // Build response header.
  aproto::AprotoHeader respHdr =
      aproto::buildHeader(cmdHeader.fullUid,  // Echo back source UID.
                          RESP_OPCODE,        // ACK or NAK.
                          cmdHeader.sequence, // Echo sequence for correlation.
                          static_cast<std::uint16_t>(TOTAL_PAYLOAD), // Payload length.
                          true,                                      // isResponse = true.
                          false,  // No ACK requested for responses.
                          false); // No CRC for simplicity.

  // Encode packet into responseBuf_ scratch.
  std::size_t bytesWritten = 0;
  const aproto::Status ST = aproto::encodePacket(
      respHdr, apex::compat::rospan<std::uint8_t>{frameBuf_.data(), TOTAL_PAYLOAD},
      apex::compat::mutable_bytes_span{responseBuf_.data(), responseBuf_.size()}, bytesWritten);

  if (ST != aproto::Status::SUCCESS || bytesWritten == 0) {
    return;
  }

  // Enqueue into pre-allocated TxFrame slot (no heap allocation).
  enqueueTxMessage(serverId, apex::compat::rospan<std::uint8_t>{responseBuf_.data(), bytesWritten});
  ++stats_.responsesQueued;
}

/* ----------------------------- Queue Draining ----------------------------- */

void ApexInterface::drainTelemetryOutboxes() noexcept {
  // Server ID 0 (single TCP socket).
  constexpr std::uint8_t SERVER_ID = 0;

  queueMgr_.forEach([this](std::uint32_t /*fullUid*/, ComponentQueues& q) {
    MessageBuffer* buf = nullptr;
    while (q.tlmOutbox.tryPop(buf)) {
      if (buf == nullptr) {
        continue;
      }

      // Wrap payload in APROTO at the external boundary.
      const aproto::AprotoHeader HDR = aproto::buildHeader(buf->fullUid, buf->opcode, buf->sequence,
                                                           static_cast<std::uint16_t>(buf->length),
                                                           true,   // isResponse (telemetry).
                                                           false,  // No ACK requested.
                                                           false); // No CRC.

      std::size_t bytesWritten = 0;
      const aproto::Status ST = aproto::encodePacket(
          HDR, apex::compat::rospan<std::uint8_t>{buf->data, buf->length},
          apex::compat::mutable_bytes_span{responseBuf_.data(), responseBuf_.size()}, bytesWritten);

      if (ST == aproto::Status::SUCCESS && bytesWritten > 0) {
        enqueueTxMessage(SERVER_ID,
                         apex::compat::rospan<std::uint8_t>{responseBuf_.data(), bytesWritten});
        ++stats_.telemetryFrames;
      }

      // Release buffer back to pool.
      bufferPool_.release(buf);
    }
  });
}

std::size_t ApexInterface::drainCommandsToComponents(std::size_t maxPerComponent) noexcept {
  std::size_t total = 0;

  queueMgr_.forEach([this, maxPerComponent, &total](std::uint32_t fullUid, ComponentQueues& q) {
    // Look up component via resolver.
    if (componentResolver_ == nullptr) {
      return;
    }

    auto* comp = componentResolver_->getComponent(fullUid);
    if (comp == nullptr) {
      return;
    }

    // Process commands from queue (pointer-based).
    std::size_t count = 0;
    MessageBuffer* buf = nullptr;

    while (q.cmdInbox.tryPop(buf)) {
      if (buf == nullptr) {
        continue;
      }

      // Dispatch to component using metadata fields directly (no APROTO re-parse).
      std::vector<std::uint8_t> response;
      [[maybe_unused]] const std::uint8_t RESULT = comp->handleCommand(
          buf->opcode, apex::compat::rospan<std::uint8_t>{buf->data, buf->length}, response);

      // Release buffer after processing.
      bufferPool_.release(buf);

      ++count;
      ++total;

      // Check limit.
      if (maxPerComponent > 0 && count >= maxPerComponent) {
        break;
      }
    }
  });

  return total;
}

/* ----------------------------- Command Handling ----------------------------- */

std::uint8_t ApexInterface::handleCommand(std::uint16_t opcode,
                                          apex::compat::rospan<std::uint8_t> payload,
                                          std::vector<std::uint8_t>& response) noexcept {
  // Interface-specific opcodes.
  if (opcode == static_cast<std::uint16_t>(InterfaceTlmOpcode::GET_STATS)) {
    // GET_STATS (0x0100) - Return packed health telemetry.
    InterfaceHealthTlm tlm{};
    tlm.packetsReceived = stats_.packetsReceived;
    tlm.packetsInvalid = stats_.packetsInvalid;
    tlm.systemCommands = stats_.systemCommands;
    tlm.routedCommands = stats_.routedCommands;
    tlm.responsesQueued = stats_.responsesQueued;
    tlm.telemetryFrames = stats_.telemetryFrames;
    tlm.framingErrors = stats_.framingErrors;
    tlm.internalCommandsSent = stats_.internalCommandsSent;
    tlm.internalTelemetrySent = stats_.internalTelemetrySent;
    tlm.internalCommandsFailed = stats_.internalCommandsFailed;
    tlm.multicastCommandsSent = stats_.multicastCommandsSent;
    tlm.broadcastCommandsSent = stats_.broadcastCommandsSent;
    tlm.cmdQueueOverflows = stats_.cmdQueueOverflows;
    tlm.tlmQueueOverflows = stats_.tlmQueueOverflows;
    response.resize(sizeof(tlm));
    std::memcpy(response.data(), &tlm, sizeof(tlm));
    return 0; // SUCCESS
  }
  if (opcode == 0x0082) {
    // GET_INTERFACE_STATS (legacy) - Return raw statistics structure.
    response.resize(sizeof(Stats));
    std::memcpy(response.data(), &stats_, sizeof(Stats));
    return 0; // SUCCESS
  }

  // Delegate to base class for common component opcodes (0x0080-0x0081).
  return system_component::SystemComponentBase::handleCommand(opcode, payload, response);
}

/* ----------------------------- Dropped Frame Reporting ----------------------------- */

void ApexInterface::reportDroppedFramesIfNeeded(std::uint8_t serverId) noexcept {
  // Increment dropped frame counter.
  ++droppedFrameCount_;
  ++stats_.framingErrors;

  // Check if threshold reached (0 = disabled).
  const std::uint16_t THRESHOLD = tunables_.droppedFrameReportThreshold;
  if (THRESHOLD == 0 || droppedFrameCount_ < THRESHOLD) {
    return;
  }

  // Build NAK with dropped frame count as payload.
  aproto::AprotoHeader nakHdr =
      aproto::buildHeader(fullUid(), // Interface fullUid.
                          static_cast<std::uint16_t>(aproto::SystemOpcode::NAK),
                          0,      // Sequence 0 for unsolicited NAK.
                          4,      // Payload = 4 bytes (dropped count).
                          true,   // isResponse.
                          false,  // No ACK requested.
                          false); // No CRC.

  // Payload: 4-byte dropped count (little-endian).
  std::array<std::uint8_t, 4> payload{};
  std::memcpy(payload.data(), &droppedFrameCount_, 4);

  // Build standard AckPayload.
  aproto::AckPayload ack{};
  ack.cmdOpcode = 0;
  ack.cmdSequence = 0;
  ack.status = NAK_STATUS_FRAMES_DROPPED;
  std::memset(ack.reserved, 0, sizeof(ack.reserved));

  // Combine AckPayload + dropped count into frameBuf_ scratch (no heap allocation).
  const std::size_t ACK_SIZE = aproto::APROTO_ACK_PAYLOAD_SIZE;
  const std::size_t TOTAL_SIZE = ACK_SIZE + payload.size();
  std::memcpy(frameBuf_.data(), &ack, ACK_SIZE);
  std::memcpy(frameBuf_.data() + ACK_SIZE, payload.data(), payload.size());

  // Encode into responseBuf_ scratch and enqueue.
  std::size_t bytesWritten = 0;
  const aproto::Status ST = aproto::encodePacket(
      nakHdr, apex::compat::rospan<std::uint8_t>{frameBuf_.data(), TOTAL_SIZE},
      apex::compat::mutable_bytes_span{responseBuf_.data(), responseBuf_.size()}, bytesWritten);

  if (ST == aproto::Status::SUCCESS && bytesWritten > 0) {
    enqueueTxMessage(serverId,
                     apex::compat::rospan<std::uint8_t>{responseBuf_.data(), bytesWritten});
  }

  // Reset counter after reporting.
  droppedFrameCount_ = 0;
}

/* ----------------------------- Internal Messaging ----------------------------- */

bool ApexInterface::postInternalCommand(std::uint32_t srcFullUid, std::uint32_t dstFullUid,
                                        std::uint16_t opcode,
                                        apex::compat::rospan<std::uint8_t> payload) noexcept {
  (void)srcFullUid; // Reserved for future use (audit trail, access control).

  // Get target component's queues.
  auto* queues = queueMgr_.get(dstFullUid);
  if (queues == nullptr) {
    ++stats_.internalCommandsFailed;
    return false; // Component not found.
  }

  // Allocate MessageBuffer from pool (payload only, no protocol header).
  MessageBuffer* buf = bufferPool_.acquire(payload.size());
  if (buf == nullptr) {
    ++stats_.internalCommandsFailed;
    return false; // Pool exhausted.
  }

  // Setup metadata (routing info lives in struct fields, not serialized).
  buf->fullUid = dstFullUid;
  buf->opcode = opcode;
  buf->sequence = internalSeq_++;
  buf->internalOrigin = true;

  // Copy payload directly (no protocol header encoding).
  if (!payload.empty()) {
    std::memcpy(buf->data, payload.data(), payload.size());
  }
  buf->length = payload.size();

  // Push pointer to cmdInbox (RT-safe, lock-free).
  const bool success = queues->cmdInbox.tryPush(buf);
  if (success) {
    ++stats_.internalCommandsSent;
  } else {
    // Queue full - release buffer.
    bufferPool_.release(buf);
    ++stats_.internalCommandsFailed;
    ++stats_.cmdQueueOverflows;
  }
  return success;
}

bool ApexInterface::postInternalTelemetry(std::uint32_t srcFullUid, std::uint16_t opcode,
                                          apex::compat::rospan<std::uint8_t> payload) noexcept {
  // Get source component's queues (telemetry goes to outbox).
  auto* queues = queueMgr_.get(srcFullUid);
  if (queues == nullptr) {
    return false; // Component not found.
  }

  // Allocate MessageBuffer from pool (payload only, no protocol header).
  MessageBuffer* buf = bufferPool_.acquire(payload.size());
  if (buf == nullptr) {
    ++stats_.tlmQueueOverflows;
    return false; // Pool exhausted.
  }

  // Setup metadata (routing info lives in struct fields, not serialized).
  buf->fullUid = srcFullUid;
  buf->opcode = opcode;
  buf->sequence = internalSeq_++;
  buf->internalOrigin = true;

  // Copy payload directly (no protocol header encoding).
  if (!payload.empty()) {
    std::memcpy(buf->data, payload.data(), payload.size());
  }
  buf->length = payload.size();

  // Push pointer to tlmOutbox (RT-safe, lock-free).
  const bool success = queues->tlmOutbox.tryPush(buf);
  if (success) {
    ++stats_.internalTelemetrySent;
  } else {
    // Queue full - release buffer.
    bufferPool_.release(buf);
    ++stats_.internalCommandsFailed;
    ++stats_.tlmQueueOverflows;
  }
  return success;
}

std::size_t ApexInterface::postMulticastCommand(
    std::uint32_t srcFullUid, apex::compat::rospan<std::uint32_t> dstFullUids, std::uint16_t opcode,
    apex::compat::rospan<std::uint8_t> payload) noexcept {
  (void)srcFullUid; // Reserved for future use.

  if (dstFullUids.empty()) {
    return 0;
  }

  // Count valid recipients first.
  std::size_t validCount = 0;
  for (auto uid : dstFullUids) {
    if (queueMgr_.get(uid) != nullptr) {
      ++validCount;
    }
  }

  if (validCount == 0) {
    return 0;
  }

  // Acquire single buffer (payload only, no protocol header).
  MessageBuffer* buf = bufferPool_.acquire(payload.size());
  if (buf == nullptr) {
    ++stats_.internalCommandsFailed;
    return 0; // Pool exhausted.
  }

  // Setup metadata (routing info lives in struct fields, not serialized).
  buf->fullUid = 0; // Multicast has no single target.
  buf->opcode = opcode;
  buf->sequence = internalSeq_++;
  buf->internalOrigin = true;

  // Copy payload directly (no protocol header encoding).
  if (!payload.empty()) {
    std::memcpy(buf->data, payload.data(), payload.size());
  }
  buf->length = payload.size();

  // Set refcount to number of valid recipients.
  buf->setRefCount(static_cast<std::uint32_t>(validCount));

  // Push same pointer to all recipient queues.
  std::size_t queued = 0;
  for (auto uid : dstFullUids) {
    auto* q = queueMgr_.get(uid);
    if (q != nullptr && q->cmdInbox.tryPush(buf)) {
      ++queued;
    }
  }

  // If some pushes failed, adjust refcount by releasing for each failed push.
  if (queued < validCount) {
    for (std::size_t i = 0; i < (validCount - queued); ++i) {
      bufferPool_.release(buf); // Decrements refcount.
    }
  }

  if (queued > 0) {
    ++stats_.multicastCommandsSent;
    stats_.internalCommandsSent += static_cast<std::uint32_t>(queued);
  }
  return queued;
}

std::size_t
ApexInterface::postBroadcastCommand(std::uint32_t srcFullUid, std::uint16_t opcode,
                                    apex::compat::rospan<std::uint8_t> payload) noexcept {
  // Collect all UIDs except source (stack-based array for RT-safety).
  constexpr std::size_t MAX_BROADCAST_TARGETS = 64;
  std::array<std::uint32_t, MAX_BROADCAST_TARGETS> targets{};
  std::size_t count = 0;

  queueMgr_.forEach([&](std::uint32_t uid, ComponentQueues&) {
    if (uid != srcFullUid && count < MAX_BROADCAST_TARGETS) {
      targets[count++] = uid;
    }
  });

  if (count == 0) {
    return 0;
  }

  const std::size_t result =
      postMulticastCommand(srcFullUid, {targets.data(), count}, opcode, payload);
  if (result > 0) {
    ++stats_.broadcastCommandsSent;
  }
  return result;
}

/* ----------------------------- Statistics ----------------------------- */

void ApexInterface::logStatsSummary() noexcept {
  auto* log = componentLog();
  if (log == nullptr) {
    return;
  }

  log->info(label(), "=== Interface Statistics ===");
  log->info(label(), "External Interface:");
  log->info(label(), fmt::format("  Packets received: {}", stats_.packetsReceived));
  log->info(label(), fmt::format("  Packets invalid: {}", stats_.packetsInvalid));
  log->info(label(), fmt::format("  System commands: {}", stats_.systemCommands));
  log->info(label(), fmt::format("  Routed commands: {}", stats_.routedCommands));
  log->info(label(), fmt::format("  Responses queued: {}", stats_.responsesQueued));
  log->info(label(), fmt::format("  Telemetry frames: {}", stats_.telemetryFrames));
  log->info(label(), fmt::format("  Framing errors: {}", stats_.framingErrors));
  log->info(label(), "Internal Bus:");
  log->info(label(), fmt::format("  Commands sent: {}", stats_.internalCommandsSent));
  log->info(label(), fmt::format("  Telemetry sent: {}", stats_.internalTelemetrySent));
  log->info(label(), fmt::format("  Commands failed: {}", stats_.internalCommandsFailed));
  log->info(label(), "Queue Health:");
  log->info(label(), fmt::format("  Cmd queue overflows: {}", stats_.cmdQueueOverflows));
  log->info(label(), fmt::format("  Tlm queue overflows: {}", stats_.tlmQueueOverflows));
  log->info(label(), "=============================");
}

} // namespace interface
} // namespace system_core
