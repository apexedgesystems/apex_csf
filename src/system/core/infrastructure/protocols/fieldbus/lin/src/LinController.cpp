/**
 * @file LinController.cpp
 * @brief Implementation of LIN master/slave controller.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/lin/inc/LinController.hpp"

#include <sys/ioctl.h>
#include <termios.h>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace lin {

using apex::protocols::TraceDirection;

/* ----------------------------- LinController Methods ----------------------------- */

LinController::LinController(apex::protocols::serial::uart::UartDevice& uart) noexcept
    : uart_(uart) {}

Status LinController::configure(const LinConfig& config) noexcept {
  if (!uart_.isOpen()) {
    return Status::ERROR_CLOSED;
  }

  // Configure underlying UART with LIN-appropriate settings
  const auto UART_STATUS = uart_.configure(config.toUartConfig());
  if (UART_STATUS != apex::protocols::serial::uart::Status::SUCCESS) {
    return Status::ERROR_IO;
  }

  config_ = config;
  configured_ = true;
  return Status::SUCCESS;
}

bool LinController::isConfigured() const noexcept { return configured_ && uart_.isOpen(); }

const LinConfig& LinController::config() const noexcept { return config_; }

Status LinController::sendBreak() noexcept {
  if (!isConfigured()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  const int FD = uart_.fd();
  if (FD < 0) {
    return Status::ERROR_CLOSED;
  }

  // Send break via TCSBRK ioctl
  // TCSBRK with arg=0 sends break for 0.25-0.5 seconds (too long for LIN)
  // Use TCSBRKP with duration in centiseconds, or use tcsendbreak()
  // For LIN, we need ~13 bit times at baud rate
  // At 19200 baud, 13 bits = ~677us

  // Using tcsendbreak with duration 0 sends break for 250-500ms (POSIX)
  // For precise LIN break timing, we use the baud rate trick:
  // Temporarily lower baud rate and send 0x00

  // Alternative: Use TCSBRKP (Linux-specific) with precise duration
  // Or: Use tcsendbreak() which sends 0.25-0.5s break (not ideal but works)

  // For now, use tcsendbreak which is most portable
  if (tcsendbreak(FD, 0) != 0) {
    ++stats_.breakErrors;
    return Status::ERROR_BREAK;
  }

  return Status::SUCCESS;
}

Status LinController::sendHeader(std::uint8_t frameId) noexcept {
  if (!isConfigured()) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (!isValidFrameId(frameId)) {
    return Status::ERROR_INVALID_ARG;
  }

  // Send break field
  const Status BREAK_STATUS = sendBreak();
  if (BREAK_STATUS != Status::SUCCESS) {
    return BREAK_STATUS;
  }

  // Build header (sync + PID)
  FrameBuffer header;
  const Status BUILD_STATUS = buildHeader(header, frameId);
  if (BUILD_STATUS != Status::SUCCESS) {
    return BUILD_STATUS;
  }

  // Send header bytes
  const Status WRITE_STATUS = writeWithCollisionCheck(header.data, header.length);
  if (WRITE_STATUS != Status::SUCCESS) {
    return WRITE_STATUS;
  }

  // Trace header TX (sync + PID)
  invokeTrace(TraceDirection::TX, header.data, header.length);

  ++stats_.framesSent;
  return Status::SUCCESS;
}

Status LinController::receiveResponse(std::uint8_t frameId, FrameBuffer& response,
                                      ParsedFrame& parsed) noexcept {
  // Determine data length from frame ID
  std::size_t dataLen = dataLengthFromId(frameId);
  if (isDiagnosticFrame(frameId)) {
    dataLen = 8;
  }
  return receiveResponse(frameId, dataLen, response, parsed);
}

Status LinController::receiveResponse(std::uint8_t frameId, std::size_t dataLen,
                                      FrameBuffer& response, ParsedFrame& parsed) noexcept {
  if (!isConfigured()) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (!isValidFrameId(frameId)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (dataLen == 0 || dataLen > Constants::MAX_DATA_LENGTH) {
    return Status::ERROR_INVALID_ARG;
  }

  response.reset();

  // Read data + checksum
  const std::size_t EXPECTED_LEN = dataLen + 1; // data + checksum
  std::size_t bytesRead = 0;
  const Status READ_STATUS = readBytes(response.data, EXPECTED_LEN, bytesRead);

  if (READ_STATUS == Status::ERROR_TIMEOUT) {
    ++stats_.timeouts;
    return Status::ERROR_NO_RESPONSE;
  }
  if (READ_STATUS != Status::SUCCESS) {
    return READ_STATUS;
  }
  if (bytesRead < EXPECTED_LEN) {
    ++stats_.timeouts;
    return Status::ERROR_NO_RESPONSE;
  }

  response.length = bytesRead;

  // Trace RX (data + checksum)
  invokeTrace(TraceDirection::RX, response.data, response.length);

  // Parse response
  const std::uint8_t PID = calculatePid(frameId);
  const Status PARSE_STATUS =
      parseResponse(response.data, response.length, PID, dataLen, config_.checksumType, parsed);

  if (PARSE_STATUS == Status::ERROR_PARITY) {
    ++stats_.parityErrors;
    return PARSE_STATUS;
  }
  if (PARSE_STATUS == Status::ERROR_CHECKSUM) {
    ++stats_.checksumErrors;
    return PARSE_STATUS;
  }
  if (PARSE_STATUS != Status::SUCCESS) {
    return PARSE_STATUS;
  }

  ++stats_.framesReceived;
  return Status::SUCCESS;
}

Status LinController::sendFrame(std::uint8_t frameId, const std::uint8_t* data,
                                std::size_t dataLen) noexcept {
  if (!isConfigured()) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (!isValidFrameId(frameId)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (data == nullptr || dataLen == 0 || dataLen > Constants::MAX_DATA_LENGTH) {
    return Status::ERROR_INVALID_ARG;
  }

  // Send header first
  const Status HEADER_STATUS = sendHeader(frameId);
  if (HEADER_STATUS != Status::SUCCESS) {
    return HEADER_STATUS;
  }

  // Build response (data + checksum)
  FrameBuffer response;
  const std::uint8_t PID = calculatePid(frameId);
  const Status BUILD_STATUS = buildResponse(response, PID, data, dataLen, config_.checksumType);
  if (BUILD_STATUS != Status::SUCCESS) {
    return BUILD_STATUS;
  }

  // Send response bytes
  const Status WRITE_STATUS = writeWithCollisionCheck(response.data, response.length);
  if (WRITE_STATUS != Status::SUCCESS) {
    return WRITE_STATUS;
  }

  // Trace data TX (data + checksum)
  invokeTrace(TraceDirection::TX, response.data, response.length);

  return Status::SUCCESS;
}

Status LinController::requestFrame(std::uint8_t frameId, FrameBuffer& response,
                                   ParsedFrame& parsed) noexcept {
  // Send header then wait for slave response
  const Status HEADER_STATUS = sendHeader(frameId);
  if (HEADER_STATUS != Status::SUCCESS) {
    return HEADER_STATUS;
  }
  return receiveResponse(frameId, response, parsed);
}

Status LinController::requestFrame(std::uint8_t frameId, std::size_t dataLen, FrameBuffer& response,
                                   ParsedFrame& parsed) noexcept {
  // Send header then wait for slave response
  const Status HEADER_STATUS = sendHeader(frameId);
  if (HEADER_STATUS != Status::SUCCESS) {
    return HEADER_STATUS;
  }
  return receiveResponse(frameId, dataLen, response, parsed);
}

/* ----------------------------- LinController Methods ----------------------------- */

Status LinController::waitForHeader(std::uint8_t& frameId) noexcept {
  return waitForHeader(frameId, config_.responseTimeoutMs);
}

Status LinController::waitForHeader(std::uint8_t& frameId, std::uint16_t timeoutMs) noexcept {
  frameId = 0;

  if (!isConfigured()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  // Wait for sync byte (0x55) which follows break
  // Note: Break detection depends on UART driver support
  std::uint8_t headerBuf[2]; // sync + PID
  std::size_t bytesRead = 0;

  // Read sync byte
  const auto SYNC_STATUS = uart_.read(headerBuf, 1, bytesRead, static_cast<int>(timeoutMs));
  if (SYNC_STATUS == apex::protocols::serial::uart::Status::WOULD_BLOCK) {
    ++stats_.timeouts;
    return Status::ERROR_TIMEOUT;
  }
  if (SYNC_STATUS != apex::protocols::serial::uart::Status::SUCCESS || bytesRead != 1) {
    return Status::ERROR_IO;
  }

  // Verify sync byte
  if (headerBuf[0] != Constants::SYNC_BYTE) {
    ++stats_.syncErrors;
    return Status::ERROR_SYNC;
  }

  // Read PID
  const int BYTE_TIMEOUT_MS = static_cast<int>((config_.interByteTimeoutUs() + 999) / 1000);
  const auto PID_STATUS = uart_.read(headerBuf + 1, 1, bytesRead, BYTE_TIMEOUT_MS);
  if (PID_STATUS != apex::protocols::serial::uart::Status::SUCCESS || bytesRead != 1) {
    ++stats_.timeouts;
    return Status::ERROR_TIMEOUT;
  }

  // Verify PID parity
  const std::uint8_t PID = headerBuf[1];
  if (!verifyPidParity(PID)) {
    ++stats_.parityErrors;
    return Status::ERROR_PARITY;
  }

  // Trace header RX (sync + PID)
  invokeTrace(TraceDirection::RX, headerBuf, 2);

  frameId = extractFrameId(PID);
  ++stats_.framesReceived;
  return Status::SUCCESS;
}

Status LinController::respondToHeader(std::uint8_t frameId, const std::uint8_t* data,
                                      std::size_t dataLen) noexcept {
  if (!isConfigured()) {
    return Status::ERROR_NOT_CONFIGURED;
  }
  if (!isValidFrameId(frameId)) {
    return Status::ERROR_INVALID_ARG;
  }
  if (data == nullptr || dataLen == 0 || dataLen > Constants::MAX_DATA_LENGTH) {
    return Status::ERROR_INVALID_ARG;
  }

  // Build response (data + checksum)
  FrameBuffer response;
  const std::uint8_t PID = calculatePid(frameId);
  const Status BUILD_STATUS = buildResponse(response, PID, data, dataLen, config_.checksumType);
  if (BUILD_STATUS != Status::SUCCESS) {
    return BUILD_STATUS;
  }

  // Send response bytes (with collision detection if enabled)
  const Status WRITE_STATUS = writeWithCollisionCheck(response.data, response.length);
  if (WRITE_STATUS != Status::SUCCESS) {
    return WRITE_STATUS;
  }

  // Trace response TX (data + checksum)
  invokeTrace(TraceDirection::TX, response.data, response.length);

  ++stats_.framesSent;
  return Status::SUCCESS;
}

const LinStats& LinController::stats() const noexcept { return stats_; }

void LinController::resetStats() noexcept { stats_.reset(); }

/* ----------------------------- Private Methods ----------------------------- */

Status LinController::writeWithCollisionCheck(const std::uint8_t* data, std::size_t len) noexcept {
  if (data == nullptr || len == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  std::size_t bytesWritten = 0;
  const auto WRITE_STATUS =
      uart_.write(data, len, bytesWritten, static_cast<int>(config_.responseTimeoutMs));

  if (WRITE_STATUS != apex::protocols::serial::uart::Status::SUCCESS) {
    return Status::ERROR_IO;
  }
  if (bytesWritten != len) {
    return Status::ERROR_IO;
  }

  // Collision detection via readback
  if (config_.enableCollisionDetection) {
    std::uint8_t readback[Constants::MAX_FRAME_SIZE];
    std::size_t bytesRead = 0;
    const auto READ_STATUS =
        uart_.read(readback, len, bytesRead, static_cast<int>(config_.responseTimeoutMs));

    if (READ_STATUS == apex::protocols::serial::uart::Status::SUCCESS && bytesRead == len) {
      // Compare transmitted and received bytes
      for (std::size_t i = 0; i < len; ++i) {
        if (readback[i] != data[i]) {
          ++stats_.collisions;
          return Status::ERROR_BUS_COLLISION;
        }
      }
    }
    // If readback fails or times out, we can't detect collision but proceed anyway
  }

  return Status::SUCCESS;
}

Status LinController::readBytes(std::uint8_t* buffer, std::size_t count,
                                std::size_t& bytesRead) noexcept {
  bytesRead = 0;

  if (buffer == nullptr || count == 0) {
    return Status::ERROR_INVALID_ARG;
  }

  // Calculate per-byte timeout from inter-byte timeout
  const int BYTE_TIMEOUT_MS = static_cast<int>((config_.interByteTimeoutUs() + 999) / 1000);
  const int TOTAL_TIMEOUT_MS = static_cast<int>(config_.responseTimeoutMs);

  std::size_t totalRead = 0;
  while (totalRead < count) {
    std::size_t chunkRead = 0;
    const int TIMEOUT = (totalRead == 0) ? TOTAL_TIMEOUT_MS : BYTE_TIMEOUT_MS;

    const auto STATUS = uart_.read(buffer + totalRead, count - totalRead, chunkRead, TIMEOUT);

    if (STATUS == apex::protocols::serial::uart::Status::WOULD_BLOCK) {
      if (totalRead == 0) {
        return Status::ERROR_TIMEOUT;
      }
      // Got some bytes but timed out waiting for more
      break;
    }
    if (STATUS != apex::protocols::serial::uart::Status::SUCCESS) {
      return Status::ERROR_IO;
    }
    if (chunkRead == 0) {
      break;
    }
    totalRead += chunkRead;
  }

  bytesRead = totalRead;

  if (totalRead < count) {
    return Status::ERROR_TIMEOUT;
  }
  return Status::SUCCESS;
}

} // namespace lin
} // namespace fieldbus
} // namespace protocols
} // namespace apex
