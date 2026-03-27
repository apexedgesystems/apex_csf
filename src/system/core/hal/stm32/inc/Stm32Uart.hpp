#ifndef APEX_HAL_STM32_UART_HPP
#define APEX_HAL_STM32_UART_HPP
/**
 * @file Stm32Uart.hpp
 * @brief STM32 UART implementation using HAL or LL drivers.
 *
 * Provides interrupt-driven UART with static circular buffers.
 *
 * Supported families (new UART peripheral with RDR/TDR/ISR/ICR registers):
 *  - STM32L4 (e.g., STM32L476xx)
 *  - STM32G4 (e.g., STM32G474xx)
 *  - STM32H7 (e.g., STM32H743xx)
 *
 * NOT supported (old UART peripheral with single DR/SR registers):
 *  - STM32F1, STM32F2, STM32F4
 *
 * Features:
 *  - Interrupt-driven RX (no polling in hot path)
 *  - Buffered TX with interrupt completion
 *  - Configurable buffer sizes (compile-time)
 *  - Error detection and statistics
 *  - GPIO, peripheral clock, and NVIC setup handled internally
 *
 * Usage:
 *  1. Define board pin mapping (or use a board header)
 *  2. Create instance with USART peripheral and pin descriptor
 *  3. Call init() with config (GPIO, clocks, NVIC configured automatically)
 *  4. Call IRQ handler from USARTx_IRQHandler
 *  5. Use read()/write() for I/O
 *
 * @code
 * // Board pin descriptor (in a board-specific header)
 * const Stm32UartPins USART2_VCP = {GPIOA, GPIO_PIN_2, GPIOA, GPIO_PIN_3, GPIO_AF7_USART2};
 *
 * // In main.cpp
 * static Stm32Uart<256, 256> uart(USART2, USART2_VCP);
 *
 * // In init
 * apex::hal::UartConfig cfg;
 * cfg.baudRate = 115200;
 * uart.init(cfg);  // GPIO, clocks, NVIC all configured here
 *
 * // IRQ handler (vector table linkage -- must be in user code)
 * extern "C" void USART2_IRQHandler() {
 *   uart.irqHandler();
 * }
 * @endcode
 *
 * Buffer sizing:
 *  - RX buffer: Should handle burst reception between read() calls
 *  - TX buffer: Should handle typical message sizes
 *  - Power of 2 sizes enable efficient modulo via bitmask
 */

#include "src/system/core/hal/base/IUart.hpp"

// STM32 HAL includes - user must have configured their project
// Note: STM32CubeL4 defines STM32L476xx (chip-specific), not STM32L4xx (family)
//
// Only new-UART families are supported (RDR/TDR register model).
// F1/F2/F4 use a different register layout (single DR) and are not compatible.
#if defined(STM32F1) || defined(STM32F2) || defined(STM32F4xx) || defined(STM32F401xC) ||          \
    defined(STM32F411xE) || defined(STM32F446xx)
#error "Stm32Uart requires new UART peripheral (RDR/TDR). F1/F2/F4 not supported."
#elif defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#elif defined(STM32H7xx) || defined(STM32H743xx)
#include "stm32h7xx_hal.h"
#else
// Allow compilation on host for testing (mocks needed)
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32G4xx, STM32H7xx, etc."
#endif
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- Stm32UartPins ----------------------------- */

#ifndef APEX_HAL_STM32_MOCK
/**
 * @brief GPIO pin descriptor for STM32 UART.
 *
 * Captures the TX/RX pin mapping for a specific UART peripheral on a
 * specific board. One instance per UART channel.
 *
 * Define these in a board-specific header (e.g., NucleoL476rgPins.hpp).
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32UartPins {
  GPIO_TypeDef* txPort; ///< TX GPIO port (e.g., GPIOA).
  uint16_t txPin;       ///< TX GPIO pin mask (e.g., GPIO_PIN_9).
  GPIO_TypeDef* rxPort; ///< RX GPIO port (e.g., GPIOA).
  uint16_t rxPin;       ///< RX GPIO pin mask (e.g., GPIO_PIN_10).
  uint8_t alternate;    ///< Alternate function (e.g., GPIO_AF7_USART1).
};
#endif

/* ----------------------------- Stm32UartOptions ----------------------------- */

/**
 * @brief Platform-specific options for STM32 UART initialization.
 *
 * Controls NVIC interrupt priority. Defaults are suitable for most
 * single-priority applications.
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32UartOptions {
  uint8_t nvicPreemptPriority = 0; ///< NVIC preemption priority (0 = highest).
  uint8_t nvicSubPriority = 0;     ///< NVIC sub-priority.
};

/* ----------------------------- Stm32Uart ----------------------------- */

/**
 * @class Stm32Uart
 * @brief STM32 UART implementation with static circular buffers.
 *
 * @tparam RxBufSize Size of RX circular buffer (bytes). Power of 2 recommended.
 * @tparam TxBufSize Size of TX circular buffer (bytes). Power of 2 recommended.
 *
 * Memory usage: RxBufSize + TxBufSize + ~120 bytes overhead (includes
 * UART_HandleTypeDef, pin descriptor, stats, and buffer pointers).
 *
 * Example instantiation:
 * @code
 * const Stm32UartPins VCP = {GPIOA, GPIO_PIN_2, GPIOA, GPIO_PIN_3, GPIO_AF7_USART2};
 * Stm32Uart<256, 128> uart(USART2, VCP);  // 256-byte RX, 128-byte TX
 * @endcode
 */
template <size_t RxBufSize = 256, size_t TxBufSize = 256> class Stm32Uart final : public IUart {
public:
  static_assert(RxBufSize > 0, "RX buffer size must be > 0");
  static_assert(TxBufSize > 0, "TX buffer size must be > 0");
  static_assert(RxBufSize <= 4096, "RX buffer size too large");
  static_assert(TxBufSize <= 4096, "TX buffer size too large");

#ifndef APEX_HAL_STM32_MOCK
  /**
   * @brief Construct UART wrapper for a specific USART peripheral.
   * @param instance USART peripheral (e.g., USART1, USART2, USART3).
   * @param pins GPIO pin descriptor for this UART on this board.
   * @note NOT RT-safe: Construction only.
   */
  Stm32Uart(USART_TypeDef* instance, const Stm32UartPins& pins) noexcept : pins_(pins) {
    huart_.Instance = instance;
  }
#else
  /**
   * @brief Mock constructor for host-side testing.
   */
  Stm32Uart() noexcept = default;
#endif

  ~Stm32Uart() override { deinit(); }

  // Non-copyable, non-movable (tied to hardware peripheral)
  Stm32Uart(const Stm32Uart&) = delete;
  Stm32Uart& operator=(const Stm32Uart&) = delete;
  Stm32Uart(Stm32Uart&&) = delete;
  Stm32Uart& operator=(Stm32Uart&&) = delete;

  /* ----------------------------- IUart Interface ----------------------------- */

  /**
   * @brief Initialize UART with default NVIC options.
   * @param config UART parameters (baud rate, data bits, parity, stop bits).
   * @return UartStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, NVIC, and UART.
   */
  [[nodiscard]] UartStatus init(const UartConfig& config) noexcept override {
    return init(config, Stm32UartOptions{});
  }

  /**
   * @brief Initialize UART with explicit NVIC priority.
   * @param config UART parameters (baud rate, data bits, parity, stop bits).
   * @param opts Platform-specific options (NVIC priority).
   * @return UartStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, NVIC, and UART.
   */
  [[nodiscard]] UartStatus init(const UartConfig& config, const Stm32UartOptions& opts) noexcept {
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

    // Configure UART parameters
    huart_.Init.BaudRate = config.baudRate;
    huart_.Init.WordLength = (config.dataBits == 9) ? UART_WORDLENGTH_9B : UART_WORDLENGTH_8B;
    huart_.Init.StopBits =
        (config.stopBits == UartStopBits::TWO) ? UART_STOPBITS_2 : UART_STOPBITS_1;

    switch (config.parity) {
    case UartParity::ODD:
      huart_.Init.Parity = UART_PARITY_ODD;
      break;
    case UartParity::EVEN:
      huart_.Init.Parity = UART_PARITY_EVEN;
      break;
    default:
      huart_.Init.Parity = UART_PARITY_NONE;
      break;
    }

    huart_.Init.Mode = UART_MODE_TX_RX;
    huart_.Init.HwFlowCtl = config.hwFlowControl ? UART_HWCONTROL_RTS_CTS : UART_HWCONTROL_NONE;
    huart_.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart_) != HAL_OK) {
      return UartStatus::ERROR_INVALID_ARG;
    }

    // Enable RX interrupt
    __HAL_UART_ENABLE_IT(&huart_, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart_, UART_IT_ERR);

    // Enable NVIC
    const IRQn_Type IRQ = irqn();
    HAL_NVIC_SetPriority(IRQ, opts.nvicPreemptPriority, opts.nvicSubPriority);
    HAL_NVIC_EnableIRQ(IRQ);

    initialized_ = true;
    return UartStatus::OK;
#else
    (void)config;
    (void)opts;
    initialized_ = true;
    return UartStatus::OK;
#endif
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (initialized_) {
      HAL_NVIC_DisableIRQ(irqn());
      __HAL_UART_DISABLE_IT(&huart_, UART_IT_RXNE);
      __HAL_UART_DISABLE_IT(&huart_, UART_IT_TXE);
      __HAL_UART_DISABLE_IT(&huart_, UART_IT_ERR);
      HAL_UART_DeInit(&huart_);
    }
#endif
    initialized_ = false;
    rxHead_ = 0;
    rxTail_ = 0;
    txHead_ = 0;
    txTail_ = 0;
    txActive_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  size_t write(const uint8_t* data, size_t len) noexcept override {
    if (!initialized_ || data == nullptr || len == 0) {
      return 0;
    }

    size_t written = 0;

    // Copy data to TX buffer
    while (written < len) {
      size_t nextHead = (txHead_ + 1) % TxBufSize;
      if (nextHead == txTail_) {
        // Buffer full
        break;
      }
      txBuf_[txHead_] = data[written];
      txHead_ = nextHead;
      ++written;
    }

    stats_.bytesTx += written;

#ifndef APEX_HAL_STM32_MOCK
    // Ensure TXE interrupt is enabled whenever there is data to send.
    // Without the critical section, the ISR can drain the last byte and
    // clear txActive_ between our buffer fill and this check -- leaving
    // new data stranded in the buffer with TXE disabled (permanent stall).
    // The critical section is 3-4 instructions (~50ns at 80 MHz).
    if (written > 0) {
      __disable_irq();
      if (!txActive_) {
        txActive_ = true;
        __HAL_UART_ENABLE_IT(&huart_, UART_IT_TXE);
      }
      __enable_irq();
    }
#endif

    return written;
  }

  size_t read(uint8_t* buffer, size_t maxLen) noexcept override {
    if (!initialized_ || buffer == nullptr || maxLen == 0) {
      return 0;
    }

    size_t bytesRead = 0;

    while (bytesRead < maxLen && rxTail_ != rxHead_) {
      buffer[bytesRead] = rxBuf_[rxTail_];
      rxTail_ = (rxTail_ + 1) % RxBufSize;
      ++bytesRead;
    }

    return bytesRead;
  }

  [[nodiscard]] size_t available() const noexcept override {
    if (rxHead_ >= rxTail_) {
      return rxHead_ - rxTail_;
    }
    return RxBufSize - rxTail_ + rxHead_;
  }

  [[nodiscard]] bool txReady() const noexcept override {
    size_t nextHead = (txHead_ + 1) % TxBufSize;
    return nextHead != txTail_;
  }

  [[nodiscard]] bool txComplete() const noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    return !txActive_ && (txHead_ == txTail_) &&
           (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_TC) != RESET);
#else
    return !txActive_ && (txHead_ == txTail_);
#endif
  }

  void flushRx() noexcept override {
    rxHead_ = 0;
    rxTail_ = 0;
  }

  void flushTx() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    while (!txComplete()) {
      // Spin wait - NOT RT-safe
    }
#endif
  }

  [[nodiscard]] const UartStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

  /* ----------------------------- IRQ Handler ----------------------------- */

  /**
   * @brief Handle UART interrupt.
   *
   * Call this from the USARTx_IRQHandler function.
   * Handles RX, TX, and error interrupts.
   *
   * @code
   * extern "C" void USART2_IRQHandler() {
   *   uart.irqHandler();
   * }
   * @endcode
   *
   * @note Must be called from interrupt context.
   */
  void irqHandler() noexcept {
#ifndef APEX_HAL_STM32_MOCK
    // Check for RX data
    if (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_RXNE) != RESET) {
      uint8_t data = static_cast<uint8_t>(huart_.Instance->RDR & 0xFF);
      size_t nextHead = (rxHead_ + 1) % RxBufSize;

      if (nextHead != rxTail_) {
        rxBuf_[rxHead_] = data;
        rxHead_ = nextHead;
        ++stats_.bytesRx;
      } else {
        // Buffer overrun - data lost
        ++stats_.overrunErrors;
      }
    }

    // Check for TX ready -- only when TXE interrupt is enabled.
    // Without the IT_SOURCE check, RX interrupts (RXNE) also enter this
    // block because TXE flag is high whenever the transmit register is
    // idle, causing spurious disable of TXE and clearing txActive_.
    if (__HAL_UART_GET_IT_SOURCE(&huart_, UART_IT_TXE) != RESET &&
        __HAL_UART_GET_FLAG(&huart_, UART_FLAG_TXE) != RESET) {
      if (txTail_ != txHead_) {
        huart_.Instance->TDR = txBuf_[txTail_];
        txTail_ = (txTail_ + 1) % TxBufSize;
      } else {
        // TX buffer empty, disable TXE interrupt
        __HAL_UART_DISABLE_IT(&huart_, UART_IT_TXE);
        txActive_ = false;
      }
    }

    // Check for errors
    if (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_ORE) != RESET) {
      __HAL_UART_CLEAR_FLAG(&huart_, UART_CLEAR_OREF);
      ++stats_.overrunErrors;
    }
    if (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_FE) != RESET) {
      __HAL_UART_CLEAR_FLAG(&huart_, UART_CLEAR_FEF);
      ++stats_.framingErrors;
    }
    if (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_PE) != RESET) {
      __HAL_UART_CLEAR_FLAG(&huart_, UART_CLEAR_PEF);
      ++stats_.parityErrors;
    }
    if (__HAL_UART_GET_FLAG(&huart_, UART_FLAG_NE) != RESET) {
      __HAL_UART_CLEAR_FLAG(&huart_, UART_CLEAR_NEF);
      ++stats_.noiseErrors;
    }
#endif
  }

  /* ----------------------------- Buffer Info ----------------------------- */

  /**
   * @brief Get RX buffer capacity.
   * @return RxBufSize - 1 (one slot reserved for full detection).
   */
  [[nodiscard]] static constexpr size_t rxCapacity() noexcept { return RxBufSize - 1; }

  /**
   * @brief Get TX buffer capacity.
   * @return TxBufSize - 1 (one slot reserved for full detection).
   */
  [[nodiscard]] static constexpr size_t txCapacity() noexcept { return TxBufSize - 1; }

private:
#ifndef APEX_HAL_STM32_MOCK
  /* ----------------------------- Platform Helpers ----------------------------- */

  /**
   * @brief Enable the RCC clock for this USART peripheral.
   * @note NOT RT-safe: Called once during init().
   */
  void enablePeripheralClock() noexcept {
    if (huart_.Instance == USART1) {
      __HAL_RCC_USART1_CLK_ENABLE();
    } else if (huart_.Instance == USART2) {
      __HAL_RCC_USART2_CLK_ENABLE();
    }
#if defined(USART3)
    else if (huart_.Instance == USART3) {
      __HAL_RCC_USART3_CLK_ENABLE();
    }
#endif
#if defined(UART4)
    else if (huart_.Instance == UART4) {
      __HAL_RCC_UART4_CLK_ENABLE();
    }
#endif
#if defined(UART5)
    else if (huart_.Instance == UART5) {
      __HAL_RCC_UART5_CLK_ENABLE();
    }
#endif
#if defined(LPUART1)
    else if (huart_.Instance == LPUART1) {
      __HAL_RCC_LPUART1_CLK_ENABLE();
    }
#endif
  }

  /**
   * @brief Enable the RCC clock for a GPIO port.
   * @param port GPIO port (e.g., GPIOA).
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

  /**
   * @brief Get the NVIC IRQ number for this USART peripheral.
   * @return IRQn_Type for NVIC configuration.
   * @note RT-safe: Deterministic lookup.
   */
  [[nodiscard]] IRQn_Type irqn() const noexcept {
    if (huart_.Instance == USART1) {
      return USART1_IRQn;
    }
    if (huart_.Instance == USART2) {
      return USART2_IRQn;
    }
#if defined(USART3)
    if (huart_.Instance == USART3) {
      return USART3_IRQn;
    }
#endif
#if defined(UART4)
    if (huart_.Instance == UART4) {
      return UART4_IRQn;
    }
#endif
#if defined(UART5)
    if (huart_.Instance == UART5) {
      return UART5_IRQn;
    }
#endif
#if defined(LPUART1)
    if (huart_.Instance == LPUART1) {
      return LPUART1_IRQn;
    }
#endif
    return UsageFault_IRQn;
  }

  UART_HandleTypeDef huart_ = {};
  Stm32UartPins pins_ = {};
#endif

  bool initialized_ = false;
  volatile bool txActive_ = false;

  // RX circular buffer
  uint8_t rxBuf_[RxBufSize] = {};
  volatile size_t rxHead_ = 0;
  volatile size_t rxTail_ = 0;

  // TX circular buffer
  uint8_t txBuf_[TxBufSize] = {};
  volatile size_t txHead_ = 0;
  volatile size_t txTail_ = 0;

  UartStats stats_ = {};
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_UART_HPP
