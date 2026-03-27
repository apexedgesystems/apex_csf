#ifndef APEX_HAL_STM32_CAN_HPP
#define APEX_HAL_STM32_CAN_HPP
/**
 * @file Stm32Can.hpp
 * @brief STM32 bxCAN implementation using HAL drivers.
 *
 * Provides interrupt-driven CAN with a static RX frame ring buffer.
 *
 * Supported families (bxCAN peripheral):
 *  - STM32L4 (e.g., STM32L476xx)
 *  - STM32F1, STM32F4 (e.g., STM32F103xx, STM32F446xx)
 *  - STM32G4 (e.g., STM32G474xx) -- has FDCAN but some variants have bxCAN
 *
 * NOT supported (FDCAN peripheral -- different register model):
 *  - STM32H7 (e.g., STM32H743xx)
 *  - STM32G4 FDCAN variants
 *
 * Features:
 *  - Interrupt-driven RX via FIFO0 message pending callback
 *  - Static RX ring buffer (configurable depth)
 *  - TX via hardware mailboxes (3 available)
 *  - Hardware acceptance filters (up to 14 banks for single CAN)
 *  - Loopback and silent modes for testing without transceiver
 *  - GPIO, peripheral clock, and NVIC setup handled internally
 *
 * Usage:
 *  1. Define board pin mapping (or use a board header)
 *  2. Create instance with CAN peripheral and pin descriptor
 *  3. Optionally call addFilter() for acceptance filtering
 *  4. Call init() with config (GPIO, clocks, NVIC configured automatically)
 *  5. Call IRQ handlers from CAN1_RX0_IRQHandler and CAN1_SCE_IRQHandler
 *  6. Use send()/recv() for frame I/O
 *
 * @code
 * // Board pin descriptor (in a board-specific header)
 * const Stm32CanPins CAN1_BUS = {GPIOB, GPIO_PIN_9, GPIOB, GPIO_PIN_8, GPIO_AF9_CAN1};
 *
 * // In main.cpp
 * static Stm32Can<16> can(CAN1, CAN1_BUS);
 *
 * // In init
 * apex::hal::CanConfig cfg;
 * cfg.bitrate = 500000;
 * cfg.mode = apex::hal::CanMode::LOOPBACK;  // No transceiver needed
 * can.init(cfg);
 *
 * // IRQ handlers (vector table linkage -- must be in user code)
 * extern "C" void CAN1_RX0_IRQHandler() { can.irqHandlerRx0(); }
 * extern "C" void CAN1_SCE_IRQHandler() { can.irqHandlerSce(); }
 * @endcode
 *
 * Buffer sizing:
 *  - RxBufSize: number of CanFrame slots (not bytes)
 *  - 16 slots handles ~160 us of back-to-back frames at 1 Mbit/s
 *  - Power of 2 sizes enable efficient modulo via bitmask
 */

#include "src/system/core/hal/base/ICan.hpp"

// STM32 HAL includes (same family detection pattern as Stm32Uart)
#if defined(STM32H7xx) || defined(STM32H743xx)
#error "Stm32Can requires bxCAN peripheral. H7 uses FDCAN (not supported by this wrapper)."
#elif defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32F1) || defined(STM32F103xB)
#include "stm32f1xx_hal.h"
#elif defined(STM32F4xx) || defined(STM32F446xx)
#include "stm32f4xx_hal.h"
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#else
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32F4xx, STM32G4xx, etc."
#endif
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- Stm32CanPins ----------------------------- */

#ifndef APEX_HAL_STM32_MOCK
/**
 * @brief GPIO pin descriptor for STM32 CAN.
 *
 * Captures the TX/RX pin mapping for CAN on a specific board.
 * Define these in a board-specific header (e.g., NucleoL476rgPins.hpp).
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32CanPins {
  GPIO_TypeDef* txPort; ///< TX GPIO port (e.g., GPIOB).
  uint16_t txPin;       ///< TX GPIO pin mask (e.g., GPIO_PIN_9).
  GPIO_TypeDef* rxPort; ///< RX GPIO port (e.g., GPIOB).
  uint16_t rxPin;       ///< RX GPIO pin mask (e.g., GPIO_PIN_8).
  uint8_t alternate;    ///< Alternate function (e.g., GPIO_AF9_CAN1).
};
#endif

/* ----------------------------- Stm32CanOptions ----------------------------- */

/**
 * @brief Platform-specific options for STM32 CAN initialization.
 *
 * Controls NVIC interrupt priorities. Default priority 1 is lower than
 * UART (priority 0) to avoid starving serial communication.
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32CanOptions {
  uint8_t nvicPreemptPriority = 1; ///< NVIC preemption priority (0 = highest).
  uint8_t nvicSubPriority = 0;     ///< NVIC sub-priority.
};

/* ----------------------------- Stm32Can ----------------------------- */

/**
 * @class Stm32Can
 * @brief STM32 bxCAN implementation with static RX ring buffer.
 *
 * @tparam RxBufSize Number of CanFrame slots in RX ring buffer. Power of 2
 *         recommended. Each slot is ~16 bytes.
 *
 * Memory usage: RxBufSize * sizeof(CanFrame) + ~300 bytes overhead (includes
 * CAN_HandleTypeDef, pin descriptor, filter array, stats, and buffer pointers).
 *
 * Example instantiation:
 * @code
 * const Stm32CanPins BUS = {GPIOB, GPIO_PIN_9, GPIOB, GPIO_PIN_8, GPIO_AF9_CAN1};
 * Stm32Can<16> can(CAN1, BUS);  // 16-frame RX buffer
 * @endcode
 */
template <size_t RxBufSize = 16> class Stm32Can final : public ICan {
public:
  static_assert(RxBufSize > 0, "RX buffer size must be > 0");
  static_assert(RxBufSize <= 64, "RX buffer size too large (max 64 frames)");

  static constexpr size_t MAX_FILTERS = 14; ///< Max filter banks for single CAN.

#ifndef APEX_HAL_STM32_MOCK
  /**
   * @brief Construct CAN wrapper for a specific CAN peripheral.
   * @param instance CAN peripheral (e.g., CAN1).
   * @param pins GPIO pin descriptor for this CAN on this board.
   * @note NOT RT-safe: Construction only.
   */
  Stm32Can(CAN_TypeDef* instance, const Stm32CanPins& pins) noexcept : pins_(pins) {
    hcan_.Instance = instance;
  }
#else
  /**
   * @brief Mock constructor for host-side testing.
   */
  Stm32Can() noexcept = default;
#endif

  ~Stm32Can() override { deinit(); }

  // Non-copyable, non-movable (tied to hardware peripheral)
  Stm32Can(const Stm32Can&) = delete;
  Stm32Can& operator=(const Stm32Can&) = delete;
  Stm32Can(Stm32Can&&) = delete;
  Stm32Can& operator=(Stm32Can&&) = delete;

  /* ----------------------------- ICan Interface ----------------------------- */

  /**
   * @brief Initialize CAN with default NVIC options.
   * @param config CAN parameters (bitrate, mode, auto-retransmit).
   * @return CanStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, NVIC, and CAN.
   */
  [[nodiscard]] CanStatus init(const CanConfig& config) noexcept override {
    return init(config, Stm32CanOptions{});
  }

  /**
   * @brief Initialize CAN with explicit NVIC priority.
   * @param config CAN parameters (bitrate, mode, auto-retransmit).
   * @param opts Platform-specific options (NVIC priority).
   * @return CanStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, NVIC, and CAN.
   */
  [[nodiscard]] CanStatus init(const CanConfig& config, const Stm32CanOptions& opts) noexcept {
    // Double-init guard: clean up before reinitializing
    if (initialized_) {
      deinit();
    }

#ifndef APEX_HAL_STM32_MOCK
    // Enable peripheral and GPIO clocks
    enablePeripheralClock();
    enableGpioClock(pins_.txPort);
    if (pins_.rxPort != pins_.txPort) {
      enableGpioClock(pins_.rxPort);
    }

    // Configure TX pin
    GPIO_InitTypeDef gpioInit = {};
    gpioInit.Pin = pins_.txPin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_PULLUP;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = pins_.alternate;
    HAL_GPIO_Init(pins_.txPort, &gpioInit);

    // Configure RX pin
    gpioInit.Pin = pins_.rxPin;
    HAL_GPIO_Init(pins_.rxPort, &gpioInit);

    // Compute bit timing for requested bitrate
    const BitTiming TIMING = computeBitTiming(config.bitrate);

    // Configure CAN parameters
    hcan_.Init.Prescaler = TIMING.prescaler;
    hcan_.Init.TimeSeg1 = TIMING.bs1;
    hcan_.Init.TimeSeg2 = TIMING.bs2;
    hcan_.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan_.Init.TimeTriggeredMode = DISABLE;
    hcan_.Init.AutoBusOff = DISABLE;
    hcan_.Init.AutoWakeUp = DISABLE;
    hcan_.Init.AutoRetransmission = config.autoRetransmit ? ENABLE : DISABLE;
    hcan_.Init.ReceiveFifoLocked = DISABLE;
    hcan_.Init.TransmitFifoPriority = DISABLE;

    switch (config.mode) {
    case CanMode::LOOPBACK:
      hcan_.Init.Mode = CAN_MODE_LOOPBACK;
      break;
    case CanMode::SILENT:
      hcan_.Init.Mode = CAN_MODE_SILENT;
      break;
    case CanMode::SILENT_LOOPBACK:
      hcan_.Init.Mode = CAN_MODE_SILENT_LOOPBACK;
      break;
    default:
      hcan_.Init.Mode = CAN_MODE_NORMAL;
      break;
    }

    if (HAL_CAN_Init(&hcan_) != HAL_OK) {
      return CanStatus::ERROR_INVALID_ARG;
    }

    // Configure filters
    if (filterCount_ == 0) {
      // Accept all frames
      CAN_FilterTypeDef filterCfg = {};
      filterCfg.FilterBank = 0;
      filterCfg.FilterMode = CAN_FILTERMODE_IDMASK;
      filterCfg.FilterScale = CAN_FILTERSCALE_32BIT;
      filterCfg.FilterIdHigh = 0x0000;
      filterCfg.FilterIdLow = 0x0000;
      filterCfg.FilterMaskIdHigh = 0x0000;
      filterCfg.FilterMaskIdLow = 0x0000;
      filterCfg.FilterFIFOAssignment = CAN_RX_FIFO0;
      filterCfg.FilterActivation = ENABLE;
      filterCfg.SlaveStartFilterBank = 14;

      if (HAL_CAN_ConfigFilter(&hcan_, &filterCfg) != HAL_OK) {
        return CanStatus::ERROR_INVALID_ARG;
      }
    } else {
      for (size_t i = 0; i < filterCount_; ++i) {
        CAN_FilterTypeDef filterCfg = {};
        filterCfg.FilterBank = static_cast<uint32_t>(i);
        filterCfg.FilterMode = CAN_FILTERMODE_IDMASK;
        filterCfg.FilterScale = CAN_FILTERSCALE_32BIT;

        if (filters_[i].extended) {
          filterCfg.FilterIdHigh = static_cast<uint16_t>((filters_[i].id << 3) >> 16);
          filterCfg.FilterIdLow = static_cast<uint16_t>((filters_[i].id << 3) | 0x04);
          filterCfg.FilterMaskIdHigh = static_cast<uint16_t>((filters_[i].mask << 3) >> 16);
          filterCfg.FilterMaskIdLow = static_cast<uint16_t>((filters_[i].mask << 3) | 0x04);
        } else {
          filterCfg.FilterIdHigh = static_cast<uint16_t>(filters_[i].id << 5);
          filterCfg.FilterIdLow = 0x0000;
          filterCfg.FilterMaskIdHigh = static_cast<uint16_t>(filters_[i].mask << 5);
          filterCfg.FilterMaskIdLow = 0x0000;
        }

        filterCfg.FilterFIFOAssignment = CAN_RX_FIFO0;
        filterCfg.FilterActivation = ENABLE;
        filterCfg.SlaveStartFilterBank = 14;

        if (HAL_CAN_ConfigFilter(&hcan_, &filterCfg) != HAL_OK) {
          return CanStatus::ERROR_INVALID_ARG;
        }
      }
    }

    // Start CAN
    if (HAL_CAN_Start(&hcan_) != HAL_OK) {
      return CanStatus::ERROR_INVALID_ARG;
    }

    // Enable RX FIFO0 message pending interrupt
    if (HAL_CAN_ActivateNotification(&hcan_, CAN_IT_RX_FIFO0_MSG_PENDING) != HAL_OK) {
      return CanStatus::ERROR_INVALID_ARG;
    }

    // Enable error and status change interrupts
    HAL_CAN_ActivateNotification(&hcan_, CAN_IT_ERROR_WARNING | CAN_IT_ERROR_PASSIVE |
                                             CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE | CAN_IT_ERROR);

    // Enable NVIC
    HAL_NVIC_SetPriority(CAN1_RX0_IRQn, opts.nvicPreemptPriority, opts.nvicSubPriority);
    HAL_NVIC_EnableIRQ(CAN1_RX0_IRQn);
    HAL_NVIC_SetPriority(CAN1_SCE_IRQn, opts.nvicPreemptPriority, opts.nvicSubPriority);
    HAL_NVIC_EnableIRQ(CAN1_SCE_IRQn);

    initialized_ = true;
    return CanStatus::OK;
#else
    (void)config;
    (void)opts;
    initialized_ = true;
    return CanStatus::OK;
#endif
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (initialized_) {
      HAL_NVIC_DisableIRQ(CAN1_RX0_IRQn);
      HAL_NVIC_DisableIRQ(CAN1_SCE_IRQn);
      HAL_CAN_Stop(&hcan_);
      HAL_CAN_DeInit(&hcan_);
    }
#endif
    initialized_ = false;
    rxHead_ = 0;
    rxTail_ = 0;
    busOff_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] CanStatus send(const CanFrame& frame) noexcept override {
    if (!initialized_) {
      return CanStatus::ERROR_NOT_INIT;
    }
    if (frame.dlc > 8) {
      return CanStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    // Check for free TX mailbox
    if (HAL_CAN_GetTxMailboxesFreeLevel(&hcan_) == 0) {
      ++stats_.txOverflows;
      return CanStatus::WOULD_BLOCK;
    }

    CAN_TxHeaderTypeDef txHeader = {};
    if (frame.canId.extended) {
      txHeader.ExtId = frame.canId.id;
      txHeader.IDE = CAN_ID_EXT;
    } else {
      txHeader.StdId = frame.canId.id;
      txHeader.IDE = CAN_ID_STD;
    }
    txHeader.RTR = frame.canId.remote ? CAN_RTR_REMOTE : CAN_RTR_DATA;
    txHeader.DLC = frame.dlc;
    txHeader.TransmitGlobalTime = DISABLE;

    uint32_t txMailbox = 0;
    if (HAL_CAN_AddTxMessage(&hcan_, &txHeader, frame.data, &txMailbox) != HAL_OK) {
      ++stats_.txOverflows;
      return CanStatus::WOULD_BLOCK;
    }

    ++stats_.framesTx;
    return CanStatus::OK;
#else
    ++stats_.framesTx;
    return CanStatus::OK;
#endif
  }

  [[nodiscard]] CanStatus recv(CanFrame& frame) noexcept override {
    if (!initialized_) {
      return CanStatus::ERROR_NOT_INIT;
    }

    if (rxTail_ == rxHead_) {
      return CanStatus::WOULD_BLOCK;
    }

    frame = rxBuf_[rxTail_];
    rxTail_ = (rxTail_ + 1) % RxBufSize;
    return CanStatus::OK;
  }

  [[nodiscard]] bool txReady() const noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (!initialized_) {
      return false;
    }
    return HAL_CAN_GetTxMailboxesFreeLevel(&hcan_) > 0;
#else
    return initialized_;
#endif
  }

  [[nodiscard]] size_t rxAvailable() const noexcept override {
    if (rxHead_ >= rxTail_) {
      return rxHead_ - rxTail_;
    }
    return RxBufSize - rxTail_ + rxHead_;
  }

  [[nodiscard]] CanStatus addFilter(const CanFilter& filter) noexcept override {
    if (filterCount_ >= MAX_FILTERS) {
      return CanStatus::ERROR_INVALID_ARG;
    }
    filters_[filterCount_] = filter;
    ++filterCount_;
    return CanStatus::OK;
  }

  void clearFilters() noexcept override { filterCount_ = 0; }

  [[nodiscard]] const CanStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  [[nodiscard]] bool isBusOff() const noexcept override { return busOff_; }

  /* ----------------------------- IRQ Handlers ----------------------------- */

  /**
   * @brief Handle CAN RX FIFO0 message pending interrupt.
   *
   * Call this from CAN1_RX0_IRQHandler.
   *
   * @code
   * extern "C" void CAN1_RX0_IRQHandler() {
   *   can.irqHandlerRx0();
   * }
   * @endcode
   *
   * @note Must be called from interrupt context.
   */
  void irqHandlerRx0() noexcept {
#ifndef APEX_HAL_STM32_MOCK
    CAN_RxHeaderTypeDef rxHeader = {};
    uint8_t rxData[8] = {};

    while (HAL_CAN_GetRxFifoFillLevel(&hcan_, CAN_RX_FIFO0) > 0) {
      if (HAL_CAN_GetRxMessage(&hcan_, CAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK) {
        ++stats_.errorFrames;
        break;
      }

      size_t nextHead = (rxHead_ + 1) % RxBufSize;
      if (nextHead != rxTail_) {
        CanFrame& slot = rxBuf_[rxHead_];
        if (rxHeader.IDE == CAN_ID_EXT) {
          slot.canId.id = rxHeader.ExtId;
          slot.canId.extended = true;
        } else {
          slot.canId.id = rxHeader.StdId;
          slot.canId.extended = false;
        }
        slot.canId.remote = (rxHeader.RTR == CAN_RTR_REMOTE);
        slot.dlc = static_cast<uint8_t>(rxHeader.DLC);
        for (uint8_t i = 0; i < slot.dlc && i < 8; ++i) {
          slot.data[i] = rxData[i];
        }

        rxHead_ = nextHead;
        ++stats_.framesRx;
      } else {
        ++stats_.rxOverflows;
      }
    }
#endif
  }

  /**
   * @brief Handle CAN status change / error interrupt.
   *
   * Call this from CAN1_SCE_IRQHandler.
   *
   * @code
   * extern "C" void CAN1_SCE_IRQHandler() {
   *   can.irqHandlerSce();
   * }
   * @endcode
   *
   * @note Must be called from interrupt context.
   */
  void irqHandlerSce() noexcept {
#ifndef APEX_HAL_STM32_MOCK
    uint32_t errorCode = HAL_CAN_GetError(&hcan_);

    if (errorCode & HAL_CAN_ERROR_BOF) {
      busOff_ = true;
      ++stats_.busOffCount;
    }
    if (errorCode & HAL_CAN_ERROR_EPV) {
      ++stats_.errorFrames;
    }
    if (errorCode & HAL_CAN_ERROR_EWG) {
      ++stats_.errorFrames;
    }

    // Clear error flags
    __HAL_CAN_CLEAR_FLAG(&hcan_, CAN_FLAG_ERRI);
#endif
  }

  /* ----------------------------- Buffer Info ----------------------------- */

  /**
   * @brief Get RX buffer capacity.
   * @return RxBufSize - 1 (one slot reserved for full detection).
   * @note RT-safe.
   */
  [[nodiscard]] static constexpr size_t rxCapacity() noexcept { return RxBufSize - 1; }

private:
#ifndef APEX_HAL_STM32_MOCK
  /* ----------------------------- Platform Helpers ----------------------------- */

  /**
   * @brief Bit timing parameters for CAN baud rate.
   */
  struct BitTiming {
    uint32_t prescaler;
    uint32_t bs1;
    uint32_t bs2;
  };

  /**
   * @brief Compute bit timing for a given bitrate.
   *
   * Assumes 80 MHz APB1 clock (STM32L4 default).
   * TQ = prescaler / 80 MHz.
   * Bit time = 1 + BS1 + BS2 = 10 TQ.
   * Bitrate = 80 MHz / (prescaler * 10).
   *
   * @param bitrate Desired bitrate in bps.
   * @return BitTiming struct with prescaler, BS1, BS2.
   * @note NOT RT-safe: Called once during init().
   */
  static BitTiming computeBitTiming(uint32_t bitrate) noexcept {
    // Standard bit timings for 80 MHz APB1, 10 TQ per bit (BS1=4, BS2=5)
    // Prescaler = 80 MHz / (bitrate * 10)
    BitTiming timing = {};
    timing.bs1 = CAN_BS1_4TQ;
    timing.bs2 = CAN_BS2_5TQ;

    switch (bitrate) {
    case 1000000:
      timing.prescaler = 8;
      break;
    case 500000:
      timing.prescaler = 16;
      break;
    case 250000:
      timing.prescaler = 32;
      break;
    case 125000:
      timing.prescaler = 64;
      break;
    case 100000:
      timing.prescaler = 80;
      break;
    default:
      // Best effort: compute prescaler for 10 TQ per bit
      timing.prescaler = 80000000U / (bitrate * 10U);
      if (timing.prescaler == 0) {
        timing.prescaler = 1;
      }
      break;
    }

    return timing;
  }

  /**
   * @brief Enable the RCC clock for the CAN peripheral.
   * @note NOT RT-safe: Called once during init().
   */
  void enablePeripheralClock() noexcept { __HAL_RCC_CAN1_CLK_ENABLE(); }

  /**
   * @brief Enable the RCC clock for a GPIO port.
   * @param port GPIO port (e.g., GPIOB).
   * @note NOT RT-safe: Called once during init().
   */
  static void enableGpioClock(GPIO_TypeDef* port) noexcept {
    if (port == GPIOA) {
      __HAL_RCC_GPIOA_CLK_ENABLE();
    } else if (port == GPIOB) {
      __HAL_RCC_GPIOB_CLK_ENABLE();
    } else if (port == GPIOC) {
      __HAL_RCC_GPIOC_CLK_ENABLE();
    }
#if defined(GPIOD)
    else if (port == GPIOD) {
      __HAL_RCC_GPIOD_CLK_ENABLE();
    }
#endif
#if defined(GPIOE)
    else if (port == GPIOE) {
      __HAL_RCC_GPIOE_CLK_ENABLE();
    }
#endif
#if defined(GPIOF)
    else if (port == GPIOF) {
      __HAL_RCC_GPIOF_CLK_ENABLE();
    }
#endif
#if defined(GPIOG)
    else if (port == GPIOG) {
      __HAL_RCC_GPIOG_CLK_ENABLE();
    }
#endif
#if defined(GPIOH)
    else if (port == GPIOH) {
      __HAL_RCC_GPIOH_CLK_ENABLE();
    }
#endif
  }

  CAN_HandleTypeDef hcan_ = {};
  Stm32CanPins pins_ = {};
#endif

  bool initialized_ = false;
  volatile bool busOff_ = false;

  // RX frame ring buffer
  CanFrame rxBuf_[RxBufSize] = {};
  volatile size_t rxHead_ = 0;
  volatile size_t rxTail_ = 0;

  // Acceptance filters (configured before init)
  CanFilter filters_[MAX_FILTERS] = {};
  size_t filterCount_ = 0;

  CanStats stats_ = {};
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_CAN_HPP
