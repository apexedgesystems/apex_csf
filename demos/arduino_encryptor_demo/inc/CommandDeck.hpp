#ifndef APEX_ARDUINO_ENCRYPTOR_COMMAND_DECK_HPP
#define APEX_ARDUINO_ENCRYPTOR_COMMAND_DECK_HPP
/**
 * @file CommandDeck.hpp
 * @brief Command channel handler for the arduino_encryptor_demo.
 *
 * Unlike the STM32 version, this does NOT own SLIP decoding. The main loop
 * handles SLIP decode and channel routing, then calls processFrame() with
 * the already-decoded command payload (no channel prefix).
 *
 * Response frames are SLIP-encoded with a CHANNEL_CMD prefix before TX.
 *
 * All buffers are statically allocated. No heap usage.
 *
 * @note RT-safe after construction (processFrame() is O(1) per frame).
 * @note EEPROM operations (key write/erase) are NOT RT-safe but execute
 *       infrequently and only in response to explicit commands.
 */

#include "EncryptorConfig.hpp"
#include "EncryptorEngine.hpp"
#include "KeyStore.hpp"
#include "OverheadTracker.hpp"
#include "src/system/core/hal/base/IUart.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"

namespace encryptor {

/* ----------------------------- CommandDeck ----------------------------- */

/**
 * @class CommandDeck
 * @brief Command channel processor for key management and diagnostics.
 *
 * Lifecycle:
 *  1. Construct with shared UART, KeyStore, EncryptorEngine, OverheadTracker
 *  2. main.cpp calls processFrame() when a command channel frame arrives
 *
 * @note RT-safe: processFrame() has no allocations, bounded execution time.
 */
class CommandDeck {
public:
  /**
   * @brief Construct command handler.
   * @param uart Shared UART for transmitting responses.
   * @param keyStore Key store for key CRUD operations.
   * @param engine Encryptor engine for stats, nonce, and mode control.
   * @param tracker Overhead tracker for stats and fast-forward control.
   */
  CommandDeck(apex::hal::IUart& uart, KeyStore& keyStore, EncryptorEngine& engine,
              OverheadTracker& tracker) noexcept;

  /**
   * @brief Process a complete command frame (without channel prefix).
   *
   * Called by main.cpp after SLIP decode and channel routing.
   * Validates CRC-16, dispatches command, builds and transmits response
   * with CHANNEL_CMD prefix.
   *
   * @param frame Decoded frame data (opcode + payload + CRC-16).
   * @param len Frame length in bytes.
   * @note RT-safe (except when dispatching EEPROM write/erase commands).
   */
  void processFrame(const uint8_t* frame, size_t len) noexcept;

private:
  apex::hal::IUart& uart_;
  KeyStore& keyStore_;
  EncryptorEngine& engine_;
  OverheadTracker& tracker_;

  // Work buffers
  uint8_t rspBuf_[MAX_RSP_FRAME];       ///< Response assembly.
  uint8_t slipEncodeBuf_[MAX_RSP_SLIP]; ///< SLIP-encoded response for TX.

  /**
   * @brief Build and transmit a SLIP-encoded response with channel prefix.
   */
  void sendResponse(uint8_t opcode, CmdStatus status, const uint8_t* payload = nullptr,
                    size_t payloadLen = 0) noexcept;

  /* ----------------------------- Command Handlers ----------------------------- */

  void handleKeyStoreWrite(const uint8_t* payload, size_t len) noexcept;
  void handleKeyStoreRead(const uint8_t* payload, size_t len) noexcept;
  void handleKeyStoreErase() noexcept;
  void handleKeyStoreStatus() noexcept;
  void handleKeyLock(const uint8_t* payload, size_t len) noexcept;
  void handleKeyUnlock() noexcept;
  void handleKeyModeStatus() noexcept;
  void handleIvReset() noexcept;
  void handleIvStatus() noexcept;
  void handleStats() noexcept;
  void handleStatsReset() noexcept;
  void handleOverhead() noexcept;
  void handleOverheadReset() noexcept;
  void handleFastForward(const uint8_t* payload, size_t len) noexcept;
};

} // namespace encryptor

#endif // APEX_ARDUINO_ENCRYPTOR_COMMAND_DECK_HPP
