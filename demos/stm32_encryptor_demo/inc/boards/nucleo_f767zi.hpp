#ifndef APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_F767ZI_HPP
#define APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_F767ZI_HPP
/**
 * @file nucleo_f767zi.hpp
 * @brief NUCLEO-F767ZI board description (STM32F767ZI, Cortex-M7 @ 216 MHz).
 *
 * One physical channel: both the data and the command traffic ride the
 * ST-Link virtual COM port, USART3 on PD8 (TX) / PD9 (RX), AF7, with a
 * one-byte channel prefix inside every SLIP frame (the single-UART shape
 * the Arduino and C2000 encryptors use). Heartbeat LED LD1 on PB0. Key
 * store in flash sector 11, the last sector in single-bank mode (256 KB);
 * in dual-bank mode the same index is bank 1's last 128 KB sector, so the
 * choice holds either way. Erasing a 256 KB sector takes on the order of a
 * second, so a populated-slot rewrite or KEY_STORE_ERASE blocks far longer
 * than on the L476's 2 KB page.
 */

#include "Stm32Uart.hpp"
#include "stm32f7xx_hal.h"

#include <stddef.h>
#include <stdint.h>

namespace encryptor {
namespace board {

// Internal linkage throughout: each translation unit that includes the
// board sees its own constants, exactly as when they lived in main.cpp,
// so the default board's image is unchanged by the board split.

/* ----------------------------- LED ----------------------------- */

static GPIO_TypeDef* const LED_PORT = GPIOB;

/// Enable the LED port clock (the port is fixed per board, so no runtime lookup).
static inline void enableLedClock() noexcept { __HAL_RCC_GPIOB_CLK_ENABLE(); }
static constexpr uint16_t LED_PIN = GPIO_PIN_0; // LD1 (green)

/* ----------------------------- Shared Channel (ST-Link VCP) ----------------------------- */

static USART_TypeDef* const CMD_UART = USART3;
static const apex::hal::stm32::Stm32UartPins CMD_UART_PINS = {GPIOD, GPIO_PIN_8, // TX
                                                              GPIOD, GPIO_PIN_9, // RX
                                                              GPIO_AF7_USART3};
static constexpr IRQn_Type CMD_UART_IRQN = USART3_IRQn;

/* ----------------------------- Channel Layout ----------------------------- */

/// One physical channel: every SLIP frame starts with CHANNEL_DATA or CHANNEL_CMD.
static constexpr size_t CHANNEL_PREFIX_SIZE = 1;

/* ----------------------------- Key Store ----------------------------- */

/// Flash sector holding the key slots: sector 11 (last sector, single-bank).
static constexpr uint32_t KEY_STORE_PAGE = 11;

/* ----------------------------- Clock and Caches ----------------------------- */

/**
 * @brief Configure SYSCLK to 216 MHz from the 8 MHz HSE bypass (ST-Link MCO).
 *
 * PLL: 8 MHz / 8 * 432 / 2 = 216 MHz; PLLQ 9 gives 48 MHz for USB/SDMMC.
 * Over-drive is required above 180 MHz; flash latency 7 wait states at
 * 216 MHz on the 3.3 V supply. APB1 at 54 MHz and APB2 at 108 MHz keep
 * both buses inside their limits.
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
  oscInit.PLL.PLLN = 432;
  oscInit.PLL.PLLP = RCC_PLLP_DIV2;
  oscInit.PLL.PLLQ = 9;

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
  clkInit.APB1CLKDivider = RCC_HCLK_DIV4; // 54 MHz
  clkInit.APB2CLKDivider = RCC_HCLK_DIV2; // 108 MHz

  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_7) != HAL_OK) {
    for (;;) {
    }
  }
}

/**
 * @brief Enable the instruction cache; the data cache stays off.
 *
 * With the data cache off, flash reads after a key-store program or erase
 * see the array directly and no cache maintenance is needed around the
 * flash driver. The demo has no DMA and its RAM traffic is small, so the
 * data cache buys nothing here.
 */
static inline void configureCaches() noexcept { SCB_EnableICache(); }

} // namespace board
} // namespace encryptor

/* ----------------------------- Interrupt Vector Names ----------------------------- */

/// 1: data and command frames share one UART and are routed by prefix.
#define APEX_BOARD_SHARED_CHANNEL 1
#define APEX_BOARD_CMD_UART_IRQ_HANDLER USART3_IRQHandler

#endif // APEX_STM32_ENCRYPTOR_BOARD_NUCLEO_F767ZI_HPP
