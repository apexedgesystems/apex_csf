#ifndef APEX_HAL_AVR_EEPROM_HPP
#define APEX_HAL_AVR_EEPROM_HPP
/**
 * @file AvrEeprom.hpp
 * @brief ATmega328P EEPROM implementation of IFlash.
 *
 * Maps the ATmega328P's 1 KB EEPROM to the IFlash interface. EEPROM is
 * byte-granular (no page erase required), has 100,000 write endurance,
 * and is non-volatile across power cycles.
 *
 * The entire 1 KB is treated as a single "page" for the IFlash interface.
 * erasePage() writes 0xFF to all bytes (simulates erased state).
 * write() uses eeprom_update_block() which skips unchanged bytes to
 * reduce wear.
 *
 * FlashGeometry:
 *   baseAddress = 0
 *   totalSize = 1024
 *   pageSize = 1024
 *   writeAlignment = 1 (byte-granular)
 *   pageCount = 1
 *   bankCount = 1
 *
 * @note NOT RT-safe: EEPROM write takes ~3.3 ms per byte.
 * @note Read is instant (EEPROM is memory-mapped via avr/eeprom.h).
 */

#include "src/system/core/hal/base/IFlash.hpp"

#include <string.h>

#ifndef APEX_HAL_AVR_MOCK
#include <avr/eeprom.h>
#endif

namespace apex {
namespace hal {
namespace avr {

/* ----------------------------- Constants ----------------------------- */

#ifndef APEX_HAL_AVR_MOCK
static constexpr uint32_t AVR_EEPROM_SIZE = E2END + 1; // 1024 on ATmega328P
#else
static constexpr uint32_t AVR_EEPROM_SIZE = 1024;
#endif

/* ----------------------------- AvrEeprom ----------------------------- */

/**
 * @class AvrEeprom
 * @brief ATmega328P EEPROM as IFlash (1 KB, byte-granular writes).
 *
 * Memory usage: ~24 bytes (geometry + stats + flag) on real hardware.
 * Mock mode adds a 1 KB shadow buffer.
 */
class AvrEeprom final : public IFlash {
public:
  AvrEeprom() noexcept = default;
  ~AvrEeprom() override { deinit(); }

  AvrEeprom(const AvrEeprom&) = delete;
  AvrEeprom& operator=(const AvrEeprom&) = delete;
  AvrEeprom(AvrEeprom&&) = delete;
  AvrEeprom& operator=(AvrEeprom&&) = delete;

  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] FlashStatus init() noexcept override {
    if (initialized_) {
      deinit();
    }

    geometry_.baseAddress = 0;
    geometry_.totalSize = AVR_EEPROM_SIZE;
    geometry_.pageSize = AVR_EEPROM_SIZE; // Entire EEPROM as one page
    geometry_.writeAlignment = 1;         // Byte-granular
    geometry_.pageCount = 1;
    geometry_.bankCount = 1;

#ifdef APEX_HAL_AVR_MOCK
    // Initialize mock shadow buffer to erased state (0xFF)
    memset(mockEeprom_, 0xFF, AVR_EEPROM_SIZE);
#endif

    stats_.reset();
    initialized_ = true;
    return FlashStatus::OK;
  }

  void deinit() noexcept override { initialized_ = false; }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /* ----------------------------- Operations ----------------------------- */

  [[nodiscard]] FlashStatus read(uint32_t address, uint8_t* data, size_t len) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return FlashStatus::ERROR_INVALID_ARG;
    }
    if (!isAddressInRange(address, len)) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_AVR_MOCK
    eeprom_read_block(data, reinterpret_cast<const void*>(static_cast<uint16_t>(address)), len);
#else
    memcpy(data, &mockEeprom_[address], len);
#endif

    stats_.bytesRead += static_cast<uint32_t>(len);
    return FlashStatus::OK;
  }

  [[nodiscard]] FlashStatus write(uint32_t address, const uint8_t* data,
                                  size_t len) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return FlashStatus::ERROR_INVALID_ARG;
    }
    if (!isAddressInRange(address, len)) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_AVR_MOCK
    // eeprom_update_block skips bytes that already match, reducing wear
    eeprom_update_block(data, reinterpret_cast<void*>(static_cast<uint16_t>(address)), len);
#else
    memcpy(&mockEeprom_[address], data, len);
#endif

    stats_.bytesWritten += static_cast<uint32_t>(len);
    return FlashStatus::OK;
  }

  [[nodiscard]] FlashStatus erasePage(uint32_t pageIndex) noexcept override {
    return erasePages(pageIndex, 1);
  }

  [[nodiscard]] FlashStatus erasePages(uint32_t startPage, uint32_t count) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (count == 0 || (startPage + count) > geometry_.pageCount) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_AVR_MOCK
    // Write 0xFF to entire EEPROM (simulates flash erase)
    for (uint16_t i = 0; i < AVR_EEPROM_SIZE; ++i) {
      eeprom_update_byte(reinterpret_cast<uint8_t*>(i), 0xFF);
    }
#else
    memset(mockEeprom_, 0xFF, AVR_EEPROM_SIZE);
#endif

    stats_.pagesErased += count;
    return FlashStatus::OK;
  }

  /* ----------------------------- Geometry ----------------------------- */

  [[nodiscard]] FlashGeometry geometry() const noexcept override { return geometry_; }

  [[nodiscard]] uint32_t pageForAddress(uint32_t address) const noexcept override {
    (void)address;
    return 0; // Single page
  }

  [[nodiscard]] uint32_t addressForPage(uint32_t pageIndex) const noexcept override {
    (void)pageIndex;
    return 0; // Single page starts at 0
  }

  /* ----------------------------- Status ----------------------------- */

  [[nodiscard]] bool isBusy() const noexcept override {
#ifndef APEX_HAL_AVR_MOCK
    // Check EEPROM write in progress (EEPE bit)
    return (EECR & (1 << EEPE)) != 0;
#else
    return false;
#endif
  }

  [[nodiscard]] const FlashStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
  [[nodiscard]] bool isAddressInRange(uint32_t address, size_t len) const noexcept {
    return (address + len) <= AVR_EEPROM_SIZE;
  }

  FlashGeometry geometry_ = {};
  bool initialized_ = false;
  FlashStats stats_ = {};

#ifdef APEX_HAL_AVR_MOCK
  uint8_t mockEeprom_[AVR_EEPROM_SIZE] = {};
#endif
};

} // namespace avr
} // namespace hal
} // namespace apex

#endif // APEX_HAL_AVR_EEPROM_HPP
