/**
 * @file main.cpp
 * @brief STM32 encryptor firmware for the NUCLEO boards in boards/.
 *
 * Data channel + command channel + flash key store.
 *   - Data channel: SLIP-framed plaintext in, encrypted ciphertext out
 *   - Command channel: SLIP-framed command/response for key management
 *   - LED heartbeat at 2 Hz
 *   - Keys persisted on the board's key-store flash page (survive power cycles)
 *
 * Every pin, peripheral instance, clock setting, and the key-store page
 * comes from the board description selected by boards/Board.hpp. Boards
 * with two UARTs run the data and command channels on separate pollers;
 * a board that shares one UART (APEX_BOARD_SHARED_CHANNEL) runs a single
 * channel task that routes each SLIP frame by its channel prefix byte.
 *
 * Supports two execution modes (selected at compile time):
 *
 * Bare-metal (default):
 *   McuExecutive runs directly in main() using Stm32SysTickSource.
 *
 * FreeRTOS (APEX_USE_FREERTOS):
 *   McuExecutive runs inside a FreeRTOS task using FreeRtosTickSource
 *   (vTaskDelayUntil). A single FreeRTOS task hosts the executive; all
 *   scheduler tasks remain within McuExecutive.
 *
 * Task model (100 Hz fundamental):
 *   - profilerStartTask: 100 Hz (priority 127, DWT cycle start marker)
 *   - ledBlinkTask:       2 Hz  (freqN=1, freqD=50)
 *   - dataChannelTask:  100 Hz  (freqN=1, freqD=1)   [two-UART boards]
 *   - commandTask:       20 Hz  (freqN=1, freqD=5)   [two-UART boards]
 *   - channelTask:      100 Hz  (freqN=1, freqD=1)   [shared-channel boards]
 *   - profilerEndTask:  100 Hz  (priority -128, DWT cycle end marker)
 */

#include "CommandDeck.hpp"
#include "EncryptorConfig.hpp"
#include "EncryptorEngine.hpp"
#include "KeyStore.hpp"
#include "McuExecutive.hpp"
#include "OverheadTracker.hpp"
#include "Stm32Flash.hpp"
#include "Stm32Uart.hpp"
#include "boards/Board.hpp"

#if APEX_USE_FREERTOS
#include "FreeRtosTickSource.hpp"
#include "FreeRTOS.h"
#include "task.h"
#else
#include "Stm32SysTickSource.hpp"
#endif

/* ----------------------------- Hardware Definitions ----------------------------- */

/// Development test key (sequential bytes, easy to reproduce in Python).
/// Provisioned to flash on first boot if key store is empty.
static constexpr uint8_t TEST_KEY[encryptor::AES_KEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

/* ----------------------------- Peripheral Instances ----------------------------- */

#if APEX_BOARD_SHARED_CHANNEL
/// One UART carries both channels; sized for the data channel's traffic.
static apex::hal::stm32::Stm32Uart<512, 512> cmdUart(encryptor::board::CMD_UART,
                                                     encryptor::board::CMD_UART_PINS);
static apex::hal::stm32::Stm32Uart<512, 512>& dataUart = cmdUart;
#else
static apex::hal::stm32::Stm32Uart<512, 512> dataUart(encryptor::board::DATA_UART,
                                                      encryptor::board::DATA_UART_PINS);
static apex::hal::stm32::Stm32Uart<128, 128> cmdUart(encryptor::board::CMD_UART,
                                                     encryptor::board::CMD_UART_PINS);
#endif
static apex::hal::stm32::Stm32Flash flash;
static encryptor::KeyStore keyStore(flash);
static encryptor::EncryptorEngine engine(dataUart, &keyStore);

/* ----------------------------- Executive Stack ----------------------------- */

#if APEX_USE_FREERTOS
static apex::hal::stm32::FreeRtosTickSource tickSource(100); // 100 Hz
#else
static apex::hal::stm32::Stm32SysTickSource tickSource(100); // 100 Hz
#endif
static executive::mcu::McuExecutive<> exec(&tickSource, 100);

/* ----------------------------- Overhead Tracker ----------------------------- */

static encryptor::OverheadTracker tracker(exec);
static encryptor::CommandDeck commandDeck(cmdUart, keyStore, engine, tracker);

#if APEX_USE_FREERTOS
/* ----------------------------- FreeRTOS Configuration ----------------------------- */

/// Executive task stack size in words (2 KB).
static constexpr uint32_t EXEC_TASK_STACK_SIZE = 512;

/// Executive task FreeRTOS priority (above idle, below critical).
static constexpr UBaseType_t EXEC_TASK_PRIORITY = 3;
#endif

/* ----------------------------- Task Functions ----------------------------- */

/**
 * @brief LED blink task at 2 Hz (heartbeat).
 * @param ctx Unused.
 * @note RT-safe: single GPIO toggle.
 */
static void ledBlinkTask(void* /*ctx*/) noexcept {
  HAL_GPIO_TogglePin(encryptor::board::LED_PORT, encryptor::board::LED_PIN);
}

#if APEX_BOARD_SHARED_CHANNEL
/* ----------------------------- Shared Channel ----------------------------- */

static apex::protocols::slip::DecodeState channelSlipState{};
static apex::protocols::slip::DecodeConfig channelSlipCfg{};
static uint8_t channelDecodeBuf[encryptor::MAX_INPUT_FRAME];

/**
 * @brief Unified channel task at 100 Hz.
 *
 * Reads the shared UART, feeds the SLIP decoder, and dispatches each
 * complete frame by its channel prefix byte:
 *   CHANNEL_DATA -> EncryptorEngine::processFrame() (frame minus prefix)
 *   CHANNEL_CMD  -> CommandDeck::processFrame()     (frame minus prefix)
 * Frames with an unknown prefix are dropped.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded by the UART buffer and frame sizes (except flash ops).
 */
static void channelTask(void* /*ctx*/) noexcept {
  static constexpr size_t RX_CHUNK_SIZE = 64;
  uint8_t rxBuf[RX_CHUNK_SIZE];
  const size_t AVAIL = cmdUart.read(rxBuf, sizeof(rxBuf));
  if (AVAIL == 0) {
    return;
  }

  size_t pos = 0;
  while (pos < AVAIL) {
    const size_t PREV_LEN = channelSlipState.frameLen;
    const apex::compat::bytes_span INPUT(rxBuf + pos, AVAIL - pos);

    auto result = apex::protocols::slip::decodeChunk(channelSlipState, channelSlipCfg, INPUT,
                                                     channelDecodeBuf + PREV_LEN,
                                                     encryptor::MAX_INPUT_FRAME - PREV_LEN);

    pos += result.bytesConsumed;

    if (result.frameCompleted) {
      const size_t FRAME_LEN = PREV_LEN + result.bytesProduced;

      // Channel byte plus at least one payload byte
      if (FRAME_LEN >= 2) {
        const uint8_t CHANNEL = channelDecodeBuf[0];
        const uint8_t* PAYLOAD = channelDecodeBuf + 1;
        const size_t PAYLOAD_LEN = FRAME_LEN - 1;

        if (CHANNEL == encryptor::CHANNEL_DATA) {
          engine.processFrame(PAYLOAD, PAYLOAD_LEN);
        } else if (CHANNEL == encryptor::CHANNEL_CMD) {
          commandDeck.processFrame(PAYLOAD, PAYLOAD_LEN);
        }
      }
    }

    if (result.bytesConsumed == 0) {
      break;
    }
  }
}
#else
/**
 * @brief Data channel task at 100 Hz.
 *
 * Polls the data UART for incoming SLIP frames, validates CRC-16,
 * encrypts with AES-256-GCM, and transmits the result.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded execution time per poll.
 */
static void dataChannelTask(void* /*ctx*/) noexcept { engine.poll(); }

/**
 * @brief Command channel task at 20 Hz.
 *
 * Polls the command UART for incoming command frames, validates CRC-16,
 * dispatches commands, and transmits responses.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded execution time per poll (except flash ops).
 */
static void commandTask(void* /*ctx*/) noexcept { commandDeck.poll(); }
#endif

/**
 * @brief Profiler start task (highest priority, runs first).
 *
 * Samples DWT->CYCCNT at the beginning of each scheduler tick.
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
 * Samples DWT->CYCCNT at the end of each scheduler tick and updates stats.
 *
 * @param ctx Pointer to OverheadTracker.
 * @note RT-safe: single register read + stats update.
 */
static void profilerEndTask(void* ctx) noexcept {
  static_cast<encryptor::OverheadTracker*>(ctx)->markTickEnd();
}

/* ----------------------------- Scheduler Task Registration ----------------------------- */

/**
 * @brief Register all scheduler tasks with the executive.
 *
 * Shared between bare-metal and FreeRTOS modes. Task configuration
 * is identical regardless of execution model.
 */
static void registerSchedulerTasks() {
  // Profiler start: every tick, highest priority (runs first)
  exec.addTask({{profilerStartTask, &tracker}, 1, 1, 0, 127, 10});
  // LED blink: freqN=1, freqD=50 -> period=50 ticks -> 2 Hz at 100 Hz
  exec.addTask({{ledBlinkTask, nullptr}, 1, 50, 0, 0, 1});
#if APEX_BOARD_SHARED_CHANNEL
  // Shared channel: freqN=1, freqD=1 -> every tick -> 100 Hz
  exec.addTask({{channelTask, nullptr}, 1, 1, 0, 0, 2});
#else
  // Data channel: freqN=1, freqD=1 -> every tick -> 100 Hz
  exec.addTask({{dataChannelTask, nullptr}, 1, 1, 0, 0, 2});
  // Command channel: freqN=1, freqD=5 -> period=5 ticks -> 20 Hz
  exec.addTask({{commandTask, nullptr}, 1, 5, 0, 0, 3});
#endif
  // Profiler end: every tick, lowest priority (runs last)
  exec.addTask({{profilerEndTask, &tracker}, 1, 1, 0, -128, 11});
}

#if APEX_USE_FREERTOS
/* ----------------------------- FreeRTOS Executive Task ----------------------------- */

/**
 * @brief FreeRTOS task that runs the McuExecutive.
 *
 * Configures scheduler tasks, initializes executive, and enters the
 * executive main loop. Uses FreeRtosTickSource for timing (vTaskDelayUntil).
 *
 * @param param Unused.
 */
static void executiveTask(void* /*param*/) {
  registerSchedulerTasks();

  // Initialize and run executive (blocks forever via vTaskDelayUntil)
  static_cast<void>(exec.init());
  static_cast<void>(exec.run());

  // Should never reach here
  for (;;) {
  }
}
#endif

/* ----------------------------- System Initialization ----------------------------- */

/**
 * @brief Enable the LED port clock and drive the heartbeat pin as push-pull output.
 */
static void GPIO_Init() {
  encryptor::board::enableLedClock();

  GPIO_InitTypeDef gpioInit = {};
  gpioInit.Pin = encryptor::board::LED_PIN;
  gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
  gpioInit.Pull = GPIO_NOPULL;
  gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(encryptor::board::LED_PORT, &gpioInit);
}

/* ----------------------------- Main Application ----------------------------- */

int main() {
  HAL_Init();
  encryptor::board::configureCaches();
  encryptor::board::configureSystemClock();
  GPIO_Init();

  // Enable DWT cycle counter for overhead measurement
  tracker.enableDwt();

  // Startup blinks (visual confirmation of init)
  for (int i = 0; i < 6; i++) {
    HAL_GPIO_TogglePin(encryptor::board::LED_PORT, encryptor::board::LED_PIN);
    HAL_Delay(150);
  }

  // Initialize UARTs (115200 8N1)
  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = 115200;
#if APEX_BOARD_SHARED_CHANNEL
  static_cast<void>(cmdUart.init(uartCfg)); // shared data + command channel
  channelSlipCfg.maxFrameSize = encryptor::MAX_INPUT_FRAME;
  channelSlipCfg.allowEmptyFrame = false;
  channelSlipCfg.dropUntilEnd = true;
  channelSlipCfg.requireTrailingEnd = true;
#else
  static_cast<void>(dataUart.init(uartCfg)); // data channel
  static_cast<void>(cmdUart.init(uartCfg));  // command channel
#endif

#if APEX_USE_FREERTOS
  // Set UART interrupt priorities for FreeRTOS compatibility.
  // Must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) if ISRs
  // ever call FreeRTOS API. Priority 6 is safe and responsive.
#if !APEX_BOARD_SHARED_CHANNEL
  HAL_NVIC_SetPriority(encryptor::board::DATA_UART_IRQN, 6, 0);
#endif
  HAL_NVIC_SetPriority(encryptor::board::CMD_UART_IRQN, 6, 0);
#endif

  // Initialize flash-backed key store (board key-store page)
  static_cast<void>(keyStore.init());

  // Provision test key on first boot if store is empty
  if (keyStore.populatedCount() == 0) {
    static_cast<void>(keyStore.writeKey(0, TEST_KEY));
  }

  // Load active key from store
  engine.loadActiveKey();

#if APEX_USE_FREERTOS
  // Create executive FreeRTOS task
  xTaskCreate(executiveTask, "Executive", EXEC_TASK_STACK_SIZE, nullptr, EXEC_TASK_PRIORITY,
              nullptr);

  // Start FreeRTOS scheduler (never returns)
  vTaskStartScheduler();
#else
  // Configure scheduler tasks and run executive directly (blocks forever)
  registerSchedulerTasks();
  static_cast<void>(exec.init());
  static_cast<void>(exec.run());
#endif

  // Should never reach here
  while (1) {
  }
  return 0;
}

/* ----------------------------- Interrupt Handlers ----------------------------- */

#if APEX_USE_FREERTOS
/// FreeRTOS port.c defines xPortSysTickHandler but has no header declaration.
extern "C" void xPortSysTickHandler();

/**
 * @brief SysTick handler shared between FreeRTOS and HAL.
 *
 * FreeRTOS owns SysTick at 1 kHz. We call both HAL_IncTick() for HAL
 * timebase and xPortSysTickHandler() for FreeRTOS kernel tick.
 */
extern "C" void SysTick_Handler() {
  HAL_IncTick();
  if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
    xPortSysTickHandler();
  }
}
#else
extern "C" void SysTick_Handler() {
  HAL_IncTick();
  apex::hal::stm32::Stm32SysTickSource::isrCallback();
}
#endif

#if !APEX_BOARD_SHARED_CHANNEL
extern "C" void APEX_BOARD_DATA_UART_IRQ_HANDLER() { dataUart.irqHandler(); }
#endif

extern "C" void APEX_BOARD_CMD_UART_IRQ_HANDLER() { cmdUart.irqHandler(); }

#if APEX_USE_FREERTOS
/* ----------------------------- FreeRTOS Hooks ----------------------------- */

/**
 * @brief Stack overflow hook -- halt on overflow detection.
 *
 * configCHECK_FOR_STACK_OVERFLOW = 2 enables pattern-fill checking.
 * If triggered, the executive task stack is too small.
 */
extern "C" void vApplicationStackOverflowHook(TaskHandle_t /*xTask*/, char* /*pcTaskName*/) {
  for (;;) {
  }
}

/**
 * @brief Malloc failed hook -- halt on allocation failure.
 *
 * Triggered when pvPortMalloc() fails (heap exhausted).
 * Increase configTOTAL_HEAP_SIZE if this fires.
 */
extern "C" void vApplicationMallocFailedHook() {
  for (;;) {
  }
}
#endif

/* ----------------------------- HAL MSP Callbacks ----------------------------- */

extern "C" void HAL_MspInit() {
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}
