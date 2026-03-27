#ifndef APEX_ARDUINO_ENCRYPTOR_CONFIG_HPP
#define APEX_ARDUINO_ENCRYPTOR_CONFIG_HPP
/**
 * @file EncryptorConfig.hpp
 * @brief Arduino-specific encryptor configuration.
 *
 * Imports shared types from EncryptorCommon.hpp and instantiates
 * EncryptorSizing with ATmega328P SRAM-constrained parameters.
 *
 * Single UART multiplexing: the first byte of each SLIP-decoded frame
 * is a channel prefix (CHANNEL_DATA or CHANNEL_CMD). The remaining
 * bytes are the channel payload (same format as stm32_encryptor).
 */

#include "EncryptorCommon.hpp"

namespace encryptor {

/* ----------------------------- Sizing ----------------------------- */

/// Arduino sizing: 48B plaintext, 4 key slots, 1-byte channel prefix.
using Sizing = EncryptorSizing<48, 4, 1>;

/* ----------------------------- Channel Prefix ----------------------------- */

/// Channel prefix for data frames (encrypt pipeline).
static constexpr uint8_t CHANNEL_DATA = 0x00;

/// Channel prefix for command frames (key management, diagnostics).
static constexpr uint8_t CHANNEL_CMD = 0x01;

/* ----------------------------- Sizing Aliases ----------------------------- */
// Aliases for backward compatibility with existing code.

static constexpr size_t MAX_PLAINTEXT_SIZE = Sizing::MAX_PLAINTEXT_SIZE;
static constexpr size_t MAX_INPUT_FRAME = Sizing::MAX_INPUT_FRAME;
static constexpr size_t MAX_OUTPUT_FRAME = Sizing::MAX_OUTPUT_FRAME;
static constexpr size_t MAX_SLIP_ENCODED = Sizing::MAX_SLIP_ENCODED;
static constexpr size_t MIN_DATA_PAYLOAD = Sizing::MIN_DATA_PAYLOAD;
static constexpr uint8_t KEY_SLOT_COUNT = Sizing::KEY_SLOT_COUNT;
static constexpr size_t MAX_CMD_PAYLOAD = Sizing::MAX_CMD_FRAME;
static constexpr size_t MIN_CMD_PAYLOAD = Sizing::MIN_CMD_FRAME;
static constexpr size_t MAX_RSP_FRAME = Sizing::MAX_RSP_FRAME;
static constexpr size_t MAX_RSP_SLIP = Sizing::MAX_RSP_SLIP;

} // namespace encryptor

#endif // APEX_ARDUINO_ENCRYPTOR_CONFIG_HPP
