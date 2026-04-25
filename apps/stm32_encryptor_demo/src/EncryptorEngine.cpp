/**
 * @file EncryptorEngine.cpp
 * @brief Data channel encrypt pipeline implementation.
 *
 * Flow per poll():
 *  1. Read available bytes from UART1 RX buffer
 *  2. Feed to SLIP streaming decoder
 *  3. On frame complete: validate CRC-16, encrypt, SLIP-encode, transmit
 */

#include "EncryptorEngine.hpp"

#include "src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp"

#include <string.h>

namespace encryptor {

/* ----------------------------- Constants ----------------------------- */

/// Chunk size for reading from UART RX buffer per poll().
static constexpr size_t RX_CHUNK_SIZE = 64;

/* ----------------------------- EncryptorEngine Methods ----------------------------- */

EncryptorEngine::EncryptorEngine(apex::hal::IUart& dataUart, KeyStore* keyStore) noexcept
    : uart_(dataUart), keyStore_(keyStore), slipState_{}, slipCfg_{}, decodeBuf_{}, outputFrame_{},
      slipEncodeBuf_{}, activeKey_{}, activeKeyIndex_(0), hasKey_(false), nonce_{}, frameCount_(0),
      keyMode_(KeyMode::LOCKED), rotationIndex_(0), stats_{} {
  slipCfg_.maxFrameSize = MAX_INPUT_FRAME;
  slipCfg_.allowEmptyFrame = false;
  slipCfg_.dropUntilEnd = true;
  slipCfg_.requireTrailingEnd = true;
}

bool EncryptorEngine::loadActiveKey() noexcept {
  if (keyStore_ == nullptr) {
    return false;
  }

  uint8_t slot = 0;
  auto status = keyStore_->selectKey(activeKey_, slot);
  if (status != KeyStoreStatus::OK) {
    return false;
  }

  activeKeyIndex_ = slot;
  hasKey_ = true;
  return true;
}

void EncryptorEngine::setKey(const uint8_t* key, uint8_t index) noexcept {
  if (key != nullptr) {
    memcpy(activeKey_, key, AES_KEY_LEN);
    activeKeyIndex_ = index;
    hasKey_ = true;
  }
}

bool EncryptorEngine::lockToSlot(uint8_t slot) noexcept {
  if (keyStore_ == nullptr) {
    return false;
  }
  if (!keyStore_->isSlotPopulated(slot)) {
    return false;
  }
  auto status = keyStore_->readKey(slot, activeKey_);
  if (status != KeyStoreStatus::OK) {
    return false;
  }
  activeKeyIndex_ = slot;
  keyMode_ = KeyMode::LOCKED;
  hasKey_ = true;
  return true;
}

void EncryptorEngine::unlock() noexcept {
  keyMode_ = KeyMode::RANDOM;
  rotationIndex_ = 0;
}

void EncryptorEngine::resetNonce() noexcept {
  memset(nonce_, 0, GCM_NONCE_LEN);
  frameCount_ = 0;
}

void EncryptorEngine::poll() noexcept {
  // Read available bytes from UART
  uint8_t rxBuf[RX_CHUNK_SIZE];
  const size_t AVAIL = uart_.read(rxBuf, sizeof(rxBuf));
  if (AVAIL == 0) {
    return;
  }

  // Feed to SLIP decoder (may contain multiple frames or partial frames)
  size_t pos = 0;
  while (pos < AVAIL) {
    const size_t PREV_LEN = slipState_.frameLen;
    const apex::compat::bytes_span INPUT(rxBuf + pos, AVAIL - pos);

    auto result = apex::protocols::slip::decodeChunk(
        slipState_, slipCfg_, INPUT, decodeBuf_ + PREV_LEN, MAX_INPUT_FRAME - PREV_LEN);

    pos += result.bytesConsumed;

    if (result.frameCompleted) {
      const size_t FRAME_LEN = PREV_LEN + result.bytesProduced;
      processFrame(decodeBuf_, FRAME_LEN);
    }

    // Break on zero progress to avoid infinite loop
    if (result.bytesConsumed == 0) {
      break;
    }
  }
}

void EncryptorEngine::processFrame(const uint8_t* frame, size_t len) noexcept {
  // In RANDOM mode, rotate through populated key slots
  if (keyMode_ == KeyMode::RANDOM && keyStore_ != nullptr) {
    const uint8_t COUNT = keyStore_->populatedCount();
    if (COUNT == 0) {
      ++stats_.framesErr;
      return;
    }
    const uint16_t BITMAP = keyStore_->bitmap();
    const uint8_t TARGET = rotationIndex_ % COUNT;
    uint8_t found = 0;
    for (uint8_t i = 0; i < KEY_SLOT_COUNT; ++i) {
      if (BITMAP & (1U << i)) {
        if (found == TARGET) {
          static_cast<void>(keyStore_->readKey(i, activeKey_));
          activeKeyIndex_ = i;
          hasKey_ = true;
          break;
        }
        ++found;
      }
    }
    ++rotationIndex_;
  }

  // Reject if no key is loaded
  if (!hasKey_) {
    ++stats_.framesErr;
    return;
  }

  // Minimum: 1 byte plaintext + 2 bytes CRC
  if (len < MIN_INPUT_FRAME) {
    ++stats_.framesErr;
    return;
  }

  // Extract CRC-16 from last 2 bytes (big-endian per design doc)
  const size_t PT_LEN = len - 2;
  const uint16_t RECEIVED_CRC =
      static_cast<uint16_t>((static_cast<uint16_t>(frame[len - 2]) << 8) | frame[len - 1]);

  if (!validateCrc16Xmodem(frame, PT_LEN, RECEIVED_CRC)) {
    ++stats_.framesErr;
    return;
  }

  // Encrypt plaintext with AES-256-GCM
  uint8_t* ct = outputFrame_ + 1 + GCM_NONCE_LEN;
  uint8_t tag[GCM_TAG_LEN];

  auto gcmResult = apex::encryption::mcu::aes256GcmEncrypt(
      activeKey_, nonce_, frame, static_cast<uint32_t>(PT_LEN), nullptr, 0, ct, tag);

  if (gcmResult.status != apex::encryption::mcu::GcmStatus::OK) {
    ++stats_.framesErr;
    return;
  }

  // Build output frame: key_index(1) + nonce(12) + ciphertext(N) + tag(16)
  outputFrame_[0] = activeKeyIndex_;
  memcpy(outputFrame_ + 1, nonce_, GCM_NONCE_LEN);
  memcpy(ct + PT_LEN, tag, GCM_TAG_LEN);

  const size_t OUTPUT_LEN = 1 + GCM_NONCE_LEN + PT_LEN + GCM_TAG_LEN;

  // SLIP-encode output frame
  const apex::compat::bytes_span OUTPUT_SPAN(outputFrame_, OUTPUT_LEN);
  auto slipResult = apex::protocols::slip::encode(OUTPUT_SPAN, slipEncodeBuf_, MAX_SLIP_ENCODED);

  if (slipResult.status != apex::protocols::slip::Status::OK) {
    ++stats_.framesErr;
    return;
  }

  // Transmit SLIP-encoded frame
  uart_.write(slipEncodeBuf_, slipResult.bytesProduced);

  // Update stats, frame counter, and nonce
  ++stats_.framesOk;
  stats_.bytesIn += static_cast<uint32_t>(PT_LEN);
  stats_.bytesOut += static_cast<uint32_t>(slipResult.bytesProduced);
  ++frameCount_;
  encryptor::incrementNonce(nonce_, GCM_NONCE_LEN);
}

} // namespace encryptor
