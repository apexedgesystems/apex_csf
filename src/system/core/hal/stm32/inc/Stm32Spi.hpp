#ifndef APEX_HAL_STM32_SPI_HPP
#define APEX_HAL_STM32_SPI_HPP
/**
 * @file Stm32Spi.hpp
 * @brief STM32 SPI master implementation using HAL drivers (polling mode).
 *
 * Provides blocking (polling) SPI master transfers with software chip select.
 * No internal buffers or interrupts -- the user provides TX/RX buffers per
 * transfer call.
 *
 * Supported families:
 *  - STM32L4 (e.g., STM32L476xx)
 *  - STM32F1, STM32F4 (e.g., STM32F103xx, STM32F446xx)
 *  - STM32G4 (e.g., STM32G474xx)
 *
 * NOT supported:
 *  - STM32H7 (different SPI register model with FIFO thresholds)
 *
 * Features:
 *  - Polling (blocking) transfers -- deterministic, no ISR needed
 *  - Full-duplex, TX-only, and RX-only transfer modes
 *  - Software chip select (CS managed via GPIO, not hardware NSS)
 *  - All four SPI clock modes (MODE_0 through MODE_3)
 *  - Configurable clock frequency via prescaler selection
 *  - GPIO, peripheral clock setup handled internally
 *
 * Usage:
 *  1. Define board pin mapping (or use a board header)
 *  2. Create instance with SPI peripheral and pin descriptor
 *  3. Call init() with config (GPIO and clocks configured automatically)
 *  4. Use transfer()/write()/read() for data exchange
 *
 * @code
 * // Board pin descriptor
 * const Stm32SpiPins SPI1_BUS = {
 *   GPIOA, GPIO_PIN_5,  // CLK (PA5)
 *   GPIOA, GPIO_PIN_7,  // MOSI (PA7)
 *   GPIOA, GPIO_PIN_6,  // MISO (PA6)
 *   GPIOB, GPIO_PIN_6,  // CS (PB6, software GPIO)
 *   GPIO_AF5_SPI1
 * };
 *
 * // In main.cpp
 * static Stm32Spi spi(SPI1, SPI1_BUS);
 *
 * // In init
 * apex::hal::SpiConfig cfg;
 * cfg.maxClockHz = 1000000;
 * spi.init(cfg);
 *
 * // Transfer
 * uint8_t tx[4] = {0x9F, 0, 0, 0};  // JEDEC ID command
 * uint8_t rx[4] = {};
 * spi.transfer(tx, rx, 4);
 * @endcode
 */

#include "src/system/core/hal/base/ISpi.hpp"

// STM32 HAL includes (same family detection pattern as Stm32Uart/Stm32Can)
#if defined(STM32H7xx) || defined(STM32H743xx)
#error "Stm32Spi: H7 SPI has different register model (not supported by this wrapper)."
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

/* ----------------------------- Stm32SpiPins ----------------------------- */

#ifndef APEX_HAL_STM32_MOCK
/**
 * @brief GPIO pin descriptor for STM32 SPI.
 *
 * Captures the CLK/MOSI/MISO/CS pin mapping for SPI on a specific board.
 * CS is managed as a software GPIO output (not hardware NSS).
 * Define these in a board-specific header (e.g., NucleoL476rgPins.hpp).
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32SpiPins {
  GPIO_TypeDef* clkPort;  ///< SCK GPIO port (e.g., GPIOA).
  uint16_t clkPin;        ///< SCK GPIO pin mask (e.g., GPIO_PIN_5).
  GPIO_TypeDef* mosiPort; ///< MOSI GPIO port (e.g., GPIOA).
  uint16_t mosiPin;       ///< MOSI GPIO pin mask (e.g., GPIO_PIN_7).
  GPIO_TypeDef* misoPort; ///< MISO GPIO port (e.g., GPIOA).
  uint16_t misoPin;       ///< MISO GPIO pin mask (e.g., GPIO_PIN_6).
  GPIO_TypeDef* csPort;   ///< CS GPIO port (e.g., GPIOB). Software NSS.
  uint16_t csPin;         ///< CS GPIO pin mask (e.g., GPIO_PIN_6).
  uint8_t alternate;      ///< Alternate function (e.g., GPIO_AF5_SPI1).
};
#endif

/* ----------------------------- Stm32SpiOptions ----------------------------- */

/**
 * @brief Platform-specific options for STM32 SPI initialization.
 *
 * Controls the polling timeout for blocking transfers.
 *
 * @note NOT RT-safe: Used only during init() and transfer().
 */
struct Stm32SpiOptions {
  uint32_t timeoutMs = 1000; ///< Polling timeout per transfer in milliseconds.
};

/* ----------------------------- Stm32Spi ----------------------------- */

/**
 * @class Stm32Spi
 * @brief STM32 SPI master implementation (polling mode, software CS).
 *
 * Not templated -- no internal buffers needed for polling transfers.
 *
 * Memory usage: ~120 bytes (SPI_HandleTypeDef + pin descriptor + options +
 * stats + flags).
 *
 * Example instantiation:
 * @code
 * const Stm32SpiPins BUS = {GPIOA, GPIO_PIN_5, GPIOA, GPIO_PIN_7,
 *                           GPIOA, GPIO_PIN_6, GPIOB, GPIO_PIN_6,
 *                           GPIO_AF5_SPI1};
 * Stm32Spi spi(SPI1, BUS);
 * @endcode
 */
class Stm32Spi final : public ISpi {
public:
#ifndef APEX_HAL_STM32_MOCK
  /**
   * @brief Construct SPI wrapper for a specific SPI peripheral.
   * @param instance SPI peripheral (e.g., SPI1, SPI2, SPI3).
   * @param pins GPIO pin descriptor for this SPI on this board.
   * @note NOT RT-safe: Construction only.
   */
  Stm32Spi(SPI_TypeDef* instance, const Stm32SpiPins& pins) noexcept : pins_(pins) {
    hspi_.Instance = instance;
  }
#else
  /**
   * @brief Mock constructor for host-side testing.
   */
  Stm32Spi() noexcept = default;
#endif

  ~Stm32Spi() override { deinit(); }

  // Non-copyable, non-movable (tied to hardware peripheral)
  Stm32Spi(const Stm32Spi&) = delete;
  Stm32Spi& operator=(const Stm32Spi&) = delete;
  Stm32Spi(Stm32Spi&&) = delete;
  Stm32Spi& operator=(Stm32Spi&&) = delete;

  /* ----------------------------- ISpi Interface ----------------------------- */

  /**
   * @brief Initialize SPI with default options.
   * @param config SPI parameters (clock, mode, bit order, data size).
   * @return SpiStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, and SPI registers.
   */
  [[nodiscard]] SpiStatus init(const SpiConfig& config) noexcept override {
    return init(config, Stm32SpiOptions{});
  }

  /**
   * @brief Initialize SPI with explicit options.
   * @param config SPI parameters (clock, mode, bit order, data size).
   * @param opts Platform-specific options (timeout).
   * @return SpiStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, and SPI registers.
   */
  [[nodiscard]] SpiStatus init(const SpiConfig& config, const Stm32SpiOptions& opts) noexcept {
    // Double-init guard: clean up before reinitializing
    if (initialized_) {
      deinit();
    }

    options_ = opts;

#ifndef APEX_HAL_STM32_MOCK
    // Enable SPI peripheral clock
    enablePeripheralClock();

    // Enable GPIO clocks for all pins
    enableGpioClock(pins_.clkPort);
    if (pins_.mosiPort != pins_.clkPort) {
      enableGpioClock(pins_.mosiPort);
    }
    if (pins_.misoPort != pins_.clkPort && pins_.misoPort != pins_.mosiPort) {
      enableGpioClock(pins_.misoPort);
    }
    if (pins_.csPort != pins_.clkPort && pins_.csPort != pins_.mosiPort &&
        pins_.csPort != pins_.misoPort) {
      enableGpioClock(pins_.csPort);
    }

    // Configure CLK pin (AF push-pull)
    GPIO_InitTypeDef gpioInit = {};
    gpioInit.Pin = pins_.clkPin;
    gpioInit.Mode = GPIO_MODE_AF_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = pins_.alternate;
    HAL_GPIO_Init(pins_.clkPort, &gpioInit);

    // Configure MOSI pin (AF push-pull)
    gpioInit.Pin = pins_.mosiPin;
    HAL_GPIO_Init(pins_.mosiPort, &gpioInit);

    // Configure MISO pin (AF push-pull)
    gpioInit.Pin = pins_.misoPin;
    HAL_GPIO_Init(pins_.misoPort, &gpioInit);

    // Configure CS pin (GPIO output push-pull, deasserted HIGH)
    gpioInit.Pin = pins_.csPin;
    gpioInit.Mode = GPIO_MODE_OUTPUT_PP;
    gpioInit.Pull = GPIO_NOPULL;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = 0;
    HAL_GPIO_Init(pins_.csPort, &gpioInit);
    HAL_GPIO_WritePin(pins_.csPort, pins_.csPin, GPIO_PIN_SET); // Deassert CS

    // Configure SPI peripheral
    hspi_.Init.Mode = SPI_MODE_MASTER;
    hspi_.Init.Direction = SPI_DIRECTION_2LINES;
    hspi_.Init.NSS = SPI_NSS_SOFT;
    hspi_.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi_.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi_.Init.CRCPolynomial = 7;

    // Data size
    hspi_.Init.DataSize =
        (config.dataSize == SpiDataSize::BITS_16) ? SPI_DATASIZE_16BIT : SPI_DATASIZE_8BIT;

    // Clock polarity and phase from SpiMode
    switch (config.mode) {
    case SpiMode::MODE_0:
      hspi_.Init.CLKPolarity = SPI_POLARITY_LOW;
      hspi_.Init.CLKPhase = SPI_PHASE_1EDGE;
      break;
    case SpiMode::MODE_1:
      hspi_.Init.CLKPolarity = SPI_POLARITY_LOW;
      hspi_.Init.CLKPhase = SPI_PHASE_2EDGE;
      break;
    case SpiMode::MODE_2:
      hspi_.Init.CLKPolarity = SPI_POLARITY_HIGH;
      hspi_.Init.CLKPhase = SPI_PHASE_1EDGE;
      break;
    case SpiMode::MODE_3:
      hspi_.Init.CLKPolarity = SPI_POLARITY_HIGH;
      hspi_.Init.CLKPhase = SPI_PHASE_2EDGE;
      break;
    }

    // Bit order
    hspi_.Init.FirstBit =
        (config.bitOrder == SpiBitOrder::LSB_FIRST) ? SPI_FIRSTBIT_LSB : SPI_FIRSTBIT_MSB;

    // Baud rate prescaler
    hspi_.Init.BaudRatePrescaler = computePrescaler(config.maxClockHz);

    if (HAL_SPI_Init(&hspi_) != HAL_OK) {
      return SpiStatus::ERROR_INVALID_ARG;
    }

    initialized_ = true;
    return SpiStatus::OK;
#else
    (void)config;
    initialized_ = true;
    return SpiStatus::OK;
#endif
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (initialized_) {
      HAL_SPI_DeInit(&hspi_);
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] SpiStatus transfer(const uint8_t* txData, uint8_t* rxData,
                                   size_t len) noexcept override {
    if (!initialized_) {
      return SpiStatus::ERROR_NOT_INIT;
    }
    if (txData == nullptr || rxData == nullptr || len == 0) {
      return SpiStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    csAssert();
    HAL_StatusTypeDef halStatus =
        HAL_SPI_TransmitReceive(&hspi_, const_cast<uint8_t*>(txData), rxData,
                                static_cast<uint16_t>(len), options_.timeoutMs);
    csDeassert();

    if (halStatus == HAL_TIMEOUT) {
      return SpiStatus::ERROR_TIMEOUT;
    }
    if (halStatus != HAL_OK) {
      return mapHalError();
    }
#else
    // Mock: fill rxData with dummy pattern (echo TX for loopback testing)
    for (size_t i = 0; i < len; ++i) {
      rxData[i] = txData[i];
    }
#endif

    stats_.bytesTransferred += static_cast<uint32_t>(len);
    ++stats_.transferCount;
    return SpiStatus::OK;
  }

  [[nodiscard]] SpiStatus write(const uint8_t* data, size_t len) noexcept override {
    if (!initialized_) {
      return SpiStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return SpiStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    csAssert();
    HAL_StatusTypeDef halStatus = HAL_SPI_Transmit(&hspi_, const_cast<uint8_t*>(data),
                                                   static_cast<uint16_t>(len), options_.timeoutMs);
    csDeassert();

    if (halStatus == HAL_TIMEOUT) {
      return SpiStatus::ERROR_TIMEOUT;
    }
    if (halStatus != HAL_OK) {
      return mapHalError();
    }
#else
    (void)data;
#endif

    stats_.bytesTransferred += static_cast<uint32_t>(len);
    ++stats_.transferCount;
    return SpiStatus::OK;
  }

  [[nodiscard]] SpiStatus read(uint8_t* data, size_t len) noexcept override {
    if (!initialized_) {
      return SpiStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return SpiStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    csAssert();
    HAL_StatusTypeDef halStatus =
        HAL_SPI_Receive(&hspi_, data, static_cast<uint16_t>(len), options_.timeoutMs);
    csDeassert();

    if (halStatus == HAL_TIMEOUT) {
      return SpiStatus::ERROR_TIMEOUT;
    }
    if (halStatus != HAL_OK) {
      return mapHalError();
    }
#else
    // Mock: fill with 0xFF (as if no slave responding)
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0xFF;
    }
#endif

    stats_.bytesTransferred += static_cast<uint32_t>(len);
    ++stats_.transferCount;
    return SpiStatus::OK;
  }

  [[nodiscard]] bool isBusy() const noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (!initialized_) {
      return false;
    }
    return HAL_SPI_GetState(&hspi_) == HAL_SPI_STATE_BUSY;
#else
    return false;
#endif
  }

  [[nodiscard]] const SpiStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
#ifndef APEX_HAL_STM32_MOCK
  /* ----------------------------- Platform Helpers ----------------------------- */

  /**
   * @brief Assert chip select (drive CS low).
   * @note RT-safe: GPIO write.
   */
  void csAssert() noexcept { HAL_GPIO_WritePin(pins_.csPort, pins_.csPin, GPIO_PIN_RESET); }

  /**
   * @brief Deassert chip select (drive CS high).
   * @note RT-safe: GPIO write.
   */
  void csDeassert() noexcept { HAL_GPIO_WritePin(pins_.csPort, pins_.csPin, GPIO_PIN_SET); }

  /**
   * @brief Map HAL SPI error flags to SpiStatus.
   * @return SpiStatus corresponding to current HAL error.
   */
  SpiStatus mapHalError() noexcept {
    uint32_t err = HAL_SPI_GetError(&hspi_);
    if (err & HAL_SPI_ERROR_OVR) {
      ++stats_.overrunErrors;
      return SpiStatus::ERROR_OVERRUN;
    }
    if (err & HAL_SPI_ERROR_CRC) {
      ++stats_.crcErrors;
      return SpiStatus::ERROR_CRC;
    }
    if (err & HAL_SPI_ERROR_MODF) {
      ++stats_.modfErrors;
      return SpiStatus::ERROR_MODF;
    }
    return SpiStatus::ERROR_TIMEOUT;
  }

  /**
   * @brief Compute SPI baud rate prescaler for a given max clock frequency.
   *
   * SPI1 is on APB2 (80 MHz on STM32L4), SPI2/SPI3 on APB1 (80 MHz).
   * Both APB buses run at 80 MHz in the default Nucleo configuration.
   *
   * Picks the smallest prescaler such that (APB_CLK / prescaler) <= maxClockHz.
   *
   * @param maxClockHz Maximum desired SCK frequency in Hz.
   * @return HAL prescaler constant (SPI_BAUDRATEPRESCALER_*).
   * @note NOT RT-safe: Called once during init().
   */
  static uint32_t computePrescaler(uint32_t maxClockHz) noexcept {
    // Prescaler values and their divisors
    static constexpr struct {
      uint32_t divisor;
      uint32_t halValue;
    } PRESCALERS[] = {
        {2, SPI_BAUDRATEPRESCALER_2},     {4, SPI_BAUDRATEPRESCALER_4},
        {8, SPI_BAUDRATEPRESCALER_8},     {16, SPI_BAUDRATEPRESCALER_16},
        {32, SPI_BAUDRATEPRESCALER_32},   {64, SPI_BAUDRATEPRESCALER_64},
        {128, SPI_BAUDRATEPRESCALER_128}, {256, SPI_BAUDRATEPRESCALER_256},
    };

    static constexpr uint32_t APB_CLK = 80000000U;

    for (const auto& p : PRESCALERS) {
      if ((APB_CLK / p.divisor) <= maxClockHz) {
        return p.halValue;
      }
    }

    // Slowest possible
    return SPI_BAUDRATEPRESCALER_256;
  }

  /**
   * @brief Enable the RCC clock for the SPI peripheral.
   * @note NOT RT-safe: Called once during init().
   */
  void enablePeripheralClock() noexcept {
    if (hspi_.Instance == SPI1) {
      __HAL_RCC_SPI1_CLK_ENABLE();
    }
#if defined(SPI2)
    else if (hspi_.Instance == SPI2) {
      __HAL_RCC_SPI2_CLK_ENABLE();
    }
#endif
#if defined(SPI3)
    else if (hspi_.Instance == SPI3) {
      __HAL_RCC_SPI3_CLK_ENABLE();
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

  SPI_HandleTypeDef hspi_ = {};
  Stm32SpiPins pins_ = {};
#endif

  Stm32SpiOptions options_ = {};
  bool initialized_ = false;
  SpiStats stats_ = {};
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_SPI_HPP
