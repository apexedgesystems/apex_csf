#ifndef APEX_STM32_ENCRYPTOR_ENGINE_HPP
#define APEX_STM32_ENCRYPTOR_ENGINE_HPP
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
  /**
   * @brief Construct engine bound to a data UART and optional key store.
   * @param dataUart UART for data channel I/O (USART1).
   * @param keyStore Key store for flash-backed keys (nullptr = manual setKey).
   */
  explicit EncryptorEngine(apex::hal::IUart& dataUart, KeyStore* keyStore = nullptr) noexcept;

  /**
   * @brief Poll the data channel: read UART, decode SLIP, encrypt, transmit.
   *
   * Called from dataChannelTask at 100 Hz. Processes all available bytes
   * in the UART RX buffer and handles zero or more complete frames.
   *
   * @note RT-safe after construction.
   */
  void poll() noexcept;

  /**
   * @brief Load active key from the key store.
   *
   * Calls keyStore->selectKey() and caches the result locally.
   * Requires a non-null KeyStore set at construction.
   *
   * @return true if a key was loaded, false if store is empty or null.
   * @note NOT RT-safe (may read from flash on first call).
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
   *
   * Reads the key from the store into the active cache and switches
   * to LOCKED mode. Returns false if the slot is empty or invalid.
   *
   * @param slot Key slot index (0-15).
   * @return true if locked successfully, false if slot is empty.
   * @note NOT RT-safe (reads from KeyStore RAM cache).
   */
  bool lockToSlot(uint8_t slot) noexcept;

  /**
   * @brief Switch to RANDOM mode (rotate through populated slots).
   * @note RT-safe.
   */
  void unlock() noexcept;

  /**
   * @brief Get the current key selection mode.
   * @return LOCKED or RANDOM.
   * @note RT-safe.
   */
  KeyMode keyMode() const noexcept { return keyMode_; }

  /**
   * @brief Get the active key slot index.
   * @return Slot index (0-15) of the currently active key.
   * @note RT-safe.
   */
  uint8_t activeKeyIndex() const noexcept { return activeKeyIndex_; }

  /**
   * @brief Clear the active key, disabling encryption.
   *
   * After calling this, hasKey() returns false and incoming frames
   * are rejected until a key is loaded again.
   *
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
   * @return Pointer to nonce array.
   * @note RT-safe.
   */
  const uint8_t* nonce() const noexcept { return nonce_; }

  /**
   * @brief Get the frame counter (incremented per successful encrypt).
   *
   * Resets to 0 when resetNonce() is called. Independent of stats.
   *
   * @return Number of frames encrypted since last nonce reset.
   * @note RT-safe.
   */
  uint32_t frameCount() const noexcept { return frameCount_; }

  /* ----------------------------- Statistics ----------------------------- */

  /**
   * @brief Get encryption statistics.
   * @return Reference to stats structure.
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

  // SLIP decoder state
  apex::protocols::slip::DecodeState slipState_;
  apex::protocols::slip::DecodeConfig slipCfg_;

  // Work buffers (all static, no heap)
  uint8_t decodeBuf_[MAX_INPUT_FRAME];      ///< SLIP decode output.
  uint8_t outputFrame_[MAX_OUTPUT_FRAME];   ///< Assembled output (hdr + ct + tag).
  uint8_t slipEncodeBuf_[MAX_SLIP_ENCODED]; ///< SLIP-encoded output for TX.

  // Encryption state
  uint8_t activeKey_[AES_KEY_LEN];
  uint8_t activeKeyIndex_;
  bool hasKey_;                  ///< true if a key has been loaded.
  uint8_t nonce_[GCM_NONCE_LEN]; ///< Incrementing counter nonce.
  uint32_t frameCount_;          ///< Frames since last nonce reset.

  // Key mode state
  KeyMode keyMode_;       ///< LOCKED or RANDOM.
  uint8_t rotationIndex_; ///< Counter for RANDOM mode rotation.

  EncryptorStats stats_;

  /**
   * @brief Process a complete SLIP-decoded frame.
   * @param frame Decoded frame data.
   * @param len Frame length in bytes.
   */
  void processFrame(const uint8_t* frame, size_t len) noexcept;
};

} // namespace encryptor

#endif // APEX_STM32_ENCRYPTOR_ENGINE_HPP
