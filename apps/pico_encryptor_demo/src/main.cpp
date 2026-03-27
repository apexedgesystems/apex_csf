/**
 * @file main.cpp
 * @brief Raspberry Pi Pico encryptor firmware.
 *
 * Data channel + command channel + flash key store on RP2040.
 *   - UART0 (GP0/GP1): SLIP-framed plaintext in, encrypted ciphertext out
 *   - USB CDC (native):  SLIP-framed command/response for key management
 *   - LED heartbeat at 2 Hz (GP25, onboard)
 *   - Keys persisted on flash sector 511 (survive power cycles)
 *
 * Task model (100 Hz fundamental):
 *   - ledBlinkTask:     2 Hz  (freqN=1, freqD=50)
 *   - dataChannelTask: 100 Hz (freqN=1, freqD=1)
 *   - commandTask:      20 Hz (freqN=1, freqD=5)
 *
 * No profiler tasks: Cortex-M0+ lacks a DWT cycle counter.
 * OverheadTracker is a no-op stub that returns zeros.
 */

#include "CommandDeck.hpp"
#include "EncryptorConfig.hpp"
#include "EncryptorEngine.hpp"
#include "KeyStore.hpp"
#include "LiteExecutive.hpp"
#include "OverheadTracker.hpp"
#include "PicoFlash.hpp"
#include "PicoSysTickSource.hpp"
#include "PicoUart.hpp"
#include "PicoUsbCdc.hpp"

#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"

/* ----------------------------- Hardware Definitions ----------------------------- */

static constexpr uint LED_PIN = 25;

/// Development test key (sequential bytes, easy to reproduce in Python).
/// Provisioned to flash on first boot if key store is empty.
static constexpr uint8_t TEST_KEY[encryptor::AES_KEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

/* ----------------------------- Peripheral Instances ----------------------------- */

/// UART0: Data channel (GP0 TX, GP1 RX) -> FTDI adapter.
static apex::hal::pico::PicoUart<512, 512> dataUart(uart0, 0, 1);

/// USB CDC: Command channel via native USB (same role as STM32 VCP).
static apex::hal::pico::PicoUsbCdc<128, 128> cmdUart;

static apex::hal::pico::PicoFlash flash;
static encryptor::KeyStore keyStore(flash);
static encryptor::EncryptorEngine engine(dataUart, &keyStore);

/* ----------------------------- Executive Stack ----------------------------- */

static apex::hal::pico::PicoSysTickSource tickSource(100); // 100 Hz
static executive::lite::LiteExecutive<> exec(&tickSource, 100);

/* ----------------------------- Overhead Tracker ----------------------------- */

static encryptor::OverheadTracker tracker(exec);
static encryptor::CommandDeck commandDeck(cmdUart, keyStore, engine, tracker);

/* ----------------------------- Task Functions ----------------------------- */

/**
 * @brief LED blink task at 2 Hz (heartbeat).
 * @param ctx Unused.
 * @note RT-safe: single GPIO toggle.
 */
static void ledBlinkTask(void* /*ctx*/) noexcept {
  static bool ledState = false;
  ledState = !ledState;
  gpio_put(LED_PIN, ledState);
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
 * @note RT-safe: bounded execution time per poll (except flash ops).
 */
static void commandTask(void* /*ctx*/) noexcept { commandDeck.poll(); }

/* ----------------------------- IRQ Handlers ----------------------------- */

/// UART0 interrupt handler (data channel).
static void uart0IrqHandler() { dataUart.irqHandler(); }

/// SysTick interrupt handler (executive tick source).
extern "C" void isr_systick() { apex::hal::pico::PicoSysTickSource::isrCallback(); }

/* ----------------------------- Main Application ----------------------------- */

int main() {
  // Pico SDK system initialization (clocks, USB CDC stack).
  // stdio_usb handles TinyUSB init and background tud_task() timer.
  // We use tud_cdc_*() directly for binary I/O (no printf/getchar).
  stdio_init_all();

  // Configure LED GPIO
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);

  // Startup blinks (visual confirmation of init)
  for (int i = 0; i < 6; i++) {
    gpio_put(LED_PIN, i & 1);
    sleep_ms(150);
  }
  gpio_put(LED_PIN, false);

  // Register UART0 IRQ handler before init (init enables the IRQ)
  irq_set_exclusive_handler(UART0_IRQ, uart0IrqHandler);

  // Initialize UART0 (115200 8N1) for data channel
  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = 115200;
  static_cast<void>(dataUart.init(uartCfg));

  // Initialize USB CDC for command channel (baud rate ignored)
  static_cast<void>(cmdUart.init(uartCfg));

  // Initialize flash-backed key store (sector 511)
  static_cast<void>(flash.init());
  static_cast<void>(keyStore.init());

  // Provision test key on first boot if store is empty
  if (keyStore.populatedCount() == 0) {
    static_cast<void>(keyStore.writeKey(0, TEST_KEY));
  }

  // Load active key from store
  engine.loadActiveKey();

  // Enable DWT (no-op on M0+, included for API consistency)
  tracker.enableDwt();

  // Register scheduler tasks
  // LED blink: freqN=1, freqD=50 -> period=50 ticks -> 2 Hz at 100 Hz
  exec.addTask({ledBlinkTask, nullptr, 1, 50, 0, 0, 1});
  // Data channel: freqN=1, freqD=1 -> every tick -> 100 Hz
  exec.addTask({dataChannelTask, nullptr, 1, 1, 0, 0, 2});
  // Command channel: freqN=1, freqD=5 -> period=5 ticks -> 20 Hz
  exec.addTask({commandTask, nullptr, 1, 5, 0, 0, 3});

  // Initialize and run executive (blocks forever via SysTick WFI)
  static_cast<void>(exec.init());
  static_cast<void>(exec.run());

  // Should never reach here
  while (1) {
  }
  return 0;
}
