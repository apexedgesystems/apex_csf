#ifndef APEX_PICO_ENCRYPTOR_ENGINE_HPP
#define APEX_PICO_ENCRYPTOR_ENGINE_HPP
/**
 * @file EncryptorEngine.hpp
 * @brief SLIP + CRC-16 + AES-256-GCM encrypt pipeline for the data channel.
 *
 * Owns the full encrypt pipeline: UART RX -> SLIP decode -> CRC validate
 * -> AES-256-GCM encrypt -> SLIP encode -> UART TX.
 *
 * All buffers are statically allocated. No heap usage.
 *
 * @note RT-safe after construction (poll() is O(n) in packet size).
 */

#include "EncryptorConfig.hpp"
#include "KeyStore.hpp"
#include "src/system/core/hal/base/IUart.hpp"
#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"

namespace encryptor {

/* ----------------------------- EncryptorEngine ----------------------------- */

/**
 * @class EncryptorEngine
 * @brief Data channel encrypt pipeline.
 *
 * Lifecycle:
 *  1. Construct with reference to data UART and optional KeyStore
 *  2. Call loadActiveKey() to load key from store, or setKey() for manual
 *  3. Call poll() at 100 Hz from dataChannelTask
 *
 * @note RT-safe: poll() has no allocations, bounded execution time.
 */
class EncryptorEngine {
public:
  explicit EncryptorEngine(apex::hal::IUart& dataUart, KeyStore* keyStore = nullptr) noexcept;

  void poll() noexcept;

  bool loadActiveKey() noexcept;

  void setKey(const uint8_t* key, uint8_t index) noexcept;

  bool hasKey() const noexcept { return hasKey_; }

  /* ----------------------------- Key Mode ----------------------------- */

  bool lockToSlot(uint8_t slot) noexcept;
  void unlock() noexcept;
  KeyMode keyMode() const noexcept { return keyMode_; }
  uint8_t activeKeyIndex() const noexcept { return activeKeyIndex_; }
  void clearActiveKey() noexcept { hasKey_ = false; }

  /* ----------------------------- IV / Nonce ----------------------------- */

  void resetNonce() noexcept;
  const uint8_t* nonce() const noexcept { return nonce_; }
  uint32_t frameCount() const noexcept { return frameCount_; }

  /* ----------------------------- Statistics ----------------------------- */

  const EncryptorStats& stats() const noexcept { return stats_; }
  void resetStats() noexcept { stats_.reset(); }

private:
  apex::hal::IUart& uart_;
  KeyStore* keyStore_;

  apex::protocols::slip::DecodeState slipState_;
  apex::protocols::slip::DecodeConfig slipCfg_;

  uint8_t decodeBuf_[MAX_INPUT_FRAME];
  uint8_t outputFrame_[MAX_OUTPUT_FRAME];
  uint8_t slipEncodeBuf_[MAX_SLIP_ENCODED];

  uint8_t activeKey_[AES_KEY_LEN];
  uint8_t activeKeyIndex_;
  bool hasKey_;
  uint8_t nonce_[GCM_NONCE_LEN];
  uint32_t frameCount_;

  KeyMode keyMode_;
  uint8_t rotationIndex_;

  EncryptorStats stats_;

  void processFrame(const uint8_t* frame, size_t len) noexcept;
};

} // namespace encryptor

#endif // APEX_PICO_ENCRYPTOR_ENGINE_HPP
