#ifndef APEX_HIL_DEMO_FIRMWARE_CONFIG_HPP
#define APEX_HIL_DEMO_FIRMWARE_CONFIG_HPP
/**
 * @file FirmwareConfig.hpp
 * @brief STM32-specific hardware configuration for the HIL firmware.
 *
 * Pin definitions, UART mapping, and board-level constants for the
 * NUCLEO-L476RG development board.
 *
 * @note RT-safe: Compile-time constants only.
 */

#include "stm32l4xx_hal.h"

#include "src/system/core/hal/stm32/inc/Stm32Uart.hpp"

namespace appsim {
namespace firmware {

/* ----------------------------- LED ----------------------------- */

/// LED pin (PA5 on NUCLEO-L476RG).
static constexpr uint16_t LED_PIN = GPIO_PIN_5;

/// LED GPIO port.
static GPIO_TypeDef* const LED_PORT = GPIOA;

/* ----------------------------- UART ----------------------------- */

/// USART1 pins: PA9 (TX), PA10 (RX), AF7 -- connected to FTDI adapter.
static const apex::hal::stm32::Stm32UartPins HIL_UART_PINS = {GPIOA, GPIO_PIN_9,  // TX
                                                              GPIOA, GPIO_PIN_10, // RX
                                                              GPIO_AF7_USART1};

/* ----------------------------- Clock ----------------------------- */

/// System clock frequency [Hz] (80 MHz via MSI + PLL).
static constexpr uint32_t SYSCLK_HZ = 80000000;

/// DWT cycles per microsecond.
static constexpr uint32_t CYCLES_PER_US = SYSCLK_HZ / 1000000;

} // namespace firmware
} // namespace appsim

#endif // APEX_HIL_DEMO_FIRMWARE_CONFIG_HPP
