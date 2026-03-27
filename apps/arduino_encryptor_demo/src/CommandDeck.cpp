/**
 * @file CommandDeck.cpp
 * @brief Command channel handler implementation (Arduino version).
 *
 * processFrame() is called by main.cpp after SLIP decode and channel routing.
 * Validates CRC-16, dispatches command, builds and transmits SLIP-encoded
 * response with CHANNEL_CMD prefix.
 */

#include "CommandDeck.hpp"

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <string.h>

namespace encryptor {

/* ----------------------------- File Helpers ----------------------------- */

/**
 * @brief Compute CRC-16/XMODEM over data.
 * @param data Input bytes.
 * @param len Number of bytes.
 * @return Computed CRC-16 value.
 */
static uint16_t computeCrc16(const uint8_t* data, size_t len) noexcept {
  apex::checksums::crc::Crc16XmodemBitwise crc;
  uint16_t result = 0;
  crc.calculate(data, len, result);
  return result;
}

/* ----------------------------- CommandDeck Methods ----------------------------- */

CommandDeck::CommandDeck(apex::hal::IUart& uart, KeyStore& keyStore, EncryptorEngine& engine,
                         OverheadTracker& tracker) noexcept
    : uart_(uart), keyStore_(keyStore), engine_(engine), tracker_(tracker), rspBuf_{},
      slipEncodeBuf_{} {}

void CommandDeck::processFrame(const uint8_t* frame, size_t len) noexcept {
  // Minimum: opcode(1) + CRC(2)
  if (len < MIN_CMD_PAYLOAD) {
    return;
  }

  // Extract and validate CRC-16 (big-endian, last 2 bytes)
  const size_t BODY_LEN = len - 2;
  const uint16_t RECEIVED_CRC =
      static_cast<uint16_t>((static_cast<uint16_t>(frame[len - 2]) << 8) | frame[len - 1]);
  const uint16_t COMPUTED_CRC = computeCrc16(frame, BODY_LEN);

  if (RECEIVED_CRC != COMPUTED_CRC) {
    return;
  }

  const uint8_t OPCODE = frame[0];
  const uint8_t* PAYLOAD = frame + 1;
  const size_t PAYLOAD_LEN = BODY_LEN - 1;

  switch (static_cast<CmdOpcode>(OPCODE)) {
  case CmdOpcode::KEY_STORE_WRITE:
    handleKeyStoreWrite(PAYLOAD, PAYLOAD_LEN);
    break;
  case CmdOpcode::KEY_STORE_READ:
    handleKeyStoreRead(PAYLOAD, PAYLOAD_LEN);
    break;
  case CmdOpcode::KEY_STORE_ERASE:
    handleKeyStoreErase();
    break;
  case CmdOpcode::KEY_STORE_STATUS:
    handleKeyStoreStatus();
    break;
  case CmdOpcode::KEY_LOCK:
    handleKeyLock(PAYLOAD, PAYLOAD_LEN);
    break;
  case CmdOpcode::KEY_UNLOCK:
    handleKeyUnlock();
    break;
  case CmdOpcode::KEY_MODE_STATUS:
    handleKeyModeStatus();
    break;
  case CmdOpcode::IV_RESET:
    handleIvReset();
    break;
  case CmdOpcode::IV_STATUS:
    handleIvStatus();
    break;
  case CmdOpcode::STATS:
    handleStats();
    break;
  case CmdOpcode::STATS_RESET:
    handleStatsReset();
    break;
  case CmdOpcode::OVERHEAD:
    handleOverhead();
    break;
  case CmdOpcode::OVERHEAD_RESET:
    handleOverheadReset();
    break;
  case CmdOpcode::FASTFORWARD:
    handleFastForward(PAYLOAD, PAYLOAD_LEN);
    break;
  default:
    sendResponse(OPCODE, CmdStatus::ERR_INVALID_CMD);
    break;
  }
}

void CommandDeck::sendResponse(uint8_t opcode, CmdStatus status, const uint8_t* payload,
                               size_t payloadLen) noexcept {
  // Build response: channel(1) + opcode(1) + status(1) + payload(N) + CRC(2)
  // Channel prefix is first byte of the SLIP payload
  size_t pos = 0;
  rspBuf_[pos++] = CHANNEL_CMD;
  rspBuf_[pos++] = opcode;
  rspBuf_[pos++] = static_cast<uint8_t>(status);

  if (payload != nullptr && payloadLen > 0) {
    memcpy(rspBuf_ + pos, payload, payloadLen);
    pos += payloadLen;
  }

  // Append CRC-16 (big-endian) over opcode + status + payload (skip channel byte)
  const uint16_t RSP_CRC = computeCrc16(rspBuf_ + 1, pos - 1);
  rspBuf_[pos++] = static_cast<uint8_t>(RSP_CRC >> 8);
  rspBuf_[pos++] = static_cast<uint8_t>(RSP_CRC & 0xFF);

  // SLIP-encode the entire buffer (channel + opcode + status + payload + CRC)
  const apex::compat::bytes_span RSP_SPAN(rspBuf_, pos);
  auto slipResult = apex::protocols::slip::encode(RSP_SPAN, slipEncodeBuf_, MAX_RSP_SLIP);

  if (slipResult.status != apex::protocols::slip::Status::OK) {
    return;
  }

  uart_.write(slipEncodeBuf_, slipResult.bytesProduced);
}

/* ----------------------------- Command Handlers ----------------------------- */

void CommandDeck::handleKeyStoreWrite(const uint8_t* payload, size_t len) noexcept {
  if (len != 1 + AES_KEY_LEN) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_WRITE), CmdStatus::ERR_BAD_PAYLOAD);
    return;
  }

  const uint8_t SLOT = payload[0];
  if (SLOT >= KEY_SLOT_COUNT) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_WRITE), CmdStatus::ERR_KEY_SLOT);
    return;
  }

  auto ks = keyStore_.writeKey(SLOT, payload + 1);
  if (ks != KeyStoreStatus::OK) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_WRITE), CmdStatus::ERR_FLASH);
    return;
  }

  // If engine is LOCKED on this slot, reload the active key
  if (engine_.keyMode() == KeyMode::LOCKED && engine_.activeKeyIndex() == SLOT) {
    static_cast<void>(engine_.lockToSlot(SLOT));
  }

  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_WRITE), CmdStatus::OK);
}

void CommandDeck::handleKeyStoreRead(const uint8_t* payload, size_t len) noexcept {
  if (len != 1) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_READ), CmdStatus::ERR_BAD_PAYLOAD);
    return;
  }

  const uint8_t SLOT = payload[0];
  uint8_t keyBuf[AES_KEY_LEN];

  auto ks = keyStore_.readKey(SLOT, keyBuf);
  if (ks == KeyStoreStatus::ERROR_INVALID_SLOT) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_READ), CmdStatus::ERR_KEY_SLOT);
    return;
  }
  if (ks == KeyStoreStatus::ERROR_SLOT_EMPTY) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_READ), CmdStatus::ERR_KEY_SLOT);
    return;
  }
  if (ks != KeyStoreStatus::OK) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_READ), CmdStatus::ERR_FLASH);
    return;
  }

  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_READ), CmdStatus::OK, keyBuf, AES_KEY_LEN);
}

void CommandDeck::handleKeyStoreErase() noexcept {
  auto ks = keyStore_.eraseAll();
  if (ks != KeyStoreStatus::OK) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_ERASE), CmdStatus::ERR_FLASH);
    return;
  }

  engine_.unlock();
  engine_.clearActiveKey();

  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_ERASE), CmdStatus::OK);
}

void CommandDeck::handleKeyStoreStatus() noexcept {
  uint8_t payload[3];
  payload[0] = keyStore_.populatedCount();
  const uint16_t BITMAP = keyStore_.bitmap();
  payload[1] = static_cast<uint8_t>(BITMAP & 0xFF);
  payload[2] = static_cast<uint8_t>((BITMAP >> 8) & 0xFF);

  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_STORE_STATUS), CmdStatus::OK, payload,
               sizeof(payload));
}

void CommandDeck::handleKeyLock(const uint8_t* payload, size_t len) noexcept {
  if (len != 1) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_LOCK), CmdStatus::ERR_BAD_PAYLOAD);
    return;
  }

  const uint8_t SLOT = payload[0];
  if (!engine_.lockToSlot(SLOT)) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_LOCK), CmdStatus::ERR_KEY_SLOT);
    return;
  }

  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_LOCK), CmdStatus::OK);
}

void CommandDeck::handleKeyUnlock() noexcept {
  engine_.unlock();
  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_UNLOCK), CmdStatus::OK);
}

void CommandDeck::handleKeyModeStatus() noexcept {
  uint8_t payload[2];
  payload[0] = static_cast<uint8_t>(engine_.keyMode());
  payload[1] = engine_.activeKeyIndex();

  sendResponse(static_cast<uint8_t>(CmdOpcode::KEY_MODE_STATUS), CmdStatus::OK, payload,
               sizeof(payload));
}

void CommandDeck::handleIvReset() noexcept {
  engine_.resetNonce();
  sendResponse(static_cast<uint8_t>(CmdOpcode::IV_RESET), CmdStatus::OK);
}

void CommandDeck::handleIvStatus() noexcept {
  uint8_t payload[GCM_NONCE_LEN + 4];

  memcpy(payload, engine_.nonce(), GCM_NONCE_LEN);

  const uint32_t FC = engine_.frameCount();
  memcpy(payload + GCM_NONCE_LEN, &FC, sizeof(FC));

  sendResponse(static_cast<uint8_t>(CmdOpcode::IV_STATUS), CmdStatus::OK, payload, sizeof(payload));
}

void CommandDeck::handleStats() noexcept {
  const auto& S = engine_.stats();
  uint8_t payload[16];

  memcpy(payload + 0, &S.framesOk, 4);
  memcpy(payload + 4, &S.framesErr, 4);
  memcpy(payload + 8, &S.bytesIn, 4);
  memcpy(payload + 12, &S.bytesOut, 4);

  sendResponse(static_cast<uint8_t>(CmdOpcode::STATS), CmdStatus::OK, payload, sizeof(payload));
}

void CommandDeck::handleStatsReset() noexcept {
  engine_.resetStats();
  sendResponse(static_cast<uint8_t>(CmdOpcode::STATS_RESET), CmdStatus::OK);
}

void CommandDeck::handleOverhead() noexcept {
  const auto& S = tracker_.stats();
  const uint32_t BUDGET = tracker_.budgetCycles();
  const uint8_t FLAGS = tracker_.isFastForward() ? 0x01 : 0x00;

  uint8_t payload[21];
  memcpy(payload + 0, &S.lastCycles, 4);
  memcpy(payload + 4, &S.minCycles, 4);
  memcpy(payload + 8, &S.maxCycles, 4);
  memcpy(payload + 12, &S.sampleCount, 4);
  memcpy(payload + 16, &BUDGET, 4);
  payload[20] = FLAGS;

  sendResponse(static_cast<uint8_t>(CmdOpcode::OVERHEAD), CmdStatus::OK, payload, sizeof(payload));
}

void CommandDeck::handleOverheadReset() noexcept {
  tracker_.resetStats();
  sendResponse(static_cast<uint8_t>(CmdOpcode::OVERHEAD_RESET), CmdStatus::OK);
}

void CommandDeck::handleFastForward(const uint8_t* payload, size_t len) noexcept {
  if (len != 1) {
    sendResponse(static_cast<uint8_t>(CmdOpcode::FASTFORWARD), CmdStatus::ERR_BAD_PAYLOAD);
    return;
  }

  const bool ENABLE = (payload[0] != 0);
  tracker_.setFastForward(ENABLE);

  sendResponse(static_cast<uint8_t>(CmdOpcode::FASTFORWARD), CmdStatus::OK);
}

} // namespace encryptor
