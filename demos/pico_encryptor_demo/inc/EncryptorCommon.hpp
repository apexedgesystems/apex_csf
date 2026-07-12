#ifndef APEX_ENCRYPTOR_COMMON_HPP
#define APEX_ENCRYPTOR_COMMON_HPP
/**
 * @file EncryptorCommon.hpp
 * @brief Shared types, sizing traits, and utilities for the pico_encryptor_demo.
 *
 * Each encryptor app has its own copy of this header (self-contained apps).
 *
 * Contains:
 *  - EncryptorSizing<> template for platform-dependent buffer dimensions
 *  - Command protocol enums (CmdOpcode, CmdStatus, KeyMode)
 *  - Shared encryption constants (AES_KEY_LEN, GCM_NONCE_LEN, GCM_TAG_LEN)
 *  - Utility functions (CRC validation, nonce increment)
 *
 * @note RT-safe: all functions are inline, no heap, no exceptions.
 */

#include "src/utilities/checksums/crc/inc/Crc.hpp"

#include <stddef.h>
#include <stdint.h>

namespace encryptor {

/* ----------------------------- Encryption Constants ----------------------------- */

/// AES-256 key length in bytes.
static constexpr size_t AES_KEY_LEN = 32;

/// GCM nonce/IV length in bytes (96-bit per NIST SP 800-38D).
static constexpr size_t GCM_NONCE_LEN = 12;

/// GCM authentication tag length in bytes (128-bit full tag).
static constexpr size_t GCM_TAG_LEN = 16;

/* ----------------------------- EncryptorSizing ----------------------------- */

/**
 * @struct EncryptorSizing
 * @brief Compile-time buffer dimension calculator.
 *
 * Platform-specific apps instantiate with their constraints:
 *   Arduino: EncryptorSizing<48, 4, 1>   (channel prefix multiplexing)
 *   STM32:   EncryptorSizing<256, 16, 0> (separate UARTs, no prefix)
 *   Pico:    EncryptorSizing<256, 16, 0> (separate UARTs, no prefix)
 *
 * All derived sizes cascade from the three template parameters.
 *
 * @tparam MaxPlaintext Maximum plaintext bytes per packet.
 * @tparam KeySlots Number of key slots in the backing store.
 * @tparam ChannelPrefixSize Bytes prepended for channel multiplexing (0 or 1).
 */
template <size_t MaxPlaintext, uint8_t KeySlots, size_t ChannelPrefixSize = 0>
struct EncryptorSizing {
  static constexpr size_t MAX_PLAINTEXT_SIZE = MaxPlaintext;
  static constexpr uint8_t KEY_SLOT_COUNT = KeySlots;
  static constexpr size_t CHANNEL_PREFIX = ChannelPrefixSize;

  /// Maximum SLIP-decoded input: prefix + plaintext + CRC-16.
  static constexpr size_t MAX_INPUT_FRAME = ChannelPrefixSize + MaxPlaintext + 2;

  /// Maximum assembled output: key_index(1) + nonce(12) + ciphertext + tag(16).
  static constexpr size_t MAX_OUTPUT_FRAME = 1 + GCM_NONCE_LEN + MaxPlaintext + GCM_TAG_LEN;

  /// Worst-case SLIP-encoded output: prefix + frame * 2 + 2 delimiters.
  static constexpr size_t MAX_SLIP_ENCODED = (ChannelPrefixSize + MAX_OUTPUT_FRAME) * 2 + 2;

  /// Minimum valid data payload: 1 byte plaintext + 2 bytes CRC.
  static constexpr size_t MIN_DATA_PAYLOAD = 3;

  /// Maximum command frame size.
  static constexpr size_t MAX_CMD_FRAME = 64;

  /// Minimum command frame: opcode(1) + CRC(2).
  static constexpr size_t MIN_CMD_FRAME = 3;

  /// Maximum response frame: prefix + opcode(1) + status(1) + payload(0-32) + CRC(2).
  static constexpr size_t MAX_RSP_FRAME = ChannelPrefixSize + 1 + 1 + 32 + 2;

  /// Worst-case SLIP-encoded response.
  static constexpr size_t MAX_RSP_SLIP = MAX_RSP_FRAME * 2 + 2;
};

/* ----------------------------- Command Protocol ----------------------------- */

/**
 * @brief Command opcodes for the command channel.
 */
enum class CmdOpcode : uint8_t {
  KEY_STORE_WRITE = 0x01,
  KEY_STORE_READ = 0x02,
  KEY_STORE_ERASE = 0x03,
  KEY_STORE_STATUS = 0x04,
  KEY_LOCK = 0x10,
  KEY_UNLOCK = 0x11,
  KEY_MODE_STATUS = 0x12,
  IV_RESET = 0x20,
  IV_STATUS = 0x21,
  STATS = 0x30,
  STATS_RESET = 0x31,
  OVERHEAD = 0x40,
  OVERHEAD_RESET = 0x41,
  FASTFORWARD = 0x42
};

/**
 * @brief Command response status codes.
 */
enum class CmdStatus : uint8_t {
  OK = 0x00,
  ERR_INVALID_CMD = 0x01,
  ERR_BAD_PAYLOAD = 0x02,
  ERR_KEY_SLOT = 0x03,
  ERR_FLASH = 0x04,
  ERR_LOCKED = 0x05
};

/**
 * @brief Key selection mode for the encrypt pipeline.
 */
enum class KeyMode : uint8_t {
  RANDOM = 0x00, ///< Rotate through populated slots per packet.
  LOCKED = 0x01  ///< Use a single locked slot for all packets.
};

/* ----------------------------- EncryptorStats ----------------------------- */

/**
 * @struct EncryptorStats
 * @brief Encryption pipeline statistics.
 * @note RT-safe.
 */
struct EncryptorStats {
  uint32_t framesOk = 0;  ///< Packets successfully encrypted.
  uint32_t framesErr = 0; ///< Packets rejected (CRC, size, encrypt failure).
  uint32_t bytesIn = 0;   ///< Total plaintext bytes encrypted.
  uint32_t bytesOut = 0;  ///< Total bytes transmitted (SLIP-encoded output).

  void reset() noexcept {
    framesOk = 0;
    framesErr = 0;
    bytesIn = 0;
    bytesOut = 0;
  }
};

/* ----------------------------- Utility Functions ----------------------------- */

/**
 * @brief Validate CRC-16/XMODEM over data against an expected value.
 * @param data Input bytes.
 * @param len Number of bytes to CRC.
 * @param expected Expected CRC value.
 * @return true if computed CRC matches expected.
 * @note RT-safe.
 */
inline bool validateCrc16Xmodem(const uint8_t* data, size_t len, uint16_t expected) noexcept {
  apex::checksums::crc::Crc16XmodemBitwise crc;
  uint16_t computed = 0;
  crc.calculate(data, len, computed);
  return computed == expected;
}

/**
 * @brief Increment a nonce as a big-endian counter.
 * @param nonce Nonce buffer to increment in place.
 * @param len Nonce length in bytes (typically GCM_NONCE_LEN = 12).
 * @note RT-safe.
 */
inline void incrementNonce(uint8_t* nonce, size_t len) noexcept {
  for (int i = static_cast<int>(len) - 1; i >= 0; --i) {
    if (++nonce[i] != 0) {
      break;
    }
  }
}

} // namespace encryptor

#endif // APEX_ENCRYPTOR_COMMON_HPP
