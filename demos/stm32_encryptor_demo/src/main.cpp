/**
 * @file main.cpp
 * @brief STM32 encryptor firmware for NUCLEO-L476RG.
 *
 * Data channel + command channel + flash key store.
 *   - UART1 (FTDI): SLIP-framed plaintext in, encrypted ciphertext out
 *   - UART2 (VCP):  SLIP-framed command/response for key management
 *   - LED heartbeat at 2 Hz
 *   - Keys persisted on flash page 510 (survive power cycles)
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
 *   - dataChannelTask:  100 Hz  (freqN=1, freqD=1)
 *   - commandTask:       20 Hz  (freqN=1, freqD=5)
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
#include "stm32l4xx_hal.h"

#if APEX_USE_FREERTOS
#include "FreeRtosTickSource.hpp"
#include "FreeRTOS.h"
#include "task.h"
#else
#include "Stm32SysTickSource.hpp"
#endif

/* ----------------------------- Hardware Definitions ----------------------------- */

static constexpr uint16_t LED_PIN = GPIO_PIN_5;
static GPIO_TypeDef* const LED_PORT = GPIOA;

/// USART1 pins: PA9 (TX), PA10 (RX), AF7 -- connected to FTDI FT232RL.
static const apex::hal::stm32::Stm32UartPins USART1_PINS = {GPIOA, GPIO_PIN_9,  // TX
                                                            GPIOA, GPIO_PIN_10, // RX
                                                            GPIO_AF7_USART1};

/// USART2 pins: PA2 (TX), PA3 (RX), AF7 -- connected to ST-Link VCP.
static const apex::hal::stm32::Stm32UartPins USART2_PINS = {GPIOA, GPIO_PIN_2, // TX
                                                            GPIOA, GPIO_PIN_3, // RX
                                                            GPIO_AF7_USART2};

/// Development test key (sequential bytes, easy to reproduce in Python).
/// Provisioned to flash on first boot if key store is empty.
static constexpr uint8_t TEST_KEY[encryptor::AES_KEY_LEN] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

/* ----------------------------- Peripheral Instances ----------------------------- */

static apex::hal::stm32::Stm32Uart<512, 512> dataUart(USART1, USART1_PINS);
static apex::hal::stm32::Stm32Uart<128, 128> cmdUart(USART2, USART2_PINS);
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
static void ledBlinkTask(void* /*ctx*/) noexcept { HAL_GPIO_TogglePin(LED_PORT, LED_PIN); }

/**
 * @brief Data channel task at 100 Hz.
 *
 * Polls UART1 for incoming SLIP frames, validates CRC-16,
 * encrypts with AES-256-GCM, and transmits the result.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded execution time per poll.
 */
static void dataChannelTask(void* /*ctx*/) noexcept { engine.poll(); }

/**
 * @brief Command channel task at 20 Hz.
 *
 * Polls UART2 (VCP) for incoming command frames, validates CRC-16,
 * dispatches commands, and transmits responses.
 *
 * @param ctx Unused.
 * @note RT-safe: bounded execution time per poll (except flash ops).
 */
static void commandTask(void* /*ctx*/) noexcept { commandDeck.poll(); }

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
  exec.addTask({profilerStartTask, &tracker, 1, 1, 0, 127, 10});
  // LED blink: freqN=1, freqD=50 -> period=50 ticks -> 2 Hz at 100 Hz
  exec.addTask({ledBlinkTask, nullptr, 1, 50, 0, 0, 1});
  // Data channel: freqN=1, freqD=1 -> every tick -> 100 Hz
  exec.addTask({dataChannelTask, nullptr, 1, 1, 0, 0, 2});
  // Command channel: freqN=1, freqD=5 -> period=5 ticks -> 20 Hz
  exec.addTask({commandTask, nullptr, 1, 5, 0, 0, 3});
  // Profiler end: every tick, lowest priority (runs last)
  exec.addTask({profilerEndTask, &tracker, 1, 1, 0, -128, 11});
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
 * @brief Configure system clock to 80 MHz using MSI + PLL.
 */
static void SystemClock_Config() {
  RCC_OscInitTypeDef oscInit = {};
  RCC_ClkInitTypeDef clkInit = {};

  oscInit.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  oscInit.MSIState = RCC_MSI_ON;
  oscInit.MSICalibrationValue = RCC_MSICALIBRATION_DEFAULT;
  oscInit.MSIClockRange = RCC_MSIRANGE_6; // 4 MHz
  oscInit.PLL.PLLState = RCC_PLL_ON;
  oscInit.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  oscInit.PLL.PLLM = 1;
  oscInit.PLL.PLLN = 40;
  oscInit.PLL.PLLR = 2;
  oscInit.PLL.PLLP = 7;
  oscInit.PLL.PLLQ = 4;

  if (HAL_RCC_OscConfig(&oscInit) != HAL_OK) {
    while (1) {
    }
  }

  clkInit.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clkInit.APB1CLKDivider = RCC_HCLK_DIV1;
  clkInit.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_4) != HAL_OK) {
    while (1) {
    }
  }
}

/**
 * @brief Initialize GPIO for LED (PA5).
 */
static void GPIO_Init() {
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gpioInit = {};
  gpioInit.Pin = LED_PIN;
  gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
  gpioInit.Pull = GPIO_NOPULL;
  gpioInit.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(LED_PORT, &gpioInit);
}

/* ----------------------------- Main Application ----------------------------- */

int main() {
  HAL_Init();
  SystemClock_Config();
  GPIO_Init();

  // Enable DWT cycle counter for overhead measurement
  tracker.enableDwt();

  // Startup blinks (visual confirmation of init)
  for (int i = 0; i < 6; i++) {
    HAL_GPIO_TogglePin(LED_PORT, LED_PIN);
    HAL_Delay(150);
  }

  // Initialize UARTs (115200 8N1)
  apex::hal::UartConfig uartCfg;
  uartCfg.baudRate = 115200;
  static_cast<void>(dataUart.init(uartCfg)); // USART1 data channel
  static_cast<void>(cmdUart.init(uartCfg));  // USART2 command channel

#if APEX_USE_FREERTOS
  // Set UART interrupt priorities for FreeRTOS compatibility.
  // Must be >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY (5) if ISRs
  // ever call FreeRTOS API. Priority 6 is safe and responsive.
  HAL_NVIC_SetPriority(USART1_IRQn, 6, 0);
  HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
#endif

  // Initialize flash-backed key store (page 510)
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

extern "C" void USART1_IRQHandler() { dataUart.irqHandler(); }

extern "C" void USART2_IRQHandler() { cmdUart.irqHandler(); }

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
