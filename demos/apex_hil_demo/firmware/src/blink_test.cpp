/**
 * @file blink_test.cpp
 * @brief Minimal LED blink test for NUCLEO-L476RG hardware validation.
 *
 * Blinks LD2 (PA5) at ~2Hz using only HAL. No UART, no SLIP, no CRC.
 * If the LED does not blink, the MCU hardware is damaged.
 */

#include "stm32l4xx_hal.h"

extern "C" {
void* __dso_handle = nullptr;
void __cxa_pure_virtual() {
  while (1) {
  }
}
int __cxa_atexit(void (*)(void*), void*, void*) { return 0; }
void _init() {}
void _fini() {}
}
void operator delete(void*, unsigned int) noexcept {}
void operator delete(void*) noexcept {}

extern "C" void SysTick_Handler() { HAL_IncTick(); }

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
    // PLL FAIL: fast blink (10 Hz) to indicate clock failure
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef gpio = {};
    gpio.Pin = GPIO_PIN_5;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    HAL_GPIO_Init(GPIOA, &gpio);
    while (1) {
      HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
      HAL_Delay(50);
    }
  }

  clkInit.ClockType =
      RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  clkInit.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  clkInit.AHBCLKDivider = RCC_SYSCLK_DIV1;
  clkInit.APB1CLKDivider = RCC_HCLK_DIV1;
  clkInit.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&clkInit, FLASH_LATENCY_4) != HAL_OK) {
    // CLK CONFIG FAIL: triple blink pattern
    while (1) {
      for (int i = 0; i < 6; i++) {
        HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
        HAL_Delay(100);
      }
      HAL_Delay(500);
    }
  }
}

int main() {
  HAL_Init();
  SystemClock_Config();

  // If we get here, PLL at 80 MHz succeeded
  __HAL_RCC_GPIOA_CLK_ENABLE();

  GPIO_InitTypeDef gpio = {};
  gpio.Pin = GPIO_PIN_5;
  gpio.Mode = GPIO_MODE_OUTPUT_PP;
  gpio.Pull = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &gpio);

  // Slow blink (1 Hz) = PLL success
  while (1) {
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    HAL_Delay(500);
  }

  return 0;
}
