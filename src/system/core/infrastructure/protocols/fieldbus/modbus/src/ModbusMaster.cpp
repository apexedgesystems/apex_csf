/**
 * @file ModbusMaster.cpp
 * @brief Implementation of high-level Modbus master API.
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusMaster.hpp"

#include <cstring>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- Construction ----------------------------- */

ModbusMaster::ModbusMaster(ModbusTransport* transport, const MasterConfig& config)
    : transport_(transport), config_(config), requestBuf_(), responseBuf_() {}

/* ----------------------------- Read Operations ----------------------------- */

ModbusResult ModbusMaster::readHoldingRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                std::uint16_t quantity, std::uint16_t* values,
                                                int timeoutMs) noexcept {
  if (values == nullptr) {
    return ModbusResult::error(Status::ERROR_INVALID_ARG);
  }

  // Build request
  const Status BUILD_STATUS =
      buildReadHoldingRegistersRequest(requestBuf_, unitAddr, startAddr, quantity);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  ModbusResult result = executeTransaction(effectiveTimeout(timeoutMs));
  if (!result.ok()) {
    return result;
  }

  // Parse response - extract register values
  const std::size_t EXTRACTED =
      extractRegistersFromResponse(responseBuf_.data, responseBuf_.length, values, quantity);
  if (EXTRACTED != quantity) {
    return ModbusResult::error(Status::ERROR_FRAME);
  }

  return ModbusResult::success();
}

ModbusResult ModbusMaster::readInputRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                              std::uint16_t quantity, std::uint16_t* values,
                                              int timeoutMs) noexcept {
  if (values == nullptr) {
    return ModbusResult::error(Status::ERROR_INVALID_ARG);
  }

  // Build request
  const Status BUILD_STATUS =
      buildReadInputRegistersRequest(requestBuf_, unitAddr, startAddr, quantity);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  ModbusResult result = executeTransaction(effectiveTimeout(timeoutMs));
  if (!result.ok()) {
    return result;
  }

  // Parse response
  const std::size_t EXTRACTED =
      extractRegistersFromResponse(responseBuf_.data, responseBuf_.length, values, quantity);
  if (EXTRACTED != quantity) {
    return ModbusResult::error(Status::ERROR_FRAME);
  }

  return ModbusResult::success();
}

ModbusResult ModbusMaster::readCoils(std::uint8_t unitAddr, std::uint16_t startAddr,
                                     std::uint16_t quantity, std::uint8_t* values,
                                     int timeoutMs) noexcept {
  if (values == nullptr) {
    return ModbusResult::error(Status::ERROR_INVALID_ARG);
  }

  // Build request
  const Status BUILD_STATUS = buildReadCoilsRequest(requestBuf_, unitAddr, startAddr, quantity);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  ModbusResult result = executeTransaction(effectiveTimeout(timeoutMs));
  if (!result.ok()) {
    return result;
  }

  // Parse response - coils are packed as bits
  const std::size_t BYTE_COUNT = (quantity + 7) / 8;
  const std::size_t EXTRACTED =
      extractCoilsFromResponse(responseBuf_.data, responseBuf_.length, values, BYTE_COUNT);
  if (EXTRACTED != BYTE_COUNT) {
    return ModbusResult::error(Status::ERROR_FRAME);
  }

  return ModbusResult::success();
}

ModbusResult ModbusMaster::readDiscreteInputs(std::uint8_t unitAddr, std::uint16_t startAddr,
                                              std::uint16_t quantity, std::uint8_t* values,
                                              int timeoutMs) noexcept {
  if (values == nullptr) {
    return ModbusResult::error(Status::ERROR_INVALID_ARG);
  }

  // Build request
  const Status BUILD_STATUS =
      buildReadDiscreteInputsRequest(requestBuf_, unitAddr, startAddr, quantity);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  ModbusResult result = executeTransaction(effectiveTimeout(timeoutMs));
  if (!result.ok()) {
    return result;
  }

  // Parse response - inputs are packed as bits
  const std::size_t BYTE_COUNT = (quantity + 7) / 8;
  const std::size_t EXTRACTED =
      extractCoilsFromResponse(responseBuf_.data, responseBuf_.length, values, BYTE_COUNT);
  if (EXTRACTED != BYTE_COUNT) {
    return ModbusResult::error(Status::ERROR_FRAME);
  }

  return ModbusResult::success();
}

/* ----------------------------- Write Operations ----------------------------- */

ModbusResult ModbusMaster::writeSingleRegister(std::uint8_t unitAddr, std::uint16_t regAddr,
                                               std::uint16_t value, int timeoutMs) noexcept {
  // Build request
  const Status BUILD_STATUS =
      buildWriteSingleRegisterRequest(requestBuf_, unitAddr, regAddr, value);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  // For broadcast (unitAddr == 0), we don't expect a response
  if (unitAddr == 0) {
    const Status SEND_STATUS = transport_->sendRequest(requestBuf_, effectiveTimeout(timeoutMs));
    return (SEND_STATUS == Status::SUCCESS) ? ModbusResult::success()
                                            : ModbusResult::error(SEND_STATUS);
  }

  return executeTransaction(effectiveTimeout(timeoutMs));
}

ModbusResult ModbusMaster::writeMultipleRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                  std::uint16_t quantity,
                                                  const std::uint16_t* values,
                                                  int timeoutMs) noexcept {
  if (values == nullptr && quantity > 0) {
    return ModbusResult::error(Status::ERROR_INVALID_ARG);
  }

  // Build request
  const Status BUILD_STATUS =
      buildWriteMultipleRegistersRequest(requestBuf_, unitAddr, startAddr, values, quantity);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  if (unitAddr == 0) {
    const Status SEND_STATUS = transport_->sendRequest(requestBuf_, effectiveTimeout(timeoutMs));
    return (SEND_STATUS == Status::SUCCESS) ? ModbusResult::success()
                                            : ModbusResult::error(SEND_STATUS);
  }

  return executeTransaction(effectiveTimeout(timeoutMs));
}

ModbusResult ModbusMaster::writeSingleCoil(std::uint8_t unitAddr, std::uint16_t coilAddr,
                                           bool value, int timeoutMs) noexcept {
  // Build request
  const Status BUILD_STATUS = buildWriteSingleCoilRequest(requestBuf_, unitAddr, coilAddr, value);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  if (unitAddr == 0) {
    const Status SEND_STATUS = transport_->sendRequest(requestBuf_, effectiveTimeout(timeoutMs));
    return (SEND_STATUS == Status::SUCCESS) ? ModbusResult::success()
                                            : ModbusResult::error(SEND_STATUS);
  }

  return executeTransaction(effectiveTimeout(timeoutMs));
}

ModbusResult ModbusMaster::writeMultipleCoils(std::uint8_t unitAddr, std::uint16_t startAddr,
                                              std::uint16_t quantity, const std::uint8_t* values,
                                              int timeoutMs) noexcept {
  if (values == nullptr && quantity > 0) {
    return ModbusResult::error(Status::ERROR_INVALID_ARG);
  }

  // Build request
  const Status BUILD_STATUS =
      buildWriteMultipleCoilsRequest(requestBuf_, unitAddr, startAddr, quantity, values);
  if (BUILD_STATUS != Status::SUCCESS) {
    return ModbusResult::error(BUILD_STATUS);
  }

  // Execute transaction
  if (unitAddr == 0) {
    const Status SEND_STATUS = transport_->sendRequest(requestBuf_, effectiveTimeout(timeoutMs));
    return (SEND_STATUS == Status::SUCCESS) ? ModbusResult::success()
                                            : ModbusResult::error(SEND_STATUS);
  }

  return executeTransaction(effectiveTimeout(timeoutMs));
}

/* ----------------------------- Private Helpers ----------------------------- */

ModbusResult ModbusMaster::executeTransaction(int timeoutMs) noexcept {
  // Send request
  Status status = transport_->sendRequest(requestBuf_, timeoutMs);
  if (status != Status::SUCCESS) {
    return ModbusResult::error(status);
  }

  // Receive response
  responseBuf_.reset();
  status = transport_->receiveResponse(responseBuf_, timeoutMs);

  if (status == Status::ERROR_EXCEPTION) {
    // Parse exception code from response
    if (responseBuf_.length >= 3) {
      const ExceptionCode CODE = static_cast<ExceptionCode>(responseBuf_.data[2]);
      return ModbusResult::exception(CODE);
    }
    return ModbusResult::exception(ExceptionCode::NONE);
  }

  if (status != Status::SUCCESS) {
    return ModbusResult::error(status);
  }

  // Validate response unit address if configured
  if (config_.validateUnitAddress && responseBuf_.length >= 1) {
    if (responseBuf_.data[0] != requestBuf_.data[0]) {
      return ModbusResult::error(Status::ERROR_FRAME);
    }
  }

  // Validate response function code if configured
  if (config_.validateFunctionCode && responseBuf_.length >= 2) {
    const std::uint8_t REQ_FC = requestBuf_.data[1];
    const std::uint8_t RESP_FC = responseBuf_.data[1];
    if (RESP_FC != REQ_FC && !isExceptionResponse(RESP_FC)) {
      return ModbusResult::error(Status::ERROR_FRAME);
    }
  }

  return ModbusResult::success();
}

int ModbusMaster::effectiveTimeout(int timeoutMs) const noexcept {
  // Use default from transport config if not specified
  // For now, just use a reasonable default
  return (timeoutMs >= 0) ? timeoutMs : 1000;
}

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex
