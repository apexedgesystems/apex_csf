/**
 * @file ModbusTcpTransport.cpp
 * @brief Implementation of Modbus TCP transport over TCP/IP.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTcpTransport.hpp"

#include <cstring>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

using apex::protocols::TraceDirection;

/* ----------------------------- Construction ----------------------------- */

ModbusTcpTransport::ModbusTcpTransport(const std::string& host, std::uint16_t port,
                                       const ModbusTcpConfig& config)
    : host_(host), port_(port), config_(config), client_(nullptr), transactionId_(0),
      isOpen_(false), stats_() {
  // Build description string
  description_ = "TCP:" + host_ + ":" + std::to_string(port_);
}

ModbusTcpTransport::~ModbusTcpTransport() { (void)close(); }

/* ----------------------------- Configuration ----------------------------- */

Status ModbusTcpTransport::open() noexcept {
  if (isOpen_) {
    return Status::SUCCESS;
  }

  // Create TCP client
  client_ = std::make_unique<tcp::TcpSocketClient>(host_, std::to_string(port_));

  // Initialize connection
  std::string error;
  const std::uint8_t RESULT = client_->init(static_cast<int>(config_.connectTimeoutMs), error);

  if (RESULT != tcp::TCP_CLIENT_SUCCESS) {
    client_.reset();
    return Status::ERROR_IO;
  }

  isOpen_ = true;
  transactionId_ = 0;
  return Status::SUCCESS;
}

Status ModbusTcpTransport::close() noexcept {
  if (client_) {
    client_.reset();
  }
  isOpen_ = false;
  return Status::SUCCESS;
}

bool ModbusTcpTransport::isOpen() const noexcept { return isOpen_ && client_ != nullptr; }

/* ----------------------------- I/O Operations ----------------------------- */

Status ModbusTcpTransport::sendRequest(const FrameBuffer& frame, int timeoutMs) noexcept {
  if (!isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  // For TCP, the input frame should be RTU format (Unit ID + PDU + CRC)
  // We strip the CRC and wrap with MBAP header
  if (frame.length < Constants::RTU_MIN_FRAME_SIZE) {
    return Status::ERROR_INVALID_ARG;
  }

  // Create a frame without CRC for MBAP transmission
  FrameBuffer pduFrame;
  pduFrame.length = frame.length - Constants::CRC_SIZE;
  std::memcpy(pduFrame.data, frame.data, pduFrame.length);

  return sendMbapFrame(pduFrame, timeoutMs);
}

Status ModbusTcpTransport::receiveResponse(FrameBuffer& frame, int timeoutMs) noexcept {
  if (!isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  frame.reset();
  return receiveMbapFrame(frame, timeoutMs);
}

Status ModbusTcpTransport::flush() noexcept {
  if (!isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  // TCP doesn't need explicit flush like serial
  return Status::SUCCESS;
}

/* ----------------------------- Statistics ----------------------------- */

ModbusStats ModbusTcpTransport::stats() const noexcept { return stats_; }

void ModbusTcpTransport::resetStats() noexcept { stats_.reset(); }

/* ----------------------------- Info ----------------------------- */

const char* ModbusTcpTransport::description() const noexcept { return description_.c_str(); }

/* ----------------------------- Private Helpers ----------------------------- */

Status ModbusTcpTransport::sendMbapFrame(const FrameBuffer& frame, int timeoutMs) noexcept {
  // MBAP Header (7 bytes):
  //   Transaction ID (2 bytes) - incremented for each request
  //   Protocol ID (2 bytes) - always 0 for Modbus
  //   Length (2 bytes) - number of following bytes (Unit ID + PDU)
  //   Unit ID (1 byte) - from frame.data[0]
  //
  // Frame layout: [MBAP Header (7)] [Unit ID (1)] [PDU (N)]
  // Note: Unit ID is part of the PDU frame, but also counted in MBAP length

  // Increment transaction ID
  ++transactionId_;

  // Calculate MBAP length field (Unit ID + PDU)
  const std::uint16_t MBAP_LENGTH = static_cast<std::uint16_t>(frame.length);

  // Build MBAP frame
  std::uint8_t mbapFrame[Constants::TCP_MAX_FRAME_SIZE];
  std::size_t offset = 0;

  // Transaction ID (big-endian)
  mbapFrame[offset++] = static_cast<std::uint8_t>(transactionId_ >> 8);
  mbapFrame[offset++] = static_cast<std::uint8_t>(transactionId_ & 0xFF);

  // Protocol ID (0 for Modbus)
  mbapFrame[offset++] = 0x00;
  mbapFrame[offset++] = 0x00;

  // Length (big-endian)
  mbapFrame[offset++] = static_cast<std::uint8_t>(MBAP_LENGTH >> 8);
  mbapFrame[offset++] = static_cast<std::uint8_t>(MBAP_LENGTH & 0xFF);

  // Copy Unit ID + PDU
  std::memcpy(mbapFrame + offset, frame.data, frame.length);
  offset += frame.length;

  // Send via TCP
  std::string error;
  const apex::compat::span<const std::uint8_t> DATA_SPAN(mbapFrame, offset);
  const ssize_t WRITTEN = client_->write(DATA_SPAN, timeoutMs, error);

  if (WRITTEN < 0) {
    ++stats_.ioErrors;
    return Status::ERROR_IO;
  }

  if (static_cast<std::size_t>(WRITTEN) != offset) {
    ++stats_.ioErrors;
    return Status::ERROR_IO;
  }

  ++stats_.requestsSent;
  stats_.bytesTx += static_cast<std::uint64_t>(WRITTEN);

  // Trace the transmitted frame (MBAP header + PDU)
  invokeTrace(TraceDirection::TX, mbapFrame, offset);

  return Status::SUCCESS;
}

Status ModbusTcpTransport::receiveMbapFrame(FrameBuffer& frame, int timeoutMs) noexcept {
  // Read MBAP header first (7 bytes)
  std::uint8_t mbapHeader[Constants::MBAP_HEADER_SIZE];
  apex::compat::mutable_bytes_span headerSpan(mbapHeader, Constants::MBAP_HEADER_SIZE);

  std::string error;
  ssize_t bytesRead = client_->read(headerSpan, timeoutMs, error);

  if (bytesRead == 0) {
    ++stats_.timeouts;
    return Status::ERROR_TIMEOUT;
  }

  if (bytesRead < 0) {
    ++stats_.ioErrors;
    return Status::ERROR_IO;
  }

  if (static_cast<std::size_t>(bytesRead) < Constants::MBAP_HEADER_SIZE) {
    ++stats_.frameErrors;
    return Status::ERROR_FRAME;
  }

  // Parse MBAP header
  const std::uint16_t RX_TRANSACTION_ID =
      (static_cast<std::uint16_t>(mbapHeader[0]) << 8) | mbapHeader[1];
  const std::uint16_t PROTOCOL_ID =
      (static_cast<std::uint16_t>(mbapHeader[2]) << 8) | mbapHeader[3];
  const std::uint16_t MBAP_LENGTH =
      (static_cast<std::uint16_t>(mbapHeader[4]) << 8) | mbapHeader[5];

  // Validate protocol ID
  if (PROTOCOL_ID != Constants::MBAP_PROTOCOL_ID) {
    ++stats_.frameErrors;
    return Status::ERROR_FRAME;
  }

  // Validate transaction ID matches
  if (RX_TRANSACTION_ID != transactionId_) {
    ++stats_.frameErrors;
    return Status::ERROR_FRAME;
  }

  // Validate length
  if (MBAP_LENGTH == 0 || MBAP_LENGTH > Constants::PDU_MAX_SIZE + 1) {
    ++stats_.frameErrors;
    return Status::ERROR_FRAME;
  }

  // Read the PDU (Unit ID + Function Code + Data)
  apex::compat::mutable_bytes_span pduSpan(frame.data, MBAP_LENGTH);
  bytesRead = client_->read(pduSpan, timeoutMs, error);

  if (bytesRead == 0) {
    ++stats_.timeouts;
    return Status::ERROR_TIMEOUT;
  }

  if (bytesRead < 0) {
    ++stats_.ioErrors;
    return Status::ERROR_IO;
  }

  if (static_cast<std::uint16_t>(bytesRead) != MBAP_LENGTH) {
    ++stats_.frameErrors;
    return Status::ERROR_FRAME;
  }

  frame.length = static_cast<std::size_t>(bytesRead);
  stats_.bytesRx += static_cast<std::uint64_t>(Constants::MBAP_HEADER_SIZE) +
                    static_cast<std::uint64_t>(bytesRead);

  // Trace the received frame
  invokeTrace(TraceDirection::RX, frame.data, frame.length);

  // Check for exception response
  if (frame.length >= 2 && isExceptionResponse(frame.data[1])) {
    ++stats_.exceptionsReceived;
    return Status::ERROR_EXCEPTION;
  }

  ++stats_.responsesReceived;
  return Status::SUCCESS;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex
