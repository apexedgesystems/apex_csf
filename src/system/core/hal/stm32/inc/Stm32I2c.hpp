#ifndef APEX_HAL_STM32_I2C_HPP
#define APEX_HAL_STM32_I2C_HPP
/**
 * @file Stm32I2c.hpp
 * @brief STM32 I2C master implementation using HAL drivers (polling mode).
 *
 * Provides blocking (polling) I2C master transfers. No internal buffers or
 * interrupts -- the user provides TX/RX buffers per transfer call.
 *
 * Supported families (new I2C peripheral with TIMINGR register):
 *  - STM32L4 (e.g., STM32L476xx)
 *  - STM32G4 (e.g., STM32G474xx)
 *
 * NOT supported (legacy I2C with CCR register):
 *  - STM32F1, STM32F4
 *
 * NOT supported yet (new I2C, similar to L4):
 *  - STM32H7
 *
 * Features:
 *  - Polling (blocking) transfers -- deterministic, no ISR needed
 *  - Master write, master read, and combined write-then-read (register access)
 *  - All standard I2C speeds: 100 kHz, 400 kHz, 1 MHz
 *  - 7-bit and 10-bit addressing
 *  - GPIO, peripheral clock setup handled internally
 *
 * Usage:
 *  1. Define board pin mapping (or use a board header)
 *  2. Create instance with I2C peripheral and pin descriptor
 *  3. Call init() with config (GPIO and clocks configured automatically)
 *  4. Use write()/read()/writeRead() for data exchange
 *
 * @code
 * // Board pin descriptor
 * const Stm32I2cPins I2C1_BUS = {
 *   GPIOB, GPIO_PIN_8,  // SCL (PB8)
 *   GPIOB, GPIO_PIN_9,  // SDA (PB9)
 *   GPIO_AF4_I2C1
 * };
 *
 * // In main.cpp
 * static Stm32I2c i2c(I2C1, I2C1_BUS);
 *
 * // In init
 * apex::hal::I2cConfig cfg;
 * cfg.speed = apex::hal::I2cSpeed::FAST;
 * i2c.init(cfg);
 *
 * // Read register 0x75 (WHO_AM_I) from device at address 0x68
 * uint8_t regAddr = 0x75;
 * uint8_t whoAmI = 0;
 * i2c.writeRead(0x68, &regAddr, 1, &whoAmI, 1);
 * @endcode
 */

#include "src/system/core/hal/base/II2c.hpp"

// STM32 HAL includes (same family detection pattern as Stm32Uart/Stm32Can/Stm32Spi)
// Only new I2C families (TIMINGR register) are supported.
// F1/F4 use legacy I2C with CCR register and are not compatible.
#if defined(STM32F1) || defined(STM32F103xB) || defined(STM32F4xx) || defined(STM32F446xx) ||      \
    defined(STM32F401xC) || defined(STM32F411xE)
#error "Stm32I2c requires new I2C peripheral (TIMINGR). F1/F4 not supported."
#elif defined(STM32H7xx) || defined(STM32H743xx)
#error "Stm32I2c: H7 I2C not yet tested. Remove this guard to enable."
#elif defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#else
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32G4xx, etc."
#endif
#endif

namespace apex {
namespace hal {
namespace stm32 {

/* ----------------------------- Stm32I2cPins ----------------------------- */

#ifndef APEX_HAL_STM32_MOCK
/**
 * @brief GPIO pin descriptor for STM32 I2C.
 *
 * Captures the SCL/SDA pin mapping for I2C on a specific board.
 * I2C uses open-drain mode with internal or external pull-ups.
 * Define these in a board-specific header (e.g., NucleoL476rgPins.hpp).
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32I2cPins {
  GPIO_TypeDef* sclPort; ///< SCL GPIO port (e.g., GPIOB).
  uint16_t sclPin;       ///< SCL GPIO pin mask (e.g., GPIO_PIN_8).
  GPIO_TypeDef* sdaPort; ///< SDA GPIO port (e.g., GPIOB).
  uint16_t sdaPin;       ///< SDA GPIO pin mask (e.g., GPIO_PIN_9).
  uint8_t alternate;     ///< Alternate function (e.g., GPIO_AF4_I2C1).
};
#endif

/* ----------------------------- Stm32I2cOptions ----------------------------- */

/**
 * @brief Platform-specific options for STM32 I2C initialization.
 *
 * Controls the polling timeout for blocking transfers.
 *
 * @note NOT RT-safe: Used only during init() and transfer().
 */
struct Stm32I2cOptions {
  uint32_t timeoutMs = 1000; ///< Polling timeout per transfer in milliseconds.
};

/* ----------------------------- Stm32I2c ----------------------------- */

/**
 * @class Stm32I2c
 * @brief STM32 I2C master implementation (polling mode).
 *
 * Not templated -- no internal buffers needed for polling transfers.
 *
 * Memory usage: ~120 bytes (I2C_HandleTypeDef + pin descriptor + options +
 * stats + flags).
 *
 * Example instantiation:
 * @code
 * const Stm32I2cPins BUS = {GPIOB, GPIO_PIN_8, GPIOB, GPIO_PIN_9,
 *                           GPIO_AF4_I2C1};
 * Stm32I2c i2c(I2C1, BUS);
 * @endcode
 */
class Stm32I2c final : public II2c {
public:
#ifndef APEX_HAL_STM32_MOCK
  /**
   * @brief Construct I2C wrapper for a specific I2C peripheral.
   * @param instance I2C peripheral (e.g., I2C1, I2C2, I2C3).
   * @param pins GPIO pin descriptor for this I2C on this board.
   * @note NOT RT-safe: Construction only.
   */
  Stm32I2c(I2C_TypeDef* instance, const Stm32I2cPins& pins) noexcept : pins_(pins) {
    hi2c_.Instance = instance;
  }
#else
  /**
   * @brief Mock constructor for host-side testing.
   */
  Stm32I2c() noexcept = default;
#endif

  ~Stm32I2c() override { deinit(); }

  // Non-copyable, non-movable (tied to hardware peripheral)
  Stm32I2c(const Stm32I2c&) = delete;
  Stm32I2c& operator=(const Stm32I2c&) = delete;
  Stm32I2c(Stm32I2c&&) = delete;
  Stm32I2c& operator=(Stm32I2c&&) = delete;

  /* ----------------------------- II2c Interface ----------------------------- */

  /**
   * @brief Initialize I2C with default options.
   * @param config I2C parameters (speed, address mode).
   * @return I2cStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, and I2C registers.
   */
  [[nodiscard]] I2cStatus init(const I2cConfig& config) noexcept override {
    return init(config, Stm32I2cOptions{});
  }

  /**
   * @brief Initialize I2C with explicit options.
   * @param config I2C parameters (speed, address mode).
   * @param opts Platform-specific options (timeout).
   * @return I2cStatus::OK on success, error code on failure.
   * @note NOT RT-safe: Configures GPIO, peripheral clock, and I2C registers.
   */
  [[nodiscard]] I2cStatus init(const I2cConfig& config, const Stm32I2cOptions& opts) noexcept {
    // Double-init guard: clean up before reinitializing
    if (initialized_) {
      deinit();
    }

    options_ = opts;

#ifndef APEX_HAL_STM32_MOCK
    // Enable I2C peripheral clock
    enablePeripheralClock();

    // Enable GPIO clocks for SCL and SDA
    enableGpioClock(pins_.sclPort);
    if (pins_.sdaPort != pins_.sclPort) {
      enableGpioClock(pins_.sdaPort);
    }

    // Configure SCL pin (AF open-drain with pull-up)
    GPIO_InitTypeDef gpioInit = {};
    gpioInit.Pin = pins_.sclPin;
    gpioInit.Mode = GPIO_MODE_AF_OD;
    gpioInit.Pull = GPIO_PULLUP;
    gpioInit.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpioInit.Alternate = pins_.alternate;
    HAL_GPIO_Init(pins_.sclPort, &gpioInit);

    // Configure SDA pin (AF open-drain with pull-up)
    gpioInit.Pin = pins_.sdaPin;
    HAL_GPIO_Init(pins_.sdaPort, &gpioInit);

    // Configure I2C peripheral
    hi2c_.Init.Timing = computeTiming(config.speed);
    hi2c_.Init.OwnAddress1 = 0;
    hi2c_.Init.AddressingMode = (config.addressMode == I2cAddressMode::TEN_BIT)
                                    ? I2C_ADDRESSINGMODE_10BIT
                                    : I2C_ADDRESSINGMODE_7BIT;
    hi2c_.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c_.Init.OwnAddress2 = 0;
    hi2c_.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c_.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c_.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;

    if (HAL_I2C_Init(&hi2c_) != HAL_OK) {
      return I2cStatus::ERROR_INVALID_ARG;
    }

    initialized_ = true;
    return I2cStatus::OK;
#else
    (void)config;
    initialized_ = true;
    return I2cStatus::OK;
#endif
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (initialized_) {
      HAL_I2C_DeInit(&hi2c_);
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  [[nodiscard]] I2cStatus write(uint16_t addr, const uint8_t* data, size_t len) noexcept override {
    if (!initialized_) {
      return I2cStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return I2cStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    HAL_StatusTypeDef halStatus = HAL_I2C_Master_Transmit(
        &hi2c_, static_cast<uint16_t>(addr << 1), const_cast<uint8_t*>(data),
        static_cast<uint16_t>(len), options_.timeoutMs);

    if (halStatus == HAL_TIMEOUT) {
      ++stats_.timeoutErrors;
      return I2cStatus::ERROR_TIMEOUT;
    }
    if (halStatus != HAL_OK) {
      return mapHalError();
    }
#else
    (void)addr;
    (void)data;
#endif

    stats_.bytesTx += static_cast<uint32_t>(len);
    ++stats_.transferCount;
    return I2cStatus::OK;
  }

  [[nodiscard]] I2cStatus read(uint16_t addr, uint8_t* data, size_t len) noexcept override {
    if (!initialized_) {
      return I2cStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return I2cStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    HAL_StatusTypeDef halStatus =
        HAL_I2C_Master_Receive(&hi2c_, static_cast<uint16_t>(addr << 1), data,
                               static_cast<uint16_t>(len), options_.timeoutMs);

    if (halStatus == HAL_TIMEOUT) {
      ++stats_.timeoutErrors;
      return I2cStatus::ERROR_TIMEOUT;
    }
    if (halStatus != HAL_OK) {
      return mapHalError();
    }
#else
    (void)addr;
    // Mock: fill with 0xFF (as if no slave responding)
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0xFF;
    }
#endif

    stats_.bytesRx += static_cast<uint32_t>(len);
    ++stats_.transferCount;
    return I2cStatus::OK;
  }

  [[nodiscard]] I2cStatus writeRead(uint16_t addr, const uint8_t* txData, size_t txLen,
                                    uint8_t* rxData, size_t rxLen) noexcept override {
    if (!initialized_) {
      return I2cStatus::ERROR_NOT_INIT;
    }
    if (txData == nullptr || txLen == 0 || rxData == nullptr || rxLen == 0) {
      return I2cStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    // Use HAL_I2C_Mem_Read for 1-2 byte register addresses (common case).
    // This performs a combined write-then-read with repeated START.
    if (txLen <= 2) {
      uint16_t memAddr = txData[0];
      uint16_t memAddrSize = I2C_MEMADD_SIZE_8BIT;
      if (txLen == 2) {
        memAddr = static_cast<uint16_t>((txData[0] << 8) | txData[1]);
        memAddrSize = I2C_MEMADD_SIZE_16BIT;
      }

      HAL_StatusTypeDef halStatus =
          HAL_I2C_Mem_Read(&hi2c_, static_cast<uint16_t>(addr << 1), memAddr, memAddrSize, rxData,
                           static_cast<uint16_t>(rxLen), options_.timeoutMs);

      if (halStatus == HAL_TIMEOUT) {
        ++stats_.timeoutErrors;
        return I2cStatus::ERROR_TIMEOUT;
      }
      if (halStatus != HAL_OK) {
        return mapHalError();
      }
    } else {
      // For >2 byte prefix: two separate calls (rare case)
      HAL_StatusTypeDef halStatus = HAL_I2C_Master_Transmit(
          &hi2c_, static_cast<uint16_t>(addr << 1), const_cast<uint8_t*>(txData),
          static_cast<uint16_t>(txLen), options_.timeoutMs);

      if (halStatus == HAL_TIMEOUT) {
        ++stats_.timeoutErrors;
        return I2cStatus::ERROR_TIMEOUT;
      }
      if (halStatus != HAL_OK) {
        return mapHalError();
      }

      halStatus = HAL_I2C_Master_Receive(&hi2c_, static_cast<uint16_t>(addr << 1), rxData,
                                         static_cast<uint16_t>(rxLen), options_.timeoutMs);

      if (halStatus == HAL_TIMEOUT) {
        ++stats_.timeoutErrors;
        return I2cStatus::ERROR_TIMEOUT;
      }
      if (halStatus != HAL_OK) {
        return mapHalError();
      }
    }
#else
    (void)addr;
    (void)txData;
    // Mock: fill rxData with incrementing pattern (simulates register read)
    for (size_t i = 0; i < rxLen; ++i) {
      rxData[i] = static_cast<uint8_t>(i);
    }
#endif

    stats_.bytesTx += static_cast<uint32_t>(txLen);
    stats_.bytesRx += static_cast<uint32_t>(rxLen);
    ++stats_.transferCount;
    return I2cStatus::OK;
  }

  [[nodiscard]] bool isBusy() const noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (!initialized_) {
      return false;
    }
    return HAL_I2C_GetState(&hi2c_) == HAL_I2C_STATE_BUSY;
#else
    return false;
#endif
  }

  [[nodiscard]] const I2cStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
#ifndef APEX_HAL_STM32_MOCK
  /* ----------------------------- Platform Helpers ----------------------------- */

  /**
   * @brief Map HAL I2C error flags to I2cStatus.
   * @return I2cStatus corresponding to current HAL error.
   */
  I2cStatus mapHalError() noexcept {
    uint32_t err = HAL_I2C_GetError(&hi2c_);
    if (err & HAL_I2C_ERROR_AF) {
      ++stats_.nackErrors;
      return I2cStatus::ERROR_NACK;
    }
    if (err & HAL_I2C_ERROR_BERR) {
      ++stats_.busErrors;
      return I2cStatus::ERROR_BUS;
    }
    if (err & HAL_I2C_ERROR_ARLO) {
      ++stats_.arbitrationErrors;
      return I2cStatus::ERROR_ARBITRATION;
    }
    if (err & HAL_I2C_ERROR_TIMEOUT) {
      ++stats_.timeoutErrors;
      return I2cStatus::ERROR_TIMEOUT;
    }
    return I2cStatus::ERROR_BUS;
  }

  /**
   * @brief Compute I2C timing register value for a given speed.
   *
   * Pre-computed timing values for 80 MHz I2CCLK (STM32L476 default).
   * Values generated by STM32CubeMX / AN4235.
   *
   * @param speed Desired I2C bus speed.
   * @return TIMINGR register value.
   * @note NOT RT-safe: Called once during init().
   */
  static uint32_t computeTiming(I2cSpeed speed) noexcept {
    switch (speed) {
    case I2cSpeed::FAST:
      return 0x00702991U; // 400 kHz @ 80 MHz I2CCLK
    case I2cSpeed::FAST_PLUS:
      return 0x00300F33U; // 1 MHz @ 80 MHz I2CCLK
    default:
      return 0x10909CEC; // 100 kHz @ 80 MHz I2CCLK (standard mode)
    }
  }

  /**
   * @brief Enable the RCC clock for the I2C peripheral.
   * @note NOT RT-safe: Called once during init().
   */
  void enablePeripheralClock() noexcept {
    if (hi2c_.Instance == I2C1) {
      __HAL_RCC_I2C1_CLK_ENABLE();
    }
#if defined(I2C2)
    else if (hi2c_.Instance == I2C2) {
      __HAL_RCC_I2C2_CLK_ENABLE();
    }
#endif
#if defined(I2C3)
    else if (hi2c_.Instance == I2C3) {
      __HAL_RCC_I2C3_CLK_ENABLE();
    }
#endif
  }

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

  I2C_HandleTypeDef hi2c_ = {};
  Stm32I2cPins pins_ = {};
#endif

  Stm32I2cOptions options_ = {};
  bool initialized_ = false;
  I2cStats stats_ = {};
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_I2C_HPP
