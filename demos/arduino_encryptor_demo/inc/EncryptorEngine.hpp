#ifndef APEX_ARDUINO_ENCRYPTOR_ENGINE_HPP
#define APEX_ARDUINO_ENCRYPTOR_ENGINE_HPP
/**
 * @file EncryptorEngine.hpp
 * @brief AES-256-GCM encrypt pipeline for the data channel.
 *
 * Unlike the STM32 version, this engine does NOT own SLIP decoding.
 * The main loop handles SLIP decode and channel routing, then calls
 * processFrame() with the already-decoded data payload (no channel prefix).
 *
 * All buffers are statically allocated. No heap usage.
 *
 * @note RT-safe after construction (processFrame() is O(n) in packet size).
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
 *  1. Construct with reference to shared UART and optional KeyStore
 *  2. Call loadActiveKey() to load key from store, or setKey() for manual
 *  3. main.cpp calls processFrame() when a data channel frame arrives
 *
 * @note RT-safe: processFrame() has no allocations, bounded execution time.
 */
class EncryptorEngine {
public:
  /**
   * @brief Construct engine bound to a shared UART and optional key store.
   * @param uart UART for transmitting encrypted output.
   * @param keyStore Key store for EEPROM-backed keys (nullptr = manual setKey).
   */
  explicit EncryptorEngine(apex::hal::IUart& uart, KeyStore* keyStore = nullptr) noexcept;

  /**
   * @brief Process a complete data channel frame (without channel prefix).
   *
   * Called by main.cpp after SLIP decode and channel routing.
   * Validates CRC-16, encrypts with AES-256-GCM, SLIP-encodes with
   * channel prefix, and transmits.
   *
   * @param frame Decoded frame data (plaintext + CRC-16).
   * @param len Frame length in bytes.
   * @note RT-safe after construction.
   */
  void processFrame(const uint8_t* frame, size_t len) noexcept;

  /**
   * @brief Load active key from the key store.
   * @return true if a key was loaded, false if store is empty or null.
   * @note NOT RT-safe (may read from EEPROM on first call).
   */
  bool loadActiveKey() noexcept;

  /**
   * @brief Set the active encryption key manually.
   * @param key 32-byte AES-256 key (copied internally).
   * @param index Key slot index (included in output frame header).
   */
  void setKey(const uint8_t* key, uint8_t index) noexcept;

  /**
   * @brief Check if a key is loaded and ready for encryption.
   * @return true if a key is available.
   * @note RT-safe.
   */
  bool hasKey() const noexcept { return hasKey_; }

  /* ----------------------------- Key Mode ----------------------------- */

  /**
   * @brief Lock encryption to a specific key slot.
   * @param slot Key slot index (0-15).
   * @return true if locked successfully, false if slot is empty.
   */
  bool lockToSlot(uint8_t slot) noexcept;

  /**
   * @brief Switch to RANDOM mode (rotate through populated slots).
   * @note RT-safe.
   */
  void unlock() noexcept;

  /**
   * @brief Get the current key selection mode.
   * @note RT-safe.
   */
  KeyMode keyMode() const noexcept { return keyMode_; }

  /**
   * @brief Get the active key slot index.
   * @note RT-safe.
   */
  uint8_t activeKeyIndex() const noexcept { return activeKeyIndex_; }

  /**
   * @brief Clear the active key, disabling encryption.
   * @note RT-safe.
   */
  void clearActiveKey() noexcept { hasKey_ = false; }

  /* ----------------------------- IV / Nonce ----------------------------- */

  /**
   * @brief Reset the 12-byte nonce to zero and clear the frame counter.
   * @note RT-safe.
   */
  void resetNonce() noexcept;

  /**
   * @brief Get read-only pointer to the current 12-byte nonce.
   * @note RT-safe.
   */
  const uint8_t* nonce() const noexcept { return nonce_; }

  /**
   * @brief Get the frame counter (incremented per successful encrypt).
   * @note RT-safe.
   */
  uint32_t frameCount() const noexcept { return frameCount_; }

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get encryption statistics.
   * @note RT-safe.
   */
  const EncryptorStats& stats() const noexcept { return stats_; }

  /**
   * @brief Reset encryption statistics to zero.
   * @note RT-safe.
   */
  void resetStats() noexcept { stats_.reset(); }

private:
  apex::hal::IUart& uart_;
  KeyStore* keyStore_;

  // Work buffers (all static, no heap)
  uint8_t outputFrame_[1 + MAX_OUTPUT_FRAME]; ///< channel(1) + assembled output (hdr+ct+tag).
  uint8_t slipEncodeBuf_[MAX_SLIP_ENCODED];   ///< SLIP-encoded output for TX.

  // Encryption state
  uint8_t activeKey_[AES_KEY_LEN];
  uint8_t activeKeyIndex_;
  bool hasKey_;
  uint8_t nonce_[GCM_NONCE_LEN];
  uint32_t frameCount_;

  // Key mode state
  KeyMode keyMode_;
  uint8_t rotationIndex_;

  EncryptorStats stats_;
};

} // namespace encryptor

#endif // APEX_ARDUINO_ENCRYPTOR_ENGINE_HPP
