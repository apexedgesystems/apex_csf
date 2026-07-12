#ifndef APEX_STM32_ENCRYPTOR_COMMAND_DECK_HPP
#define APEX_STM32_ENCRYPTOR_COMMAND_DECK_HPP
/**
 * @file CommandDeck.hpp
 * @brief Command channel handler for the stm32_encryptor_demo.
 *
 * Owns the UART2 (VCP) command pipeline: SLIP decode -> CRC validate ->
 * dispatch -> build response -> CRC append -> SLIP encode -> transmit.
 *
 * All buffers are statically allocated. No heap usage.
 *
 * @note RT-safe after construction (poll() is O(1) per call, O(n) per frame).
 * @note Flash operations (key write/erase) are NOT RT-safe but execute
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
 *  1. Construct with UART2, KeyStore, and EncryptorEngine references
 *  2. Call poll() at 20 Hz from commandTask
 *
 * @note RT-safe: poll() has no allocations, bounded execution time.
 */
class CommandDeck {
public:
  /**
   * @brief Construct command handler.
   * @param cmdUart UART for command channel I/O (USART2 VCP).
   * @param keyStore Key store for key CRUD operations.
   * @param engine Encryptor engine for stats, nonce, and mode control.
   * @param tracker Overhead tracker for DWT stats and fast-forward control.
   */
  CommandDeck(apex::hal::IUart& cmdUart, KeyStore& keyStore, EncryptorEngine& engine,
              OverheadTracker& tracker) noexcept;

  /**
   * @brief Poll the command channel for incoming commands.
   *
   * Called from commandTask at 20 Hz. Reads available bytes from UART2,
   * feeds to SLIP decoder, dispatches complete frames.
   *
   * @note RT-safe (except when dispatching flash write/erase commands).
   */
  void poll() noexcept;

private:
  apex::hal::IUart& uart_;
  KeyStore& keyStore_;
  EncryptorEngine& engine_;
  OverheadTracker& tracker_;

  // SLIP decoder state (separate from data channel)
  apex::protocols::slip::DecodeState slipState_;
  apex::protocols::slip::DecodeConfig slipCfg_;

  // Work buffers
  uint8_t decodeBuf_[MAX_CMD_FRAME];    ///< SLIP decode output.
  uint8_t rspBuf_[MAX_RSP_FRAME];       ///< Response assembly.
  uint8_t slipEncodeBuf_[MAX_RSP_SLIP]; ///< SLIP-encoded response for TX.

  /**
   * @brief Process a complete SLIP-decoded command frame.
   * @param frame Decoded frame data.
   * @param len Frame length in bytes.
   */
  void processFrame(const uint8_t* frame, size_t len) noexcept;

  /**
   * @brief Build and transmit a SLIP-encoded response.
   * @param opcode Command opcode (echoed in response).
   * @param status Response status code.
   * @param payload Response payload data (may be nullptr).
   * @param payloadLen Payload length in bytes.
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

#endif // APEX_STM32_ENCRYPTOR_COMMAND_DECK_HPP
