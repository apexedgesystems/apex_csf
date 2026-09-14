#ifndef APEX_HORIZON_DEMO_ROVER_MCU_BOARD_NUCLEO_F767ZI_HPP
#define APEX_HORIZON_DEMO_ROVER_MCU_BOARD_NUCLEO_F767ZI_HPP
/**
 * @file RoverBoard.hpp
 * @brief NUCLEO-F767ZI board description for the rover controller firmware.
 *
 * The link to the host is the ST-Link virtual COM port: USART3 on PD8
 * (TX) / PD9 (RX), AF7, the one USB-C cable. The three user LEDs show
 * lamp 1 of the rover: LD3 (red, PB14), LD1 (green, PB0), LD2 (blue,
 * PB7); yellow is red + green, white is all three. Clock 216 MHz from
 * the 8 MHz HSE bypass, as the encryptor firmware runs this board.
 */

#include "Stm32Uart.hpp"
#include "stm32f7xx_hal.h"

#include <stddef.h>
#include <stdint.h>

namespace appsim {
namespace rover_board {
namespace board {

/* ----------------------------- LEDs ----------------------------- */

static GPIO_TypeDef* const LED_PORT = GPIOB;
static constexpr uint16_t LED_GREEN_PIN = GPIO_PIN_0; // LD1
static constexpr uint16_t LED_BLUE_PIN = GPIO_PIN_7;  // LD2
static constexpr uint16_t LED_RED_PIN = GPIO_PIN_14;  // LD3
static constexpr uint16_t LED_ALL_PINS = LED_GREEN_PIN | LED_BLUE_PIN | LED_RED_PIN;

static inline void enableLedClock() noexcept { __HAL_RCC_GPIOB_CLK_ENABLE(); }

/// The pins lit for a lamp colour code (0 off, 1 red, 2 green, 3 blue, 4 yellow, 5 white).
static inline uint16_t pinsForColour(uint8_t colour) noexcept {
  switch (colour) {
  case 1:
    return LED_RED_PIN;
  case 2:
    return LED_GREEN_PIN;
  case 3:
    return LED_BLUE_PIN;
  case 4:
    return static_cast<uint16_t>(LED_RED_PIN | LED_GREEN_PIN);
  case 5:
    return LED_ALL_PINS;
  default:
    return 0;
  }
}

/* ----------------------------- Link (ST-Link VCP) ----------------------------- */

static USART_TypeDef* const LINK_UART = USART3;
static const apex::hal::stm32::Stm32UartPins LINK_UART_PINS = {GPIOD, GPIO_PIN_8, // TX
                                                               GPIOD, GPIO_PIN_9, // RX
                                                               GPIO_AF7_USART3};
static constexpr IRQn_Type LINK_UART_IRQN = USART3_IRQn;

/* ----------------------------- Clock ----------------------------- */

static constexpr uint32_t SYSCLK_HZ = 216000000;
static constexpr uint32_t CYCLES_PER_US = SYSCLK_HZ / 1000000;

/**
 * @brief SYSCLK 216 MHz from the 8 MHz HSE bypass (ST-Link MCO).
 *
 * PLL 8 MHz / 8 * 432 / 2; over-drive above 180 MHz; 7 wait states on
 * 3.3 V; APB1 54 MHz, APB2 108 MHz.
 * @note NOT RT-safe: boot only. Halts on a HAL failure (LEDs never light).
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
  clkInit.APB1CLKDivider = RCC_HCLK_DIV4;
  clkInit.APB2CLKDivider = RCC_HCLK_DIV2;
  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_7) != HAL_OK) {
    for (;;) {
    }
  }
}

/// Instruction cache on; the data cache stays off (no DMA, small RAM traffic).
static inline void configureCaches() noexcept { SCB_EnableICache(); }

} // namespace board
} // namespace rover_board
} // namespace appsim

#endif // APEX_HORIZON_DEMO_ROVER_MCU_BOARD_NUCLEO_F767ZI_HPP
