/**
 * @file stm32l4xx_hal_conf.h
 * @brief STM32L4 HAL configuration for apex_hil_demo firmware.
 *
 * Enables only the HAL modules needed: GPIO (LED), UART (VCP), RCC, PWR, DMA.
 * Flash module required by RCC HAL (FLASH_LATENCY constants for clock config).
 */

#ifndef STM32L4XX_HAL_CONF_H
#define STM32L4XX_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------- Module Selection ----------------------------- */

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* ----------------------------- Oscillator Values ----------------------------- */

#if !defined(HSE_VALUE)
#define HSE_VALUE 8000000U
#endif

#if !defined(HSE_STARTUP_TIMEOUT)
#define HSE_STARTUP_TIMEOUT 100U
#endif

#if !defined(MSI_VALUE)
#define MSI_VALUE 4000000U
#endif

#if !defined(HSI_VALUE)
#define HSI_VALUE 16000000U
#endif

#if !defined(HSI48_VALUE)
#define HSI48_VALUE 48000000U
#endif

#if !defined(LSI_VALUE)
#define LSI_VALUE 32000U
#endif

#if !defined(LSE_VALUE)
#define LSE_VALUE 32768U
#endif

#if !defined(LSE_STARTUP_TIMEOUT)
#define LSE_STARTUP_TIMEOUT 5000U
#endif

#if !defined(EXTERNAL_SAI1_CLOCK_VALUE)
#define EXTERNAL_SAI1_CLOCK_VALUE 2097000U
#endif

#if !defined(EXTERNAL_SAI2_CLOCK_VALUE)
#define EXTERNAL_SAI2_CLOCK_VALUE 2097000U
#endif

/* ----------------------------- System Configuration ----------------------------- */

#define VDD_VALUE 3300U
#define TICK_INT_PRIORITY 0x0FU
#define USE_RTOS 0U
#define PREFETCH_ENABLE 0U
#define INSTRUCTION_CACHE_ENABLE 1U
#define DATA_CACHE_ENABLE 1U

/* ----------------------------- Assert Configuration ----------------------------- */

#define assert_param(expr) ((void)0U)

/* ----------------------------- Include HAL Headers ----------------------------- */

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32l4xx_hal_rcc.h"
#include "stm32l4xx_hal_rcc_ex.h"
#endif

#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32l4xx_hal_flash.h"
#include "stm32l4xx_hal_flash_ex.h"
#include "stm32l4xx_hal_flash_ramfunc.h"
#endif

#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32l4xx_hal_gpio.h"
#include "stm32l4xx_hal_gpio_ex.h"
#endif

#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32l4xx_hal_cortex.h"
#endif

#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32l4xx_hal_pwr.h"
#include "stm32l4xx_hal_pwr_ex.h"
#endif

#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32l4xx_hal_dma.h"
#include "stm32l4xx_hal_dma_ex.h"
#endif

#ifdef HAL_UART_MODULE_ENABLED
#include "stm32l4xx_hal_uart.h"
#include "stm32l4xx_hal_uart_ex.h"
#endif

#ifdef __cplusplus
}
#endif

#endif /* STM32L4XX_HAL_CONF_H */
