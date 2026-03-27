#ifndef APEX_HAL_STM32_FLASH_HPP
#define APEX_HAL_STM32_FLASH_HPP
/**
 * @file Stm32Flash.hpp
 * @brief STM32 internal flash implementation.
 *
 * Provides page-based read/write/erase for STM32 internal flash memory.
 * No internal buffers, no interrupts -- all operations are blocking.
 *
 * Supported families (page-based erase, 2KB pages in dual-bank mode):
 *  - STM32L4 (e.g., STM32L476xx) -- 1MB dual-bank, 2KB pages
 *  - STM32G4 (e.g., STM32G474xx) -- 512KB dual-bank, 2KB pages
 *
 * NOT supported (sector-based erase with variable sector sizes):
 *  - STM32F4 (16KB-128KB sectors)
 *  - STM32H7 (128KB sectors)
 *  - STM32F1 (1KB pages, different register interface)
 *
 * Features:
 *  - Page-based erase (single page or contiguous range)
 *  - Double-word aligned writes (64-bit / 8-byte granularity)
 *  - Memory-mapped reads (instant, RT-safe)
 *  - Dual-bank geometry reported via geometry()
 *  - Flash lock/unlock managed internally
 *  - Statistics tracking (bytes written/read, pages erased, errors)
 *
 * Usage:
 *  1. Create instance (no pins or peripheral pointer needed)
 *  2. Call init() to unlock flash and populate geometry
 *  3. Call erasePage()/erasePages() before writing
 *  4. Call write() with 8-byte-aligned address
 *  5. Call read() to verify
 *  6. Call deinit() to lock flash
 *
 * @code
 * static Stm32Flash flash;
 *
 * // In init
 * flash.init();
 *
 * // Erase page 255 (last page of bank 1)
 * flash.erasePage(255);
 *
 * // Write 16 bytes at page start
 * uint8_t data[16] = {0x01, 0x02, ...};
 * flash.write(flash.addressForPage(255), data, 16);
 *
 * // Read back
 * uint8_t buf[16];
 * flash.read(flash.addressForPage(255), buf, 16);
 * @endcode
 */

#include "src/system/core/hal/base/IFlash.hpp"

#include <string.h> // memcpy, memset

// STM32 HAL includes -- only page-based flash families supported
#if defined(STM32F4xx) || defined(STM32F446xx) || defined(STM32F401xC) || defined(STM32F411xE)
#error "Stm32Flash: F4 uses sector-based erase (not supported by this wrapper)."
#elif defined(STM32H7xx) || defined(STM32H743xx)
#error "Stm32Flash: H7 uses sector-based erase (not supported by this wrapper)."
#elif defined(STM32F1) || defined(STM32F103xB)
#error "Stm32Flash: F1 has different flash register interface (not supported)."
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

/* ----------------------------- Stm32FlashOptions ----------------------------- */

/**
 * @brief Platform-specific options for STM32 flash operations.
 *
 * Controls the timeout for blocking erase/write operations. Flash erase
 * is the slowest operation (~25ms per page on STM32L4).
 *
 * @note NOT RT-safe: Used only during init().
 */
struct Stm32FlashOptions {
  uint32_t timeoutMs = 5000; ///< Timeout for erase/write operations in ms.
};

/* ----------------------------- Stm32Flash ----------------------------- */

/**
 * @class Stm32Flash
 * @brief STM32 internal flash implementation (blocking, page-based).
 *
 * Not templated -- no internal buffers needed. Flash is memory-mapped for
 * reads, HAL functions used for write/erase.
 *
 * Memory usage: ~40 bytes (geometry + options + stats + flags).
 *
 * Unlike communication peripherals (UART, SPI, CAN, I2C), flash requires
 * no GPIO pins, no peripheral clock enable, and no pin descriptor.
 */
class Stm32Flash final : public IFlash {
public:
  /**
   * @brief Construct flash interface.
   *
   * No peripheral pointer or pin descriptor needed -- internal flash
   * is always at a fixed address and requires no GPIO configuration.
   *
   * @note NOT RT-safe: Construction only.
   */
  Stm32Flash() noexcept = default;

  /**
   * @brief Destructor. Locks flash if still initialized.
   */
  ~Stm32Flash() override { deinit(); }

  Stm32Flash(const Stm32Flash&) = delete;
  Stm32Flash& operator=(const Stm32Flash&) = delete;
  Stm32Flash(Stm32Flash&&) = delete;
  Stm32Flash& operator=(Stm32Flash&&) = delete;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Initialize flash with default options.
   * @return FlashStatus::OK on success.
   * @note NOT RT-safe: Unlocks flash, populates geometry.
   */
  [[nodiscard]] FlashStatus init() noexcept override { return init(Stm32FlashOptions{}); }

  /**
   * @brief Initialize flash with explicit options.
   * @param opts Platform-specific options (timeout).
   * @return FlashStatus::OK on success.
   * @note NOT RT-safe: Unlocks flash, populates geometry.
   */
  [[nodiscard]] FlashStatus init(const Stm32FlashOptions& opts) noexcept {
    // Double-init guard: clean up before reinitializing
    if (initialized_) {
      deinit();
    }

    options_ = opts;

#ifndef APEX_HAL_STM32_MOCK
    // Unlock flash for write/erase operations
    if (HAL_FLASH_Unlock() != HAL_OK) {
      return FlashStatus::ERROR_WRITE_PROTECTED;
    }

    // Clear any pending error flags
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);

    // Populate geometry from hardware constants
    geometry_.baseAddress = FLASH_BASE;
    geometry_.totalSize = FLASH_SIZE;
    geometry_.pageSize = FLASH_PAGE_SIZE;
    geometry_.writeAlignment = 8; // 64-bit double-word
    geometry_.pageCount = static_cast<uint16_t>(FLASH_SIZE / FLASH_PAGE_SIZE);
    geometry_.bankCount = 2; // L4/G4 are dual-bank
#else
    // Mock: simulate a small flash region
    geometry_.baseAddress = 0x08000000;
    geometry_.totalSize = MOCK_FLASH_SIZE;
    geometry_.pageSize = MOCK_PAGE_SIZE;
    geometry_.writeAlignment = 8;
    geometry_.pageCount = MOCK_PAGE_COUNT;
    geometry_.bankCount = 1;

    // Initialize mock flash to erased state (0xFF)
    memset(mockFlash_, 0xFF, MOCK_FLASH_SIZE);
#endif

    stats_.reset();
    initialized_ = true;
    return FlashStatus::OK;
  }

  /**
   * @brief Deinitialize and lock flash.
   * @note NOT RT-safe: Locks flash.
   */
  void deinit() noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (initialized_) {
      HAL_FLASH_Lock();
    }
#endif
    initialized_ = false;
  }

  /**
   * @brief Check if flash is initialized.
   * @return true if initialized.
   * @note RT-safe.
   */
  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /* ----------------------------- Operations ----------------------------- */

  /**
   * @brief Read data from flash.
   * @param address Absolute flash address.
   * @param data Buffer to read into.
   * @param len Number of bytes to read.
   * @return FlashStatus::OK on success.
   * @note RT-safe: Memory-mapped read, no bus stall.
   */
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

#ifndef APEX_HAL_STM32_MOCK
    // Flash is memory-mapped on STM32 -- direct read
    memcpy(data, reinterpret_cast<const void*>(address), len);
#else
    const uint32_t OFFSET = address - geometry_.baseAddress;
    memcpy(data, &mockFlash_[OFFSET], len);
#endif

    stats_.bytesRead += static_cast<uint32_t>(len);
    return FlashStatus::OK;
  }

  /**
   * @brief Write (program) data to flash.
   * @param address Absolute flash address (must be 8-byte aligned).
   * @param data Data to program.
   * @param len Number of bytes to write.
   * @return FlashStatus::OK on success.
   * @note NOT RT-safe: Flash programming stalls the CPU bus.
   */
  [[nodiscard]] FlashStatus write(uint32_t address, const uint8_t* data,
                                  size_t len) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return FlashStatus::ERROR_INVALID_ARG;
    }
    if ((address % geometry_.writeAlignment) != 0) {
      return FlashStatus::ERROR_ALIGNMENT;
    }
    if (!isAddressInRange(address, len)) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    // Program in 64-bit (8-byte) double-word chunks
    // Pad the last chunk with 0xFF if len is not a multiple of 8
    uint32_t writeAddr = address;
    size_t remaining = len;
    const uint8_t* src = data;

    while (remaining > 0) {
      uint64_t doubleWord = 0xFFFFFFFFFFFFFFFFULL;
      const size_t CHUNK = (remaining >= 8) ? 8 : remaining;
      memcpy(&doubleWord, src, CHUNK);

      if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, writeAddr, doubleWord) != HAL_OK) {
        ++stats_.writeErrors;
        return FlashStatus::ERROR_PROGRAM_FAILED;
      }

      writeAddr += 8;
      src += CHUNK;
      remaining -= CHUNK;
    }
#else
    const uint32_t OFFSET = address - geometry_.baseAddress;
    memcpy(&mockFlash_[OFFSET], data, len);
#endif

    stats_.bytesWritten += static_cast<uint32_t>(len);
    return FlashStatus::OK;
  }

  /**
   * @brief Erase a single flash page.
   * @param pageIndex Zero-based page index.
   * @return FlashStatus::OK on success.
   * @note NOT RT-safe: Erase stalls CPU for ~25ms per page.
   */
  [[nodiscard]] FlashStatus erasePage(uint32_t pageIndex) noexcept override {
    return erasePages(pageIndex, 1);
  }

  /**
   * @brief Erase a contiguous range of flash pages.
   * @param startPage First page index to erase.
   * @param count Number of pages to erase.
   * @return FlashStatus::OK on success.
   * @note NOT RT-safe: Erase stalls CPU for ~25ms per page.
   */
  [[nodiscard]] FlashStatus erasePages(uint32_t startPage, uint32_t count) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (count == 0 || (startPage + count) > geometry_.pageCount) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

#ifndef APEX_HAL_STM32_MOCK
    FLASH_EraseInitTypeDef eraseInit = {};
    eraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
    eraseInit.Page = startPage;
    eraseInit.NbPages = count;

    // Determine which bank the pages are in
    const uint32_t PAGES_PER_BANK = geometry_.pageCount / geometry_.bankCount;
    if (startPage < PAGES_PER_BANK) {
      eraseInit.Banks = FLASH_BANK_1;
      // If range crosses into bank 2, limit to bank 1 and erase bank 2 separately
      if ((startPage + count) > PAGES_PER_BANK) {
        // Erase bank 1 portion
        const uint32_t BANK1_COUNT = PAGES_PER_BANK - startPage;
        eraseInit.NbPages = BANK1_COUNT;

        uint32_t pageError = 0;
        if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) {
          ++stats_.eraseErrors;
          return FlashStatus::ERROR_ERASE_FAILED;
        }
        stats_.pagesErased += BANK1_COUNT;

        // Erase bank 2 portion
        eraseInit.Banks = FLASH_BANK_2;
        eraseInit.Page = 0;
        eraseInit.NbPages = count - BANK1_COUNT;

        if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) {
          ++stats_.eraseErrors;
          return FlashStatus::ERROR_ERASE_FAILED;
        }
        stats_.pagesErased += count - BANK1_COUNT;
        return FlashStatus::OK;
      }
    } else {
      eraseInit.Banks = FLASH_BANK_2;
      eraseInit.Page = startPage - PAGES_PER_BANK;
    }

    uint32_t pageError = 0;
    if (HAL_FLASHEx_Erase(&eraseInit, &pageError) != HAL_OK) {
      ++stats_.eraseErrors;
      return FlashStatus::ERROR_ERASE_FAILED;
    }
#else
    const uint32_t START_OFFSET = startPage * geometry_.pageSize;
    memset(&mockFlash_[START_OFFSET], 0xFF, static_cast<size_t>(count) * geometry_.pageSize);
#endif

    stats_.pagesErased += count;
    return FlashStatus::OK;
  }

  /* ----------------------------- Geometry ----------------------------- */

  /**
   * @brief Get flash memory geometry.
   * @return Geometry descriptor.
   * @note RT-safe.
   */
  [[nodiscard]] FlashGeometry geometry() const noexcept override { return geometry_; }

  /**
   * @brief Convert absolute address to page index.
   * @param address Absolute flash address.
   * @return Page index containing the address.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t pageForAddress(uint32_t address) const noexcept override {
    return (address - geometry_.baseAddress) / geometry_.pageSize;
  }

  /**
   * @brief Convert page index to absolute start address.
   * @param pageIndex Zero-based page index.
   * @return Absolute address of the page start.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t addressForPage(uint32_t pageIndex) const noexcept override {
    return geometry_.baseAddress + (pageIndex * geometry_.pageSize);
  }

  /* ----------------------------- Status ----------------------------- */

  /**
   * @brief Check if flash is currently busy.
   * @return true if an operation is in progress.
   * @note RT-safe.
   */
  [[nodiscard]] bool isBusy() const noexcept override {
#ifndef APEX_HAL_STM32_MOCK
    if (!initialized_) {
      return false;
    }
    return __HAL_FLASH_GET_FLAG(FLASH_FLAG_BSY);
#else
    return false;
#endif
  }

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] const FlashStats& stats() const noexcept override { return stats_; }

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe.
   */
  void resetStats() noexcept override { stats_.reset(); }

private:
  /* ----------------------------- Helpers ----------------------------- */

  /**
   * @brief Check if address + length falls within flash bounds.
   * @param address Start address.
   * @param len Number of bytes.
   * @return true if the entire range is within flash.
   */
  [[nodiscard]] bool isAddressInRange(uint32_t address, size_t len) const noexcept {
    if (address < geometry_.baseAddress) {
      return false;
    }
    const uint32_t END = geometry_.baseAddress + geometry_.totalSize;
    return (address <= END) && ((address + len) <= END);
  }

  /* ----------------------------- Members ----------------------------- */

  Stm32FlashOptions options_ = {};
  FlashGeometry geometry_ = {};
  bool initialized_ = false;
  FlashStats stats_ = {};

#ifdef APEX_HAL_STM32_MOCK
  static constexpr uint32_t MOCK_PAGE_SIZE = 2048;
  static constexpr uint32_t MOCK_PAGE_COUNT = 32;
  static constexpr uint32_t MOCK_FLASH_SIZE = MOCK_PAGE_SIZE * MOCK_PAGE_COUNT;
  uint8_t mockFlash_[MOCK_FLASH_SIZE] = {};
#endif
};

} // namespace stm32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_STM32_FLASH_HPP
