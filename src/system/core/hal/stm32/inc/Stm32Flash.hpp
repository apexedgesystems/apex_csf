#ifndef APEX_HAL_STM32_FLASH_HPP
#define APEX_HAL_STM32_FLASH_HPP
/**
 * @file Stm32Flash.hpp
 * @brief STM32 internal flash implementation.
 *
 * Provides page-based read/write/erase for STM32 internal flash memory.
 * No internal buffers, no interrupts -- all operations are blocking.
 *
 * Supported families:
 *  - STM32L4 (e.g., STM32L476xx) -- 1MB dual-bank, 2KB pages
 *  - STM32G4 (e.g., STM32G474xx) -- 512KB dual-bank, 2KB pages
 *  - STM32F7 (e.g., STM32F767xx) -- sector-based: 4 x 32KB, 128KB, then
 *    256KB sectors per bank (halved when the nDBANK option selects dual
 *    bank).
 *  - STM32F4 (e.g., STM32F446xx) -- sector-based with the same rule at a
 *    16KB small sector: 4 x 16KB, 64KB, then 128KB sectors, single bank.
 *    Sectors are exposed through the page vocabulary on both: page index =
 *    sector index, geometry().pageSize is the smallest sector, and
 *    pageSizeAt() reports each sector's true size. The layout is derived
 *    at init() from the flash-size register and, where the part has one,
 *    the nDBANK option bit. Programming is 32-bit (double-word needs
 *    external Vpp).
 *
 * NOT supported:
 *  - STM32H7 (128KB sectors, 256-bit flash words)
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

// STM32 HAL includes -- page-based (L4/G4) and sector-based (F4/F7) families
#if defined(STM32F4xx) || defined(STM32F446xx) || defined(STM32F401xC) || defined(STM32F411xE)
#include "stm32f4xx_hal.h"
#define APEX_STM32_FLASH_SECTORED 1
#define APEX_STM32_FLASH_SMALL_SECTOR (16U * 1024U)
#elif defined(STM32H7xx) || defined(STM32H743xx)
#error "Stm32Flash: H7 uses sector-based erase (not supported by this wrapper)."
#elif defined(STM32F1) || defined(STM32F103xB)
#error "Stm32Flash: F1 has different flash register interface (not supported)."
#elif defined(STM32L476xx) || defined(STM32L4xx)
#include "stm32l4xx_hal.h"
#elif defined(STM32F7xx) || defined(STM32F767xx)
#include "stm32f7xx_hal.h"
#define APEX_STM32_FLASH_SECTORED 1
#define APEX_STM32_FLASH_SMALL_SECTOR (32U * 1024U)
#elif defined(STM32G4xx) || defined(STM32G474xx)
#include "stm32g4xx_hal.h"
#else
#ifndef APEX_HAL_STM32_MOCK
#error "STM32 family not defined. Define STM32L476xx, STM32F767xx, STM32G4xx, etc."
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

  /* ----------------------------- Sector Layout ----------------------------- */

  /// Upper bound on sectors for the sector-based families (F7 dual-bank: 2 x 12).
  static constexpr uint32_t MAX_SECTORS = 24;

  /**
   * @brief Compute the F4/F7 sector layout for a flash size, small-sector
   *        size, and bank count.
   *
   * Each bank holds four small sectors, one sector of four times that
   * size, and large sectors of eight times that size until the bank is
   * full. The small sector is 16 KB on the F4 and 32 KB on the F7; the
   * F7's dual-bank option halves it and splits the array into two equal
   * banks with the second bank's sectors numbered after the first bank's.
   *
   * Pure arithmetic, so it is the same in every build and unit-testable
   * without hardware.
   *
   * @param totalBytes Flash size in bytes.
   * @param smallSector Size of the small sectors in bytes.
   * @param banks Number of banks (1 or 2).
   * @param sizesOut Receives each sector's size, in index order.
   * @param maxCount Capacity of sizesOut.
   * @return Number of sectors written (0 on bad arguments or a full table).
   * @note RT-safe.
   */
  [[nodiscard]] static uint32_t sectorLayout(uint32_t totalBytes, uint32_t smallSector,
                                             uint32_t banks, uint32_t* sizesOut,
                                             uint32_t maxCount) noexcept {
    if (sizesOut == nullptr || totalBytes == 0 || smallSector == 0 || banks == 0) {
      return 0;
    }
    const uint32_t BANKS = banks;
    const uint32_t BANK_BYTES = totalBytes / BANKS;
    const uint32_t SMALL = smallSector;
    uint32_t count = 0;
    for (uint32_t b = 0; b < BANKS; ++b) {
      uint32_t filled = 0;
      uint32_t index = 0;
      while (filled < BANK_BYTES) {
        const uint32_t SIZE = (index < 4U) ? SMALL : (index == 4U) ? (SMALL * 4U) : (SMALL * 8U);
        if (count >= maxCount) {
          return 0;
        }
        sizesOut[count++] = SIZE;
        filled += SIZE;
        ++index;
      }
    }
    return count;
  }

  /**
   * @brief Size of one page (sector on sector-based families).
   * @param pageIndex Zero-based page index.
   * @return Size in bytes, or 0 when the index is out of range.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t pageSizeAt(uint32_t pageIndex) const noexcept {
    if (pageIndex >= geometry_.pageCount) {
      return 0;
    }
#ifdef APEX_STM32_FLASH_SECTORED
    return sectorSizes_[pageIndex];
#else
    return geometry_.pageSize;
#endif
  }

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

    // Clear any pending error flags (the F4 HAL has no aggregate name)
#if defined(FLASH_FLAG_ALL_ERRORS)
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS);
#else
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
#endif

#ifdef APEX_STM32_FLASH_SECTORED
    // Sector-based: size from the flash-size register (KB), bank mode from
    // the nDBANK option bit on parts that have one, sector table from the
    // family rule with the family's small-sector size.
    const uint32_t SIZE_KB = *reinterpret_cast<const volatile uint16_t*>(FLASHSIZE_BASE);
#if defined(FLASH_OPTCR_nDBANK)
    const bool DUAL = (READ_BIT(FLASH->OPTCR, FLASH_OPTCR_nDBANK) == 0U);
#else
    const bool DUAL = false;
#endif
    const uint32_t SMALL =
        DUAL ? (APEX_STM32_FLASH_SMALL_SECTOR / 2U) : APEX_STM32_FLASH_SMALL_SECTOR;
    geometry_.baseAddress = FLASH_BASE;
    geometry_.totalSize = SIZE_KB * 1024U;
    sectorCount_ =
        sectorLayout(geometry_.totalSize, SMALL, DUAL ? 2U : 1U, sectorSizes_, MAX_SECTORS);
    uint32_t offset = 0;
    for (uint32_t i = 0; i < sectorCount_; ++i) {
      sectorOffsets_[i] = offset;
      offset += sectorSizes_[i];
    }
    geometry_.pageSize = (sectorCount_ > 0) ? sectorSizes_[0] : 0; // smallest sector
    geometry_.writeAlignment = 4; // 32-bit word (double-word needs external Vpp)
    geometry_.pageCount = static_cast<uint16_t>(sectorCount_);
    geometry_.bankCount = DUAL ? 2 : 1;
#else
    // Populate geometry from hardware constants
    geometry_.baseAddress = FLASH_BASE;
    geometry_.totalSize = FLASH_SIZE;
    geometry_.pageSize = FLASH_PAGE_SIZE;
    geometry_.writeAlignment = 8; // 64-bit double-word
    geometry_.pageCount = static_cast<uint16_t>(FLASH_SIZE / FLASH_PAGE_SIZE);
    geometry_.bankCount = 2; // L4/G4 are dual-bank
#endif
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

#if defined(APEX_STM32_FLASH_SECTORED)
    // Program in 32-bit words; pad the last chunk with 0xFF.
    uint32_t writeAddr = address;
    size_t remaining = len;
    const uint8_t* src = data;

    while (remaining > 0) {
      uint32_t word = 0xFFFFFFFFU;
      const size_t CHUNK = (remaining >= 4) ? 4 : remaining;
      memcpy(&word, src, CHUNK);

      if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, writeAddr, word) != HAL_OK) {
        ++stats_.writeErrors;
        return FlashStatus::ERROR_PROGRAM_FAILED;
      }

      writeAddr += 4;
      src += CHUNK;
      remaining -= CHUNK;
    }
#elif !defined(APEX_HAL_STM32_MOCK)
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

#if defined(APEX_STM32_FLASH_SECTORED)
    FLASH_EraseInitTypeDef eraseInit = {};
    eraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
    eraseInit.Sector = startPage;
    eraseInit.NbSectors = count;
    eraseInit.VoltageRange = FLASH_VOLTAGE_RANGE_3;
#if defined(FLASH_OPTCR_nDBANK)
    eraseInit.Banks = FLASH_BANK_1; // consulted by mass erase only
#endif
    uint32_t sectorError = 0;
    if (HAL_FLASHEx_Erase(&eraseInit, &sectorError) != HAL_OK) {
      ++stats_.eraseErrors;
      return FlashStatus::ERROR_ERASE_FAILED;
    }
#elif !defined(APEX_HAL_STM32_MOCK)
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
#ifdef APEX_STM32_FLASH_SECTORED
    const uint32_t OFFSET = address - geometry_.baseAddress;
    uint32_t index = 0;
    while ((index + 1U) < sectorCount_ && OFFSET >= sectorOffsets_[index + 1U]) {
      ++index;
    }
    return index;
#else
    return (address - geometry_.baseAddress) / geometry_.pageSize;
#endif
  }

  /**
   * @brief Convert page index to absolute start address.
   * @param pageIndex Zero-based page index.
   * @return Absolute address of the page start.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t addressForPage(uint32_t pageIndex) const noexcept override {
#ifdef APEX_STM32_FLASH_SECTORED
    return geometry_.baseAddress + ((pageIndex < sectorCount_) ? sectorOffsets_[pageIndex] : 0U);
#else
    return geometry_.baseAddress + (pageIndex * geometry_.pageSize);
#endif
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

#ifdef APEX_STM32_FLASH_SECTORED
  uint32_t sectorSizes_[MAX_SECTORS] = {};   ///< Per-sector size, index order.
  uint32_t sectorOffsets_[MAX_SECTORS] = {}; ///< Per-sector offset from baseAddress.
  uint32_t sectorCount_ = 0;
#endif

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
