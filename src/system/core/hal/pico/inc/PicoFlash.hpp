#ifndef APEX_HAL_PICO_FLASH_HPP
#define APEX_HAL_PICO_FLASH_HPP
/**
 * @file PicoFlash.hpp
 * @brief RP2040 external QSPI flash implementation using Pico SDK.
 *
 * Wraps the Pico SDK flash functions for the 2 MB external QSPI flash.
 * Flash is memory-mapped via XIP at 0x10000000. Read access is instant
 * (memory-mapped). Write and erase require SDK functions that disable
 * XIP and execute from SRAM.
 *
 * Geometry:
 *  - Base address: 0x10000000 (XIP_BASE)
 *  - Total size:   2 MB (2,097,152 bytes)
 *  - Sector erase: 4 KB (FLASH_SECTOR_SIZE)
 *  - Write page:   256 bytes (FLASH_PAGE_SIZE)
 *  - Page count:   512 (2 MB / 4 KB)
 *
 * @note Write/erase are NOT RT-safe: XIP is disabled during programming.
 * @note Read is RT-safe: Direct memory-mapped access.
 */

#include "src/system/core/hal/base/IFlash.hpp"

#ifndef APEX_HAL_PICO_MOCK
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#endif

#include <string.h>

namespace apex {
namespace hal {
namespace pico {

/* ----------------------------- Constants ----------------------------- */

/// XIP base address for flash memory.
static constexpr uint32_t PICO_FLASH_BASE = 0x10000000;

/// Total flash size (2 MB).
static constexpr uint32_t PICO_FLASH_SIZE = 2 * 1024 * 1024;

/// Sector erase size (4 KB).
static constexpr uint32_t PICO_SECTOR_SIZE =
#ifndef APEX_HAL_PICO_MOCK
    FLASH_SECTOR_SIZE;
#else
    4096;
#endif

/// Write page size (256 bytes).
static constexpr uint32_t PICO_PAGE_SIZE =
#ifndef APEX_HAL_PICO_MOCK
    FLASH_PAGE_SIZE;
#else
    256;
#endif

/// Number of sectors (pages in our abstraction).
static constexpr uint16_t PICO_PAGE_COUNT = PICO_FLASH_SIZE / PICO_SECTOR_SIZE;

/* ----------------------------- PicoFlash ----------------------------- */

/**
 * @class PicoFlash
 * @brief RP2040 external QSPI flash via XIP.
 *
 * Implements the IFlash interface using the Pico SDK flash functions.
 * "Page" in this interface maps to a 4 KB flash sector (the minimum
 * erase unit). Write alignment is 256 bytes (flash page program).
 *
 * @note NOT thread-safe. Single-core usage only (core 1 must be idle
 *       during flash write/erase because XIP is disabled).
 */
class PicoFlash final : public IFlash {
public:
  PicoFlash() noexcept = default;
  ~PicoFlash() override { deinit(); }

  PicoFlash(const PicoFlash&) = delete;
  PicoFlash& operator=(const PicoFlash&) = delete;
  PicoFlash(PicoFlash&&) = delete;
  PicoFlash& operator=(PicoFlash&&) = delete;

  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] FlashStatus init() noexcept override {
#ifdef APEX_HAL_PICO_MOCK
    memset(mockFlash_, 0xFF, PICO_FLASH_SIZE);
#endif
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
    if (address < PICO_FLASH_BASE || (address + len) > (PICO_FLASH_BASE + PICO_FLASH_SIZE)) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_PICO_MOCK
    // Memory-mapped read via XIP
    memcpy(data, reinterpret_cast<const void*>(address), len);
#else
    const uint32_t OFFSET = address - PICO_FLASH_BASE;
    memcpy(data, &mockFlash_[OFFSET], len);
#endif

    stats_.bytesRead += len;
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
    if (address < PICO_FLASH_BASE || (address + len) > (PICO_FLASH_BASE + PICO_FLASH_SIZE)) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    // Convert XIP address to flash offset
    const uint32_t OFFSET = address - PICO_FLASH_BASE;

#ifndef APEX_HAL_PICO_MOCK
    // Pico SDK requires 256-byte aligned writes. For smaller writes,
    // we pad to 256 bytes using a stack buffer.
    if (len <= PICO_PAGE_SIZE) {
      uint8_t pageBuf[PICO_PAGE_SIZE];
      memset(pageBuf, 0xFF, PICO_PAGE_SIZE);
      memcpy(pageBuf, data, len);

      const uint32_t ALIGNED_OFFSET = OFFSET & ~(PICO_PAGE_SIZE - 1);
      const uint32_t INTRA_OFFSET = OFFSET - ALIGNED_OFFSET;

      // If the write doesn't start at a page boundary, read existing data first
      if (INTRA_OFFSET > 0) {
        const uint8_t* existing =
            reinterpret_cast<const uint8_t*>(PICO_FLASH_BASE + ALIGNED_OFFSET);
        uint8_t fullPage[PICO_PAGE_SIZE];
        memcpy(fullPage, existing, PICO_PAGE_SIZE);
        memcpy(fullPage + INTRA_OFFSET, data, len);

        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(ALIGNED_OFFSET, fullPage, PICO_PAGE_SIZE);
        restore_interrupts(ints);
      } else {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(ALIGNED_OFFSET, pageBuf, PICO_PAGE_SIZE);
        restore_interrupts(ints);
      }
    } else {
      // Multi-page write: must be 256-byte aligned
      if ((OFFSET % PICO_PAGE_SIZE) != 0) {
        ++stats_.writeErrors;
        return FlashStatus::ERROR_ALIGNMENT;
      }

      // Round up to page boundary
      const size_t ALIGNED_LEN = ((len + PICO_PAGE_SIZE - 1) / PICO_PAGE_SIZE) * PICO_PAGE_SIZE;
      // For large writes, we need the data to be page-aligned in length
      // The SDK handles partial last pages internally
      uint32_t ints = save_and_disable_interrupts();
      flash_range_program(OFFSET, data, ALIGNED_LEN);
      restore_interrupts(ints);
    }
#else
    // Mock: write directly to shadow buffer
    memcpy(&mockFlash_[OFFSET], data, len);
#endif

    stats_.bytesWritten += len;
    return FlashStatus::OK;
  }

  [[nodiscard]] FlashStatus erasePage(uint32_t pageIndex) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (pageIndex >= PICO_PAGE_COUNT) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    const uint32_t OFFSET = pageIndex * PICO_SECTOR_SIZE;

#ifndef APEX_HAL_PICO_MOCK
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OFFSET, PICO_SECTOR_SIZE);
    restore_interrupts(ints);
#else
    memset(&mockFlash_[OFFSET], 0xFF, PICO_SECTOR_SIZE);
#endif

    ++stats_.pagesErased;
    return FlashStatus::OK;
  }

  [[nodiscard]] FlashStatus erasePages(uint32_t startPage, uint32_t count) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (count == 0 || (startPage + count) > PICO_PAGE_COUNT) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    const uint32_t OFFSET = startPage * PICO_SECTOR_SIZE;
    const uint32_t TOTAL = count * PICO_SECTOR_SIZE;

#ifndef APEX_HAL_PICO_MOCK
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(OFFSET, TOTAL);
    restore_interrupts(ints);
#else
    memset(&mockFlash_[OFFSET], 0xFF, TOTAL);
#endif

    stats_.pagesErased += count;
    return FlashStatus::OK;
  }

  /* ----------------------------- Geometry ----------------------------- */

  [[nodiscard]] FlashGeometry geometry() const noexcept override {
    FlashGeometry geo;
    geo.baseAddress = PICO_FLASH_BASE;
    geo.totalSize = PICO_FLASH_SIZE;
    geo.pageSize = PICO_SECTOR_SIZE;
    geo.writeAlignment = PICO_PAGE_SIZE;
    geo.pageCount = PICO_PAGE_COUNT;
    geo.bankCount = 1;
    return geo;
  }

  [[nodiscard]] uint32_t pageForAddress(uint32_t address) const noexcept override {
    if (address < PICO_FLASH_BASE) {
      return 0;
    }
    return (address - PICO_FLASH_BASE) / PICO_SECTOR_SIZE;
  }

  [[nodiscard]] uint32_t addressForPage(uint32_t pageIndex) const noexcept override {
    return PICO_FLASH_BASE + (pageIndex * PICO_SECTOR_SIZE);
  }

  /* ----------------------------- Status ----------------------------- */

  [[nodiscard]] bool isBusy() const noexcept override { return false; }

  [[nodiscard]] const FlashStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
  bool initialized_ = false;
  FlashStats stats_ = {};

#ifdef APEX_HAL_PICO_MOCK
  /* ----------------------------- Mock Storage ----------------------------- */

  /// Shadow buffer simulating 2 MB QSPI flash.
  uint8_t mockFlash_[PICO_FLASH_SIZE] = {};
#endif
};

} // namespace pico
} // namespace hal
} // namespace apex

#endif // APEX_HAL_PICO_FLASH_HPP
