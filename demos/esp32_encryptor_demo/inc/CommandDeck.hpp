#ifndef APEX_ESP32_ENCRYPTOR_COMMAND_DECK_HPP
#define APEX_ESP32_ENCRYPTOR_COMMAND_DECK_HPP
/**
 * @file CommandDeck.hpp
 * @brief Command channel handler for the esp32_encryptor_demo.
 *
 * Owns the USB CDC command pipeline: SLIP decode -> CRC validate ->
 * dispatch -> build response -> CRC append -> SLIP encode -> transmit.
 *
 * All buffers are statically allocated. No heap usage.
 *
 * @note RT-safe after construction (poll() is O(1) per call, O(n) per frame).
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
 *  1. Construct with USB CDC, KeyStore, EncryptorEngine, and OverheadTracker
 *  2. Call poll() at 20 Hz from commandTask
 */
class CommandDeck {
public:
  CommandDeck(apex::hal::IUart& cmdUart, KeyStore& keyStore, EncryptorEngine& engine,
              OverheadTracker& tracker) noexcept;

  void poll() noexcept;

private:
  apex::hal::IUart& uart_;
  KeyStore& keyStore_;
  EncryptorEngine& engine_;
  OverheadTracker& tracker_;

  apex::protocols::slip::DecodeState slipState_;
  apex::protocols::slip::DecodeConfig slipCfg_;

  uint8_t decodeBuf_[MAX_CMD_FRAME];
  uint8_t rspBuf_[MAX_RSP_FRAME];
  uint8_t slipEncodeBuf_[MAX_RSP_SLIP];

  void processFrame(const uint8_t* frame, size_t len) noexcept;

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

#endif // APEX_ESP32_ENCRYPTOR_COMMAND_DECK_HPP
