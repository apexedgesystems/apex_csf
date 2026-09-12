#ifndef APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_L476RG_HPP
#define APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_L476RG_HPP
/**
 * @file nucleo_l476rg.hpp
 * @brief NUCLEO-L476RG board description (STM32L476RG, Cortex-M4F @ 80 MHz).
 *
 * Data channel: USART1 on PA9 (TX) / PA10 (RX), AF7, wired to the FTDI
 * adapter. Command channel: USART2 on PA2 (TX) / PA3 (RX), AF7, the
 * ST-Link virtual COM port. Heartbeat LED LD2 on PA5. Key store on flash
 * page 510 (2 KB, bank 2, outside the application image).
 */

#include "Stm32Uart.hpp"
#include "stm32l4xx_hal.h"

#include <stddef.h>
#include <stdint.h>

namespace encryptor {
namespace board {

/* ----------------------------- Identity ----------------------------- */
// Internal linkage throughout: each translation unit that includes the
// board sees its own constants, exactly as when they lived in main.cpp,
// so the default board's image is unchanged by the board split.

static constexpr const char* NAME = "NUCLEO-L476RG";

/* ----------------------------- LED ----------------------------- */

static GPIO_TypeDef* const LED_PORT = GPIOA;

/// Enable the LED port clock (the port is fixed per board, so no runtime lookup).
static inline void enableLedClock() noexcept { __HAL_RCC_GPIOA_CLK_ENABLE(); }
static constexpr uint16_t LED_PIN = GPIO_PIN_5;

/* ----------------------------- Data Channel (FTDI) ----------------------------- */

static USART_TypeDef* const DATA_UART = USART1;
static const apex::hal::stm32::Stm32UartPins DATA_UART_PINS = {GPIOA, GPIO_PIN_9,  // TX
                                                               GPIOA, GPIO_PIN_10, // RX
                                                               GPIO_AF7_USART1};
static constexpr IRQn_Type DATA_UART_IRQN = USART1_IRQn;

/* ----------------------------- Command Channel (ST-Link VCP) ----------------------------- */

static USART_TypeDef* const CMD_UART = USART2;
static const apex::hal::stm32::Stm32UartPins CMD_UART_PINS = {GPIOA, GPIO_PIN_2, // TX
                                                              GPIOA, GPIO_PIN_3, // RX
                                                              GPIO_AF7_USART2};
static constexpr IRQn_Type CMD_UART_IRQN = USART2_IRQn;

/* ----------------------------- Channel Layout ----------------------------- */

/// Two physical channels: no channel prefix inside the SLIP frames.
static constexpr size_t CHANNEL_PREFIX_SIZE = 0;

/* ----------------------------- Key Store ----------------------------- */

/// Flash page holding the key slots: page 510, bank 2 (2 KB), never
/// reached by the application image.
static constexpr uint32_t KEY_STORE_PAGE = 510;

/* ----------------------------- Clock and Caches ----------------------------- */

/**
 * @brief Configure SYSCLK to 80 MHz from MSI (4 MHz) through the PLL.
 * @note NOT RT-safe: boot only. Halts on a HAL failure (LED never blinks).
 */
static inline void configureSystemClock() noexcept {
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
    for (;;) {
    }
  }

  clkInit.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clkInit.APB1CLKDivider = RCC_HCLK_DIV1;
  clkInit.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_4) != HAL_OK) {
    for (;;) {
    }
  }
}

/**
 * @brief Core caches: nothing to enable on the M4 (the ART accelerator is
 *        configured by the HAL conf).
 */
static inline void configureCaches() noexcept {}

} // namespace board
} // namespace encryptor

/* ----------------------------- Interrupt Vector Names ----------------------------- */

/// 0: data and command frames arrive on separate UARTs.
#define APEX_BOARD_SHARED_CHANNEL 0
#define APEX_BOARD_DATA_UART_IRQ_HANDLER USART1_IRQHandler
#define APEX_BOARD_CMD_UART_IRQ_HANDLER USART2_IRQHandler

#endif // APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_L476RG_HPP
