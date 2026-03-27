/**
 * @file CommandDeck.cpp
 * @brief Command channel handler implementation for ESP32-S3.
 *
 * Flow per poll():
 *  1. Read available bytes from USB CDC RX buffer
 *  2. Feed to SLIP streaming decoder
 *  3. On frame complete: validate CRC-16, dispatch command, send response
 */

#include "CommandDeck.hpp"

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <string.h>

namespace encryptor {

/* ----------------------------- Constants ----------------------------- */

static constexpr size_t CMD_RX_CHUNK_SIZE = 32;

/* ----------------------------- File Helpers ----------------------------- */

static uint16_t computeCrc16(const uint8_t* data, size_t len) noexcept {
  apex::checksums::crc::Crc16XmodemBitwise crc;
  uint16_t result = 0;
  crc.calculate(data, len, result);
  return result;
}

/* ----------------------------- CommandDeck Methods ----------------------------- */

CommandDeck::CommandDeck(apex::hal::IUart& cmdUart, KeyStore& keyStore, EncryptorEngine& engine,
                         OverheadTracker& tracker) noexcept
    : uart_(cmdUart), keyStore_(keyStore), engine_(engine), tracker_(tracker), slipState_{},
      slipCfg_{}, decodeBuf_{}, rspBuf_{}, slipEncodeBuf_{} {
  slipCfg_.maxFrameSize = MAX_CMD_FRAME;
  slipCfg_.allowEmptyFrame = false;
  slipCfg_.dropUntilEnd = true;
  slipCfg_.requireTrailingEnd = true;
}

void CommandDeck::poll() noexcept {
  uint8_t rxBuf[CMD_RX_CHUNK_SIZE];
  const size_t AVAIL = uart_.read(rxBuf, sizeof(rxBuf));
  if (AVAIL == 0) {
    return;
  }

  size_t pos = 0;
  while (pos < AVAIL) {
    const size_t PREV_LEN = slipState_.frameLen;
    const apex::compat::bytes_span INPUT(rxBuf + pos, AVAIL - pos);

    auto result = apex::protocols::slip::decodeChunk(
        slipState_, slipCfg_, INPUT, decodeBuf_ + PREV_LEN, MAX_CMD_FRAME - PREV_LEN);

    pos += result.bytesConsumed;

    if (result.frameCompleted) {
      const size_t FRAME_LEN = PREV_LEN + result.bytesProduced;
      processFrame(decodeBuf_, FRAME_LEN);
    }

    if (result.bytesConsumed == 0) {
      break;
    }
  }
}

void CommandDeck::processFrame(const uint8_t* frame, size_t len) noexcept {
  if (len < MIN_CMD_FRAME) {
    return;
  }

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
  size_t pos = 0;
  rspBuf_[pos++] = opcode;
  rspBuf_[pos++] = static_cast<uint8_t>(status);

  if (payload != nullptr && payloadLen > 0) {
    memcpy(rspBuf_ + pos, payload, payloadLen);
    pos += payloadLen;
  }

  const uint16_t RSP_CRC = computeCrc16(rspBuf_, pos);
  rspBuf_[pos++] = static_cast<uint8_t>(RSP_CRC >> 8);
  rspBuf_[pos++] = static_cast<uint8_t>(RSP_CRC & 0xFF);

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
  uint8_t FLAGS = tracker_.isFastForward() ? 0x01 : 0x00;
  if (engine_.isUartInitialized()) {
    FLAGS |= 0x02;
  }

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
