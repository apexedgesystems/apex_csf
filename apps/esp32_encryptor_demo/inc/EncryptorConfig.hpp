#ifndef APEX_ESP32_ENCRYPTOR_CONFIG_HPP
#define APEX_ESP32_ENCRYPTOR_CONFIG_HPP
/**
 * @file EncryptorConfig.hpp
 * @brief ESP32-specific encryptor configuration.
 *
 * Imports shared types from EncryptorCommon.hpp and instantiates
 * EncryptorSizing with ESP32 parameters (same as STM32/Pico: larger
 * buffers, more key slots, dual channel, no channel prefix).
 */

#include "EncryptorCommon.hpp"

namespace encryptor {

/* ----------------------------- Sizing ----------------------------- */

/// ESP32 sizing: 256B plaintext, 16 key slots, no channel prefix.
using Sizing = EncryptorSizing<256, 16, 0>;

/* ----------------------------- Sizing Aliases ----------------------------- */

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

#endif // APEX_ESP32_ENCRYPTOR_CONFIG_HPP
