#ifndef APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_F446RE_HPP
#define APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_F446RE_HPP
/**
 * @file nucleo_f446re.hpp
 * @brief NUCLEO-F446RE board description (STM32F446RE, Cortex-M4F @ 180 MHz).
 *
 * One physical channel: both the data and the command traffic ride the
 * ST-Link virtual COM port, USART2 on PA2 (TX) / PA3 (RX), AF7, with a
 * one-byte channel prefix inside every SLIP frame. Heartbeat LED LD2 on
 * PA5. Key store in flash sector 7, the last of the part's eight sectors
 * (128 KB at 0x08060000); erasing it takes on the order of a second.
 */

#include "Stm32Uart.hpp"
#include "stm32f4xx_hal.h"

#include <stddef.h>
#include <stdint.h>

namespace encryptor {
namespace board {

// Internal linkage throughout: each translation unit that includes the
// board sees its own constants, exactly as when they lived in main.cpp,
// so the default board's image is unchanged by the board split.

/* ----------------------------- LED ----------------------------- */

static GPIO_TypeDef* const LED_PORT = GPIOA;
static constexpr uint16_t LED_PIN = GPIO_PIN_5; // LD2 (green)

/// Enable the LED port clock (the port is fixed per board, so no runtime lookup).
static inline void enableLedClock() noexcept { __HAL_RCC_GPIOA_CLK_ENABLE(); }

/* ----------------------------- Shared Channel (ST-Link VCP) ----------------------------- */

static USART_TypeDef* const CMD_UART = USART2;
static const apex::hal::stm32::Stm32UartPins CMD_UART_PINS = {GPIOA, GPIO_PIN_2, // TX
                                                              GPIOA, GPIO_PIN_3, // RX
                                                              GPIO_AF7_USART2};
static constexpr IRQn_Type CMD_UART_IRQN = USART2_IRQn;

/* ----------------------------- Channel Layout ----------------------------- */

/// One physical channel: every SLIP frame starts with CHANNEL_DATA or CHANNEL_CMD.
static constexpr size_t CHANNEL_PREFIX_SIZE = 1;

/* ----------------------------- Key Store ----------------------------- */

/// Flash sector holding the key slots: sector 7 (last sector, 128 KB).
static constexpr uint32_t KEY_STORE_PAGE = 7;

/* ----------------------------- Clock and Caches ----------------------------- */

/**
 * @brief Configure SYSCLK to 180 MHz from the 8 MHz HSE bypass (ST-Link MCO).
 *
 * PLL: 8 MHz / 8 * 360 / 2 = 180 MHz. Over-drive is required above
 * 168 MHz; flash latency 5 wait states at 180 MHz on the 3.3 V supply.
 * APB1 at 45 MHz and APB2 at 90 MHz keep both buses inside their limits.
 *
 * @note NOT RT-safe: boot only. Halts on a HAL failure (LED never blinks).
 */
static inline void configureSystemClock() noexcept {
  RCC_OscInitTypeDef oscInit = {};
  RCC_ClkInitTypeDef clkInit = {};

  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  oscInit.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  oscInit.HSEState = RCC_HSE_BYPASS;
  oscInit.PLL.PLLState = RCC_PLL_ON;
  oscInit.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  oscInit.PLL.PLLM = 8;
  oscInit.PLL.PLLN = 360;
  oscInit.PLL.PLLP = RCC_PLLP_DIV2;
  oscInit.PLL.PLLQ = 7;
  oscInit.PLL.PLLR = 2;

  if (HAL_RCC_OscConfig(&oscInit) != HAL_OK) {
    for (;;) {
    }
  }

  if (HAL_PWREx_EnableOverDrive() != HAL_OK) {
    for (;;) {
    }
  }

  clkInit.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clkInit.APB1CLKDivider = RCC_HCLK_DIV4; // 45 MHz
  clkInit.APB2CLKDivider = RCC_HCLK_DIV2; // 90 MHz

  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_5) != HAL_OK) {
    for (;;) {
    }
  }
}

/**
 * @brief Core caches: nothing to enable on the M4 (the ART accelerator's
 *        prefetch and instruction/data caches are configured by the HAL conf).
 */
static inline void configureCaches() noexcept {}

} // namespace board
} // namespace encryptor

/* ----------------------------- Interrupt Vector Names ----------------------------- */

/// 1: data and command frames share one UART and are routed by prefix.
#define APEX_BOARD_SHARED_CHANNEL 1
#define APEX_BOARD_CMD_UART_IRQ_HANDLER USART2_IRQHandler

#endif // APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_F446RE_HPP
