/**
 * @file EncryptorEngine.cpp
 * @brief Data channel encrypt pipeline implementation (Arduino version).
 *
 * Unlike the STM32 version, processFrame() is called externally by main.cpp
 * after SLIP decode and channel routing. The engine only handles:
 *   CRC validate -> AES-256-GCM encrypt -> SLIP encode (with channel prefix) -> TX
 */

#include "EncryptorEngine.hpp"

#include "src/utilities/encryption/lite/inc/Aes256GcmLite.hpp"

#include <string.h>

namespace encryptor {

/* ----------------------------- EncryptorEngine Methods ----------------------------- */

EncryptorEngine::EncryptorEngine(apex::hal::IUart& uart, KeyStore* keyStore) noexcept
    : uart_(uart), keyStore_(keyStore), outputFrame_{}, slipEncodeBuf_{}, activeKey_{},
      activeKeyIndex_(0), hasKey_(false), nonce_{}, frameCount_(0), keyMode_(KeyMode::LOCKED),
      rotationIndex_(0), stats_{} {}

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

void EncryptorEngine::processFrame(const uint8_t* frame, size_t len) noexcept {
  // In RANDOM mode, rotate through populated key slots
  if (keyMode_ == KeyMode::RANDOM && keyStore_ != nullptr) {
    const uint8_t COUNT = keyStore_->populatedCount();
    if (COUNT == 0) {
      ++stats_.framesErr;
      return;
    }
    const uint8_t BITMAP = keyStore_->bitmap();
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
  if (len < MIN_DATA_PAYLOAD) {
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

  // Build output frame in-place: channel(1) + key_index(1) + nonce(12) + ct(N) + tag(16).
  // Channel prefix and header are written first, then encrypt writes ct and tag
  // directly into outputFrame_ to avoid any temporary stack buffers.
  outputFrame_[0] = CHANNEL_DATA;
  outputFrame_[1] = activeKeyIndex_;
  memcpy(outputFrame_ + 2, nonce_, GCM_NONCE_LEN);

  uint8_t* ct = outputFrame_ + 2 + GCM_NONCE_LEN;
  uint8_t* tagDst = ct + PT_LEN;

  auto gcmResult = apex::encryption::lite::aes256GcmEncrypt(
      activeKey_, nonce_, frame, static_cast<uint32_t>(PT_LEN), nullptr, 0, ct, tagDst);

  if (gcmResult.status != apex::encryption::lite::GcmStatus::OK) {
    ++stats_.framesErr;
    return;
  }

  const size_t FRAME_LEN = 1 + 1 + GCM_NONCE_LEN + PT_LEN + GCM_TAG_LEN;

  // SLIP-encode directly from outputFrame_ (no separate prefixed buffer)
  const apex::compat::bytes_span FRAME_SPAN(outputFrame_, FRAME_LEN);
  auto slipResult = apex::protocols::slip::encode(FRAME_SPAN, slipEncodeBuf_, MAX_SLIP_ENCODED);

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
