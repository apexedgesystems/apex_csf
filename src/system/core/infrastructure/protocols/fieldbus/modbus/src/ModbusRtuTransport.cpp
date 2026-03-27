/**
 * @file ModbusRtuTransport.cpp
 * @brief Implementation of Modbus RTU transport over UART.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusRtuTransport.hpp"

#include <ctime>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

using apex::protocols::TraceDirection;

/* ----------------------------- Construction ----------------------------- */

ModbusRtuTransport::ModbusRtuTransport(uart::UartDevice* device, const ModbusRtuConfig& config,
                                       std::uint32_t baudRate)
    : device_(device), config_(config), interFrameDelayUs_(0), isOpen_(false), stats_() {
  // Calculate inter-frame delay if not specified
  if (config_.interFrameDelayUs > 0) {
    interFrameDelayUs_ = config_.interFrameDelayUs;
  } else if (baudRate > 0) {
    interFrameDelayUs_ = calculateInterFrameDelay(baudRate);
  } else {
    // Default to 4ms (suitable for 9600 baud)
    interFrameDelayUs_ = Constants::RTU_INTER_FRAME_DELAY_US_9600;
  }

  // Build description string
  if (device_ != nullptr) {
    description_ = "RTU:";
    description_ += device_->devicePath();
  } else {
    description_ = "RTU:<null>";
  }
}

ModbusRtuTransport::~ModbusRtuTransport() {
  // Do not close the UART - we don't own it
}

/* ----------------------------- Configuration ----------------------------- */

Status ModbusRtuTransport::open() noexcept {
  if (device_ == nullptr) {
    return Status::ERROR_INVALID_ARG;
  }

  if (!device_->isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  // Flush any stale data
  (void)device_->flush(true, true);

  isOpen_ = true;
  return Status::SUCCESS;
}

Status ModbusRtuTransport::close() noexcept {
  isOpen_ = false;
  return Status::SUCCESS;
}

bool ModbusRtuTransport::isOpen() const noexcept { return isOpen_ && device_ != nullptr; }

/* ----------------------------- I/O Operations ----------------------------- */

Status ModbusRtuTransport::sendRequest(const FrameBuffer& frame, int timeoutMs) noexcept {
  if (!isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  if (frame.length < Constants::RTU_MIN_FRAME_SIZE) {
    return Status::ERROR_INVALID_ARG;
  }

  // Wait inter-frame delay before sending
  waitInterFrameDelay();

  // Send the complete frame
  std::size_t bytesWritten = 0;
  const auto UART_STATUS = device_->write(frame.data, frame.length, bytesWritten, timeoutMs);

  if (UART_STATUS != uart::Status::SUCCESS) {
    ++stats_.ioErrors;
    return Status::ERROR_IO;
  }

  if (bytesWritten != frame.length) {
    // Partial write - shouldn't happen with blocking mode, but handle it
    ++stats_.ioErrors;
    return Status::ERROR_IO;
  }

  ++stats_.requestsSent;
  stats_.bytesTx += bytesWritten;

  // Trace the transmitted frame
  invokeTrace(TraceDirection::TX, frame.data, frame.length);

  // Wait turnaround delay if configured (for RS485 direction switching)
  if (config_.turnaroundDelayUs > 0) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = static_cast<long>(config_.turnaroundDelayUs) * 1000L;
    nanosleep(&ts, nullptr);
  }

  return Status::SUCCESS;
}

Status ModbusRtuTransport::receiveResponse(FrameBuffer& frame, int timeoutMs) noexcept {
  if (!isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  frame.reset();

  // Read at least minimum frame size first (unit + fc + crc = 4 bytes)
  std::size_t offset = 0;
  Status status = readWithTimeout(frame.data, offset, Constants::RTU_MIN_FRAME_SIZE, timeoutMs);
  if (status != Status::SUCCESS) {
    return status;
  }

  // Determine expected response length based on function code
  const std::uint8_t FUNCTION_CODE = frame.data[1];

  std::size_t expectedLength = 0;

  if (isExceptionResponse(FUNCTION_CODE)) {
    // Exception response: [Unit] [FC|0x80] [ExceptionCode] [CRC] = 5 bytes
    expectedLength = 5;
  } else {
    // Normal response length depends on function code
    switch (static_cast<FunctionCode>(FUNCTION_CODE)) {
    case FunctionCode::READ_COILS:
    case FunctionCode::READ_DISCRETE_INPUTS:
    case FunctionCode::READ_HOLDING_REGISTERS:
    case FunctionCode::READ_INPUT_REGISTERS:
      // Read responses: [Unit] [FC] [ByteCount] [Data...] [CRC]
      // Need to read byte count first
      if (offset < 3) {
        status = readWithTimeout(frame.data, offset, 3, timeoutMs);
        if (status != Status::SUCCESS) {
          return status;
        }
      }
      expectedLength = 3 + frame.data[2] + 2; // Header + data + CRC
      break;

    case FunctionCode::WRITE_SINGLE_COIL:
    case FunctionCode::WRITE_SINGLE_REGISTER:
      // Echo responses: [Unit] [FC] [Addr] [Value] [CRC] = 8 bytes
      expectedLength = 8;
      break;

    case FunctionCode::WRITE_MULTIPLE_COILS:
    case FunctionCode::WRITE_MULTIPLE_REGISTERS:
      // Write multiple responses: [Unit] [FC] [Addr] [Qty] [CRC] = 8 bytes
      expectedLength = 8;
      break;

    default:
      // Unknown function code - read until timeout or max size
      expectedLength = Constants::RTU_MAX_FRAME_SIZE;
      break;
    }
  }

  // Clamp to buffer capacity
  if (expectedLength > FrameBuffer::CAPACITY) {
    expectedLength = FrameBuffer::CAPACITY;
  }

  // Read remaining bytes if needed
  if (expectedLength > offset) {
    status = readWithTimeout(frame.data, offset, expectedLength, timeoutMs);
    if (status != Status::SUCCESS) {
      return status;
    }
  }

  frame.length = offset;
  stats_.bytesRx += offset;

  // Trace the received frame (before validation to capture all traffic)
  invokeTrace(TraceDirection::RX, frame.data, frame.length);

  // Verify CRC
  if (!verifyCrc(frame.data, frame.length)) {
    ++stats_.crcErrors;
    return Status::ERROR_CRC;
  }

  // Check for exception response
  if (isExceptionResponse(frame.data[1])) {
    ++stats_.exceptionsReceived;
    return Status::ERROR_EXCEPTION;
  }

  ++stats_.responsesReceived;
  return Status::SUCCESS;
}

Status ModbusRtuTransport::flush() noexcept {
  if (!isOpen()) {
    return Status::ERROR_NOT_CONFIGURED;
  }

  const auto UART_STATUS = device_->flush(true, true);
  return (UART_STATUS == uart::Status::SUCCESS) ? Status::SUCCESS : Status::ERROR_IO;
}

/* ----------------------------- Statistics ----------------------------- */

ModbusStats ModbusRtuTransport::stats() const noexcept { return stats_; }

void ModbusRtuTransport::resetStats() noexcept { stats_.reset(); }

/* ----------------------------- Info ----------------------------- */

const char* ModbusRtuTransport::description() const noexcept { return description_.c_str(); }

/* ----------------------------- Private Helpers ----------------------------- */

void ModbusRtuTransport::waitInterFrameDelay() noexcept {
  if (interFrameDelayUs_ > 0) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = static_cast<long>(interFrameDelayUs_) * 1000L;
    nanosleep(&ts, nullptr);
  }
}

Status ModbusRtuTransport::readWithTimeout(std::uint8_t* buf, std::size_t& offset,
                                           std::size_t needed, int timeoutMs) noexcept {
  while (offset < needed) {
    std::size_t bytesRead = 0;
    const auto UART_STATUS = device_->read(buf + offset, needed - offset, bytesRead, timeoutMs);

    if (UART_STATUS == uart::Status::WOULD_BLOCK) {
      ++stats_.timeouts;
      return Status::WOULD_BLOCK;
    }

    if (UART_STATUS == uart::Status::ERROR_TIMEOUT) {
      ++stats_.timeouts;
      return Status::ERROR_TIMEOUT;
    }

    if (UART_STATUS != uart::Status::SUCCESS) {
      ++stats_.ioErrors;
      return Status::ERROR_IO;
    }

    if (bytesRead == 0) {
      // No data available
      ++stats_.timeouts;
      return Status::ERROR_TIMEOUT;
    }

    offset += bytesRead;
  }

  return Status::SUCCESS;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex
