/**
 * @file main.cpp
 * @brief Arduino encryptor firmware for Uno R3 (ATmega328P).
 *
 * Port of stm32_encryptor to 8-bit AVR. Single UART multiplexed via
 * channel prefix byte inside SLIP frames:
 *   0x00 = data channel (encrypt pipeline)
 *   0x01 = command channel (key management, diagnostics)
 *
 * The SLIP decoder runs in main.cpp's dataChannelTask. Complete frames
 * are dispatched to EncryptorEngine or CommandDeck based on the first
 * byte (channel prefix), which is stripped before passing to processFrame().
 *
 * Task model (100 Hz fundamental):
 *   - profilerStartTask: 100 Hz (priority 127, Timer0 cycle start)
 *   - ledBlinkTask:       2 Hz  (freqN=1, freqD=50)
 *   - channelTask:      100 Hz  (freqN=1, freqD=1) -- single UART mux
 *   - profilerEndTask:  100 Hz  (priority -128, Timer0 cycle end)
 */

#include "CommandDeck.hpp"
#include "EncryptorConfig.hpp"
#include "EncryptorEngine.hpp"
#include "KeyStore.hpp"
#include "McuExecutive.hpp"
#include "OverheadTracker.hpp"

#include "AvrEeprom.hpp"
#include "AvrTimerTickSource.hpp"
#include "AvrUart.hpp"

#include "src/system/core/infrastructure/protocols/framing/slip/inc/SLIPFraming.hpp"

#include <avr/interrupt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

/* ----------------------------- Hardware Definitions ----------------------------- */

/// LED on PB5 (Arduino pin 13).
static constexpr uint8_t LED_PIN = PB5;

/// Development test key (sequential bytes, same as STM32 version).
/// Stored in PROGMEM (flash) -- only used once during first-boot provisioning.
static const uint8_t TEST_KEY[encryptor::AES_KEY_LEN] PROGMEM = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

/* ----------------------------- Peripheral Instances ----------------------------- */

/// Single UART for both data and command channels (64B RX, 32B TX).
static apex::hal::avr::AvrUart<64, 32> uart;

/// EEPROM as flash interface for key storage.
static apex::hal::avr::AvrEeprom eeprom;

/// Key store backed by EEPROM.
static encryptor::KeyStore keyStore(eeprom);

/// Encryptor engine (data channel).
static encryptor::EncryptorEngine engine(uart, &keyStore);

/* ----------------------------- Static Member Definitions ----------------------------- */

/// AvrTimerTickSource ISR dispatch (non-template, needs .cpp definition).
apex::hal::avr::AvrTimerTickSource* apex::hal::avr::AvrTimerTickSource::instance_ = nullptr;

/// OverheadTracker ISR dispatch (non-template, needs .cpp definition).
encryptor::OverheadTracker* encryptor::OverheadTracker::instance_ = nullptr;

/* ----------------------------- Executive Stack ----------------------------- */

/// Timer1 tick source at 100 Hz.
static apex::hal::avr::AvrTimerTickSource tickSource(100);

/// McuExecutive at 100 Hz.
static executive::mcu::McuExecutive<8, uint32_t> exec(&tickSource, 100);

/* ----------------------------- Overhead Tracker ----------------------------- */

static encryptor::OverheadTracker tracker(exec);
static encryptor::CommandDeck commandDeck(uart, keyStore, engine, tracker);

/* ----------------------------- SLIP Decoder State ----------------------------- */

/// Shared SLIP decoder for incoming frames (both channels).
static apex::protocols::slip::DecodeState slipState;
static apex::protocols::slip::DecodeConfig slipCfg;

/// SLIP decode output buffer (max of data or command frame).
static uint8_t decodeBuf[encryptor::MAX_INPUT_FRAME];

/* ----------------------------- Task Functions ----------------------------- */

/**
 * @brief LED blink task at 2 Hz (heartbeat).
 * @note RT-safe: single register toggle.
 */
static void ledBlinkTask(void* /*ctx*/) noexcept {
  PINB |= (1 << LED_PIN); // Toggle PB5 (write to PINx toggles output)
}

/**
 * @brief Unified channel task at 100 Hz.
 *
 * Reads UART, feeds SLIP decoder, dispatches complete frames based on
 * the channel prefix byte:
 *   0x00 -> EncryptorEngine::processFrame() (data minus channel byte)
 *   0x01 -> CommandDeck::processFrame() (command minus channel byte)
 *
 * @note RT-safe: bounded by UART buffer and frame sizes.
 */
static void channelTask(void* /*ctx*/) noexcept {
  static constexpr size_t RX_CHUNK_SIZE = 32;
  uint8_t rxBuf[RX_CHUNK_SIZE];
  const size_t AVAIL = uart.read(rxBuf, sizeof(rxBuf));
  if (AVAIL == 0) {
    return;
  }

  size_t pos = 0;
  while (pos < AVAIL) {
    const size_t PREV_LEN = slipState.frameLen;
    const apex::compat::bytes_span INPUT(rxBuf + pos, AVAIL - pos);

    auto result = apex::protocols::slip::decodeChunk(
        slipState, slipCfg, INPUT, decodeBuf + PREV_LEN, encryptor::MAX_INPUT_FRAME - PREV_LEN);

    pos += result.bytesConsumed;

    if (result.frameCompleted) {
      const size_t FRAME_LEN = PREV_LEN + result.bytesProduced;

      // Need at least channel byte + 1 payload byte
      if (FRAME_LEN >= 2) {
        const uint8_t CHANNEL = decodeBuf[0];
        const uint8_t* PAYLOAD = decodeBuf + 1;
        const size_t PAYLOAD_LEN = FRAME_LEN - 1;

        if (CHANNEL == encryptor::CHANNEL_DATA) {
          engine.processFrame(PAYLOAD, PAYLOAD_LEN);
        } else if (CHANNEL == encryptor::CHANNEL_CMD) {
          commandDeck.processFrame(PAYLOAD, PAYLOAD_LEN);
        }
        // Unknown channel bytes are silently dropped
      }
    }

    if (result.bytesConsumed == 0) {
      break;
    }
  }
}

/**
 * @brief Profiler start task (highest priority, runs first).
 * @note RT-safe: single register read.
 */
static void profilerStartTask(void* ctx) noexcept {
  static_cast<encryptor::OverheadTracker*>(ctx)->markTickStart();
}

/**
 * @brief Profiler end task (lowest priority, runs last).
 * @note RT-safe: register read + stats update.
 */
static void profilerEndTask(void* ctx) noexcept {
  static_cast<encryptor::OverheadTracker*>(ctx)->markTickEnd();
}

/* ----------------------------- Scheduler Task Registration ----------------------------- */

static void registerSchedulerTasks() {
  // Profiler start: every tick, highest priority (runs first)
  exec.addTask({profilerStartTask, &tracker, 1, 1, 0, 127, 10});
  // LED blink: freqN=1, freqD=50 -> period=50 ticks -> 2 Hz at 100 Hz
  exec.addTask({ledBlinkTask, nullptr, 1, 50, 0, 0, 1});
  // Channel task: freqN=1, freqD=1 -> every tick -> 100 Hz
  exec.addTask({channelTask, nullptr, 1, 1, 0, 0, 2});
  // Profiler end: every tick, lowest priority (runs last)
  exec.addTask({profilerEndTask, &tracker, 1, 1, 0, -128, 11});
}

/* ----------------------------- Main Application ----------------------------- */

int main() {
  // Configure LED pin as output
  DDRB |= (1 << LED_PIN);

  // Startup blinks (visual confirmation of init)
  for (uint8_t i = 0; i < 6; ++i) {
    PINB |= (1 << LED_PIN);
    _delay_ms(150);
  }

  // Initialize UART (115200 8N1)
  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = 115200;
  static_cast<void>(uart.init(uartCfg));

  // Initialize overhead tracker Timer0
  tracker.enableTimer();
  encryptor::OverheadTracker::instance_ = &tracker;

  // Initialize EEPROM-backed key store
  static_cast<void>(keyStore.init());

  // Provision test key on first boot if store is empty
  if (keyStore.populatedCount() == 0) {
    uint8_t keyBuf[encryptor::AES_KEY_LEN];
    memcpy_P(keyBuf, TEST_KEY, encryptor::AES_KEY_LEN);
    static_cast<void>(keyStore.writeKey(0, keyBuf));
  }

  // Load active key from store
  engine.loadActiveKey();

  // Initialize SLIP decoder
  slipCfg.maxFrameSize = encryptor::MAX_INPUT_FRAME;
  slipCfg.allowEmptyFrame = false;
  slipCfg.dropUntilEnd = true;
  slipCfg.requireTrailingEnd = true;

  // Register tasks and run executive (blocks forever)
  registerSchedulerTasks();
  static_cast<void>(exec.init());
  static_cast<void>(exec.run());

  // Should never reach here
  while (1) {
  }
  return 0;
}

/* ----------------------------- Interrupt Handlers ----------------------------- */

/// USART RX complete -- dispatch to AvrUart instance.
ISR(USART_RX_vect) {
  if (apex::hal::avr::AvrUart<64, 32>::instance_ != nullptr) {
    apex::hal::avr::AvrUart<64, 32>::instance_->rxIsr();
  }
}

/// USART data register empty -- dispatch to AvrUart instance.
ISR(USART_UDRE_vect) {
  if (apex::hal::avr::AvrUart<64, 32>::instance_ != nullptr) {
    apex::hal::avr::AvrUart<64, 32>::instance_->udreIsr();
  }
}

/// Timer1 compare match A -- dispatch to AvrTimerTickSource.
ISR(TIMER1_COMPA_vect) {
  if (apex::hal::avr::AvrTimerTickSource::instance_ != nullptr) {
    apex::hal::avr::AvrTimerTickSource::instance_->timerIsr();
  }
}

/// Timer0 overflow -- dispatch to OverheadTracker for extended range.
ISR(TIMER0_OVF_vect) {
  if (encryptor::OverheadTracker::instance_ != nullptr) {
    encryptor::OverheadTracker::instance_->overflowIsr();
  }
}
