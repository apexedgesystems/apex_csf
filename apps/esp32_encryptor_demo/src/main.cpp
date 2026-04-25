/**
 * @file main.cpp
 * @brief ESP32-S3 encryptor firmware for Arduino Nano ESP32.
 *
 * Data channel + command channel + NVS key store on ESP32-S3.
 *   - UART0 (GP43/GP44): SLIP-framed plaintext in, encrypted ciphertext out
 *   - USB Serial/JTAG:   SLIP-framed command/response for key management
 *   - WS2812 RGB LED heartbeat at 2 Hz (GPIO48)
 *   - Keys persisted in NVS (survive power cycles)
 *
 * FreeRTOS execution model:
 *   McuExecutive runs inside a pinned FreeRTOS task on core 0 (unicore
 *   mode via CONFIG_FREERTOS_UNICORE=y). Esp32TimerTickSource uses
 *   esp_timer for 100 Hz ticks with FreeRTOS task notification for
 *   efficient blocking.
 *
 * Task model (100 Hz fundamental):
 *   - profilerStartTask: 100 Hz (priority 127, CCOUNT start marker)
 *   - ledBlinkTask:       2 Hz  (freqN=1, freqD=50)
 *   - dataChannelTask:  100 Hz  (freqN=1, freqD=1)
 *   - commandTask:       20 Hz  (freqN=1, freqD=5)
 *   - profilerEndTask:  100 Hz  (priority -128, CCOUNT end marker)
 *
 * Unlike the Pico (Cortex-M0+ no DWT), the Xtensa LX7 CCOUNT register
 * provides real cycle-accurate overhead measurement at 240 MHz.
 */

#include "CommandDeck.hpp"
#include "EncryptorConfig.hpp"
#include "EncryptorEngine.hpp"
#include "Esp32NvsFlash.hpp"
#include "Esp32TimerTickSource.hpp"
#include "Esp32Uart.hpp"
#include "Esp32UsbCdc.hpp"
#include "KeyStore.hpp"
#include "McuExecutive.hpp"
#include "OverheadTracker.hpp"

#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ----------------------------- Hardware Definitions ----------------------------- */

/// WS2812 RGB LED on GPIO48 (Arduino Nano ESP32 onboard LED).
static constexpr gpio_num_t LED_PIN = GPIO_NUM_48;

/// UART0 pins: GPIO43 (TX), GPIO44 (RX) -- connected to FTDI FT232RL.
static constexpr gpio_num_t UART0_TX_PIN = GPIO_NUM_43;
static constexpr gpio_num_t UART0_RX_PIN = GPIO_NUM_44;

/// Development test key (sequential bytes, easy to reproduce in Python).
/// Provisioned to NVS on first boot if key store is empty.
static constexpr uint8_t TEST_KEY[encryptor::AES_KEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

/* ----------------------------- WS2812 LED Driver ----------------------------- */

/// RMT channel handle for WS2812 protocol.
static rmt_channel_handle_t ledChannel = nullptr;

/// RMT bytes encoder handle for WS2812 bit timing.
static rmt_encoder_handle_t ledEncoder = nullptr;

/**
 * @brief Initialize WS2812 LED via RMT peripheral.
 *
 * Configures an RMT TX channel on LED_PIN with WS2812 timing:
 *   T0H = 0.3us (3 ticks at 10 MHz), T0L = 0.9us (9 ticks)
 *   T1H = 0.9us (9 ticks), T1L = 0.3us (3 ticks)
 *
 * @note NOT RT-safe: Call once during initialization.
 */
static void ws2812Init() {
  rmt_tx_channel_config_t chanCfg = {};
  chanCfg.gpio_num = LED_PIN;
  chanCfg.clk_src = RMT_CLK_SRC_DEFAULT;
  chanCfg.resolution_hz = 10000000; // 10 MHz -> 100 ns per tick
  chanCfg.mem_block_symbols = 64;
  chanCfg.trans_queue_depth = 4;

  rmt_new_tx_channel(&chanCfg, &ledChannel);

  rmt_bytes_encoder_config_t bytesCfg = {};
  bytesCfg.bit0.level0 = 1;
  bytesCfg.bit0.duration0 = 3; // T0H = 300 ns
  bytesCfg.bit0.level1 = 0;
  bytesCfg.bit0.duration1 = 9; // T0L = 900 ns
  bytesCfg.bit1.level0 = 1;
  bytesCfg.bit1.duration0 = 9; // T1H = 900 ns
  bytesCfg.bit1.level1 = 0;
  bytesCfg.bit1.duration1 = 3; // T1L = 300 ns
  bytesCfg.flags.msb_first = 1;

  rmt_new_bytes_encoder(&bytesCfg, &ledEncoder);
  rmt_enable(ledChannel);
}

/**
 * @brief Set WS2812 LED color.
 * @param r Red brightness (0-255).
 * @param g Green brightness (0-255).
 * @param b Blue brightness (0-255).
 * @note RT-safe: Bounded-time RMT transmission (24 bits at 800 kHz = 30 us).
 */
static void ws2812Set(uint8_t r, uint8_t g, uint8_t b) {
  // WS2812 expects GRB byte order
  uint8_t data[3] = {g, r, b};
  rmt_transmit_config_t txCfg = {};
  txCfg.loop_count = 0;
  rmt_transmit(ledChannel, ledEncoder, data, sizeof(data), &txCfg);
  rmt_tx_wait_all_done(ledChannel, 100);
}

/* ----------------------------- Peripheral Instances ----------------------------- */

/// UART0: Data channel (GPIO43 TX, GPIO44 RX) -> FTDI adapter.
static apex::hal::esp32::Esp32Uart<512, 512> dataUart(UART_NUM_0, UART0_TX_PIN, UART0_RX_PIN);

/// USB Serial/JTAG: Command channel via native USB-C.
static apex::hal::esp32::Esp32UsbCdc<256, 256> cmdUart;

static apex::hal::esp32::Esp32NvsFlash flash;
static encryptor::KeyStore keyStore(flash);
static encryptor::EncryptorEngine engine(dataUart, &keyStore);

/* ----------------------------- Executive Stack ----------------------------- */

static apex::hal::esp32::Esp32TimerTickSource tickSource(100); // 100 Hz
static executive::mcu::McuExecutive<> exec(&tickSource, 100);

/* ----------------------------- Overhead Tracker ----------------------------- */

static encryptor::OverheadTracker tracker(exec);
static encryptor::CommandDeck commandDeck(cmdUart, keyStore, engine, tracker);

/* ----------------------------- FreeRTOS Configuration ----------------------------- */

/// Executive task stack size in words (4 KB, plenty for 512 KB SRAM).
static constexpr uint32_t EXEC_TASK_STACK_SIZE = 4096;

/// Executive task FreeRTOS priority (above idle, below critical).
static constexpr UBaseType_t EXEC_TASK_PRIORITY = 3;

/* ----------------------------- Task Functions ----------------------------- */

/**
 * @brief LED blink task at 2 Hz (heartbeat).
 *
 * Toggles WS2812 LED between green (on) and off. Low brightness
 * (0, 32, 0) to avoid blinding in close proximity.
 *
 * @param ctx Unused.
 * @note RT-safe: Single RMT transmission (~30 us).
 */
static void ledBlinkTask(void* /*ctx*/) noexcept {
  static bool ledState = false;
  ledState = !ledState;
  ws2812Set(0, ledState ? 32 : 0, 0);
}

/**
 * @brief Data channel task at 100 Hz.
 *
 * Polls UART0 for incoming SLIP frames, validates CRC-16,
 * encrypts with AES-256-GCM, and transmits the result.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded execution time per poll.
 */
static void dataChannelTask(void* /*ctx*/) noexcept { engine.poll(); }

/**
 * @brief Command channel task at 20 Hz.
 *
 * Polls USB CDC for incoming command frames, validates CRC-16,
 * dispatches commands, and transmits responses.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded execution time per poll (except NVS ops).
 */
static void commandTask(void* /*ctx*/) noexcept { commandDeck.poll(); }

/**
 * @brief Profiler start task (highest priority, runs first).
 *
 * Samples CCOUNT at the beginning of each scheduler tick.
 *
 * @param ctx Pointer to OverheadTracker.
 * @note RT-safe: single register read.
 */
static void profilerStartTask(void* ctx) noexcept {
  static_cast<encryptor::OverheadTracker*>(ctx)->markTickStart();
}

/**
 * @brief Profiler end task (lowest priority, runs last).
 *
 * Samples CCOUNT at the end of each scheduler tick and updates stats.
 *
 * @param ctx Pointer to OverheadTracker.
 * @note RT-safe: single register read + stats update.
 */
static void profilerEndTask(void* ctx) noexcept {
  static_cast<encryptor::OverheadTracker*>(ctx)->markTickEnd();
}

/* ----------------------------- Executive Task ----------------------------- */

/**
 * @brief FreeRTOS task that runs the McuExecutive.
 *
 * Configures scheduler tasks, initializes executive, and enters the
 * executive main loop. Uses Esp32TimerTickSource for timing
 * (esp_timer + FreeRTOS task notification).
 *
 * @param param Unused.
 */
static void executiveTask(void* /*param*/) {
  // Register scheduler tasks
  // Profiler start: every tick, highest priority (runs first)
  exec.addTask({profilerStartTask, &tracker, 1, 1, 0, 127, 10});
  // LED blink: freqN=1, freqD=50 -> period=50 ticks -> 2 Hz at 100 Hz
  exec.addTask({ledBlinkTask, nullptr, 1, 50, 0, 0, 1});
  // Data channel: freqN=1, freqD=1 -> every tick -> 100 Hz
  exec.addTask({dataChannelTask, nullptr, 1, 1, 0, 0, 2});
  // Command channel: freqN=1, freqD=5 -> period=5 ticks -> 20 Hz
  exec.addTask({commandTask, nullptr, 1, 5, 0, 0, 3});
  // Profiler end: every tick, lowest priority (runs last)
  exec.addTask({profilerEndTask, &tracker, 1, 1, 0, -128, 11});

  // Initialize and run executive (blocks forever via esp_timer tick)
  static_cast<void>(exec.init());
  static_cast<void>(exec.run());

  // Should never reach here
  for (;;) {
  }
}

/* ----------------------------- Main Application ----------------------------- */

/**
 * @brief ESP-IDF application entry point.
 *
 * Called by FreeRTOS startup on core 0. Initializes peripherals,
 * provisions test key, creates the executive FreeRTOS task.
 */
extern "C" void app_main() {
  // Initialize WS2812 RGB LED on GPIO48
  ws2812Init();

  // Startup blinks (visual confirmation of init)
  for (int i = 0; i < 6; i++) {
    ws2812Set(0, (i & 1) ? 32 : 0, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
  }
  ws2812Set(0, 0, 0);

  // Initialize UART0 (115200 8N1) for data channel
  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = 115200;
  static_cast<void>(dataUart.init(uartCfg));

  // Initialize USB Serial/JTAG for command channel (baud rate ignored)
  static_cast<void>(cmdUart.init(uartCfg));

  // Initialize NVS-backed key store
  static_cast<void>(keyStore.init());

  // Provision test key on first boot if store is empty
  if (keyStore.populatedCount() == 0) {
    static_cast<void>(keyStore.writeKey(0, TEST_KEY));
  }

  // Load active key from store
  engine.loadActiveKey();

  // Enable overhead tracker (no-op on Xtensa, CCOUNT always runs)
  tracker.enableDwt();

  // Create executive FreeRTOS task (pinned to core 0 in unicore mode)
  xTaskCreate(executiveTask, "Executive", EXEC_TASK_STACK_SIZE, nullptr, EXEC_TASK_PRIORITY,
              nullptr);

  // app_main() returns to the FreeRTOS idle task.
  // The executive task runs independently.
}
