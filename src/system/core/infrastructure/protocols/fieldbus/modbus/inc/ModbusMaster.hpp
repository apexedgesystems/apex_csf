#ifndef APEX_PROTOCOLS_FIELDBUS_MODBUS_MASTER_HPP
#define APEX_PROTOCOLS_FIELDBUS_MODBUS_MASTER_HPP
/**
 * @file ModbusMaster.hpp
 * @brief High-level Modbus master (client) API.
 *
 * Provides a convenient interface for Modbus master operations over any
 * transport (RTU, TCP). Handles frame building, transaction management,
 * response parsing, and retry logic.
 *
 * Usage:
 *   ModbusRtuTransport transport(&uart, rtuConfig, 115200);
 *   transport.open();
 *   ModbusMaster master(&transport, masterConfig);
 *
 *   std::uint16_t registers[10];
 *   Status status = master.readHoldingRegisters(1, 0x0000, 10, registers);
 *
 * RT-Safety:
 *  - All read/write operations are RT-safe (use pre-allocated buffers)
 *  - No heap allocation on I/O path
 *  - Timeout-bounded operations
 */

#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusConfig.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusException.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusFrame.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStats.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusStatus.hpp"
#include "src/system/core/infrastructure/protocols/fieldbus/modbus/inc/ModbusTransport.hpp"

#include <cstdint>

namespace apex {
namespace protocols {
namespace fieldbus {
namespace modbus {

/* ----------------------------- ModbusResult ----------------------------- */

/**
 * @struct ModbusResult
 * @brief Result of a Modbus operation with optional exception info.
 *
 * Combines status with exception details when a slave returns an exception
 * response rather than refusing to respond.
 */
struct ModbusResult {
  Status status{Status::SUCCESS};                   ///< Operation status.
  ExceptionCode exceptionCode{ExceptionCode::NONE}; ///< Exception if status==ERROR_EXCEPTION.

  /**
   * @brief Check if operation succeeded.
   * @return True if status is SUCCESS.
   * @note RT-safe: no allocation or I/O.
   */
  [[nodiscard]] bool ok() const noexcept { return status == Status::SUCCESS; }

  /**
   * @brief Check if operation failed due to exception.
   * @return True if status is ERROR_EXCEPTION.
   * @note RT-safe: no allocation or I/O.
   */
  [[nodiscard]] bool isException() const noexcept { return status == Status::ERROR_EXCEPTION; }

  /**
   * @brief Create a success result.
   * @return ModbusResult with SUCCESS status.
   * @note RT-safe: no allocation or I/O.
   */
  static ModbusResult success() noexcept { return {Status::SUCCESS, ExceptionCode::NONE}; }

  /**
   * @brief Create an error result.
   * @param s Error status.
   * @return ModbusResult with error status.
   * @note RT-safe: no allocation or I/O.
   */
  static ModbusResult error(Status s) noexcept { return {s, ExceptionCode::NONE}; }

  /**
   * @brief Create an exception result.
   * @param code Exception code from slave.
   * @return ModbusResult with ERROR_EXCEPTION status.
   * @note RT-safe: no allocation or I/O.
   */
  static ModbusResult exception(ExceptionCode code) noexcept {
    return {Status::ERROR_EXCEPTION, code};
  }
};

/* ----------------------------- ModbusMaster ----------------------------- */

/**
 * @class ModbusMaster
 * @brief High-level Modbus master (client) API.
 *
 * Provides read/write operations for registers and coils. Uses the
 * underlying transport for actual communication.
 *
 * Lifecycle:
 *  1. Create transport (RTU or TCP) and call open()
 *  2. Construct ModbusMaster with transport pointer
 *  3. Use read/write methods for operations
 *  4. Close transport when done
 *
 * Ownership:
 *  - Does NOT own the transport. Caller must ensure transport outlives master.
 *
 * Thread Safety:
 *  - NOT thread-safe. External synchronization required.
 */
class ModbusMaster {
public:
  /**
   * @brief Construct a Modbus master.
   * @param transport Pointer to transport layer (must be open).
   * @param config Master configuration.
   * @note RT-safe: no allocation or I/O.
   */
  ModbusMaster(ModbusTransport* transport, const MasterConfig& config);

  ~ModbusMaster() = default;

  ModbusMaster(const ModbusMaster&) = delete;
  ModbusMaster& operator=(const ModbusMaster&) = delete;
  ModbusMaster(ModbusMaster&&) = default;
  ModbusMaster& operator=(ModbusMaster&&) = default;

  /* ----------------------------- Read Operations ----------------------------- */

  /**
   * @brief Read holding registers (FC 0x03).
   * @param unitAddr Slave unit address (1-247).
   * @param startAddr Starting register address.
   * @param quantity Number of registers to read (1-125).
   * @param values Output buffer for register values (big-endian from network).
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult readHoldingRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                  std::uint16_t quantity, std::uint16_t* values,
                                                  int timeoutMs = -1) noexcept;

  /**
   * @brief Read input registers (FC 0x04).
   * @param unitAddr Slave unit address (1-247).
   * @param startAddr Starting register address.
   * @param quantity Number of registers to read (1-125).
   * @param values Output buffer for register values.
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult readInputRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                std::uint16_t quantity, std::uint16_t* values,
                                                int timeoutMs = -1) noexcept;

  /**
   * @brief Read coils (FC 0x01).
   * @param unitAddr Slave unit address (1-247).
   * @param startAddr Starting coil address.
   * @param quantity Number of coils to read (1-2000).
   * @param values Output buffer for coil values (packed bits).
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult readCoils(std::uint8_t unitAddr, std::uint16_t startAddr,
                                       std::uint16_t quantity, std::uint8_t* values,
                                       int timeoutMs = -1) noexcept;

  /**
   * @brief Read discrete inputs (FC 0x02).
   * @param unitAddr Slave unit address (1-247).
   * @param startAddr Starting input address.
   * @param quantity Number of inputs to read (1-2000).
   * @param values Output buffer for input values (packed bits).
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult readDiscreteInputs(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                std::uint16_t quantity, std::uint8_t* values,
                                                int timeoutMs = -1) noexcept;

  /* ----------------------------- Write Operations ----------------------------- */

  /**
   * @brief Write single register (FC 0x06).
   * @param unitAddr Slave unit address (1-247, 0 for broadcast).
   * @param regAddr Register address.
   * @param value Register value to write.
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult writeSingleRegister(std::uint8_t unitAddr, std::uint16_t regAddr,
                                                 std::uint16_t value, int timeoutMs = -1) noexcept;

  /**
   * @brief Write multiple registers (FC 0x10).
   * @param unitAddr Slave unit address (1-247, 0 for broadcast).
   * @param startAddr Starting register address.
   * @param quantity Number of registers to write (1-123).
   * @param values Register values to write.
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult writeMultipleRegisters(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                    std::uint16_t quantity,
                                                    const std::uint16_t* values,
                                                    int timeoutMs = -1) noexcept;

  /**
   * @brief Write single coil (FC 0x05).
   * @param unitAddr Slave unit address (1-247, 0 for broadcast).
   * @param coilAddr Coil address.
   * @param value Coil value (true = ON, false = OFF).
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult writeSingleCoil(std::uint8_t unitAddr, std::uint16_t coilAddr,
                                             bool value, int timeoutMs = -1) noexcept;

  /**
   * @brief Write multiple coils (FC 0x0F).
   * @param unitAddr Slave unit address (1-247, 0 for broadcast).
   * @param startAddr Starting coil address.
   * @param quantity Number of coils to write (1-1968).
   * @param values Coil values (packed bits).
   * @param timeoutMs Timeout in milliseconds (-1 for default).
   * @return Result with status and optional exception code.
   * @note RT-safe: Uses pre-allocated buffers.
   */
  [[nodiscard]] ModbusResult writeMultipleCoils(std::uint8_t unitAddr, std::uint16_t startAddr,
                                                std::uint16_t quantity, const std::uint8_t* values,
                                                int timeoutMs = -1) noexcept;

  /* ----------------------------- Info ----------------------------- */

  /**
   * @brief Get the underlying transport.
   * @return Pointer to transport.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] ModbusTransport* transport() const noexcept { return transport_; }

  /**
   * @brief Get the current configuration.
   * @return Reference to config.
   * @note RT-safe: O(1).
   */
  [[nodiscard]] const MasterConfig& config() const noexcept { return config_; }

private:
  ModbusTransport* transport_;
  MasterConfig config_;
  FrameBuffer requestBuf_;
  FrameBuffer responseBuf_;

  /**
   * @brief Execute a transaction (send request, receive response).
   * @param timeoutMs Timeout in milliseconds.
   * @return Result with status and optional exception code.
   */
  ModbusResult executeTransaction(int timeoutMs) noexcept;

  /**
   * @brief Get effective timeout.
   * @param timeoutMs User-specified timeout (-1 for default).
   * @return Effective timeout in milliseconds.
   */
  int effectiveTimeout(int timeoutMs) const noexcept;
};

} // namespace modbus
} // namespace fieldbus
} // namespace protocols
} // namespace apex

#endif // APEX_PROTOCOLS_FIELDBUS_MODBUS_MASTER_HPP
