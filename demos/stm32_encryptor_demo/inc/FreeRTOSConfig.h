/**
 * @file FreeRTOSConfig.h
 * @brief FreeRTOS kernel configuration for stm32_encryptor_demo_freertos.
 *
 * Target: NUCLEO-L476RG (Cortex-M4F, 80 MHz, 96 KB SRAM1)
 *
 * Configuration choices:
 *   - Preemptive scheduler (configUSE_PREEMPTION = 1)
 *   - 1 kHz kernel tick (matches HAL tick rate)
 *   - 8 KB heap (heap_4.c with coalescence)
 *   - Static allocation disabled (dynamic via heap_4)
 *   - vTaskDelayUntil enabled (used by FreeRtosTickSource)
 *   - SVC_Handler and PendSV_Handler mapped to FreeRTOS port
 *   - SysTick_Handler NOT mapped (we provide our own to share with HAL)
 */

#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#if defined(__ICCARM__) || defined(__CC_ARM) || defined(__GNUC__)
#include <stdint.h>
extern uint32_t SystemCoreClock;
#endif

/* ----------------------------- Kernel Configuration ----------------------------- */

#define configUSE_PREEMPTION 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configCPU_CLOCK_HZ (SystemCoreClock)
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES (7)
#define configMINIMAL_STACK_SIZE ((uint16_t)128)
#define configTOTAL_HEAP_SIZE ((size_t)(8 * 1024))
#define configMAX_TASK_NAME_LEN (16)
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1

/* ----------------------------- Feature Selection ----------------------------- */

#define configSUPPORT_STATIC_ALLOCATION 0
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 0
#define configUSE_COUNTING_SEMAPHORES 0
#define configUSE_TRACE_FACILITY 0
#define configUSE_APPLICATION_TASK_TAG 0
#define configQUEUE_REGISTRY_SIZE 0
#define configCHECK_FOR_STACK_OVERFLOW 2
#define configUSE_MALLOC_FAILED_HOOK 1
#define configGENERATE_RUN_TIME_STATS 0

/* ----------------------------- Co-routines ----------------------------- */

#define configUSE_CO_ROUTINES 0
#define configMAX_CO_ROUTINE_PRIORITIES (2)

/* ----------------------------- Software Timers ----------------------------- */

#define configUSE_TIMERS 0
#define configTIMER_TASK_PRIORITY (2)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH (configMINIMAL_STACK_SIZE * 2)

/* ----------------------------- API Inclusion ----------------------------- */

#define INCLUDE_vTaskPrioritySet 0
#define INCLUDE_uxTaskPriorityGet 0
#define INCLUDE_vTaskDelete 0
#define INCLUDE_vTaskCleanUpResources 0
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1

/* ----------------------------- Cortex-M4 Interrupt Priorities ----------------------------- */

/*
 * STM32L4 has 4 NVIC priority bits (16 levels, 0-15).
 * FreeRTOS requires:
 *   - PendSV and SysTick at lowest priority (15)
 *   - ISRs calling FreeRTOS API must have priority >= MAX_SYSCALL (5)
 *   - ISRs with priority < 5 cannot call FreeRTOS API but are never masked
 */
#ifdef __NVIC_PRIO_BITS
#define configPRIO_BITS __NVIC_PRIO_BITS
#else
#define configPRIO_BITS 4
#endif

#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY 0x0F
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY 5

#define configKERNEL_INTERRUPT_PRIORITY                                                            \
  (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#define configMAX_SYSCALL_INTERRUPT_PRIORITY                                                       \
  (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* ----------------------------- Assert ----------------------------- */

#define configASSERT(x)                                                                            \
  if ((x) == 0) {                                                                                  \
    taskDISABLE_INTERRUPTS();                                                                      \
    for (;;) {                                                                                     \
    }                                                                                              \
  }

/* ----------------------------- Handler Mapping ----------------------------- */

/*
 * Map FreeRTOS port handlers to CMSIS standard names.
 * SysTick_Handler is NOT mapped here -- we provide our own that calls
 * both HAL_IncTick() and xPortSysTickHandler().
 */
#define vPortSVCHandler SVC_Handler
#define xPortPendSVHandler PendSV_Handler

#endif /* FREERTOS_CONFIG_H */
