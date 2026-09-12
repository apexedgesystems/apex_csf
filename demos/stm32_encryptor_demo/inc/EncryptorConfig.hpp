#ifndef APEX_STM32_ENCRYPTOR_CONFIG_HPP
#define APEX_STM32_ENCRYPTOR_CONFIG_HPP
/**
 * @file EncryptorConfig.hpp
 * @brief STM32-specific encryptor configuration.
 *
 * Imports shared types from EncryptorCommon.hpp and instantiates
 * EncryptorSizing with STM32 parameters (larger buffers, more key slots).
 * The channel prefix size comes from the board: boards with two UARTs
 * carry no prefix; a board that multiplexes both channels on one UART
 * prefixes every SLIP frame with CHANNEL_DATA or CHANNEL_CMD.
 */

#include "EncryptorCommon.hpp"
#include "boards/Board.hpp"

namespace encryptor {

/* ----------------------------- Sizing ----------------------------- */

/// STM32 sizing: 256B plaintext, 16 key slots, board-defined channel prefix.
using Sizing = EncryptorSizing<256, 16, board::CHANNEL_PREFIX_SIZE>;

/* ----------------------------- Channel Prefix ----------------------------- */

/// Bytes of channel prefix in every SLIP frame (0 or 1).
static constexpr size_t CHANNEL_PREFIX = Sizing::CHANNEL_PREFIX;

/// Prefix byte for data channel frames (plaintext in, ciphertext out).
static constexpr uint8_t CHANNEL_DATA = 0x00;

/// Prefix byte for command channel frames (request in, response out).
static constexpr uint8_t CHANNEL_CMD = 0x01;

/* ----------------------------- Sizing Aliases ----------------------------- */
// Aliases for backward compatibility with existing code.

static constexpr size_t MAX_PLAINTEXT_SIZE = Sizing::MAX_PLAINTEXT_SIZE;
static constexpr size_t MAX_INPUT_FRAME = Sizing::MAX_INPUT_FRAME;
static constexpr size_t MAX_OUTPUT_FRAME = Sizing::MAX_OUTPUT_FRAME;
static constexpr size_t MAX_SLIP_ENCODED = Sizing::MAX_SLIP_ENCODED;
static constexpr size_t MIN_INPUT_FRAME = Sizing::MIN_DATA_PAYLOAD;
static constexpr uint8_t KEY_SLOT_COUNT = Sizing::KEY_SLOT_COUNT;
static constexpr size_t MAX_CMD_FRAME = Sizing::MAX_CMD_FRAME;
static constexpr size_t MIN_CMD_FRAME = Sizing::MIN_CMD_FRAME;
static constexpr size_t MAX_RSP_FRAME = Sizing::MAX_RSP_FRAME;
static constexpr size_t MAX_RSP_SLIP = Sizing::MAX_RSP_SLIP;

} // namespace encryptor

#endif // APEX_STM32_ENCRYPTOR_CONFIG_HPP
