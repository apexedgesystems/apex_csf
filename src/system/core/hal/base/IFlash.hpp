#ifndef APEX_HAL_IFLASH_HPP
#define APEX_HAL_IFLASH_HPP
/**
 * @file IFlash.hpp
 * @brief Abstract internal flash memory interface for embedded systems.
 *
 * Platform-agnostic interface for MCU internal flash. Covers read, write
 * (program), and erase operations with page-level granularity. All write
 * and erase operations are blocking and NOT RT-safe (flash programming
 * stalls the CPU bus).
 *
 * Design principles:
 *  - No heap allocation
 *  - No POSIX dependencies
 *  - Page-based erase (caller specifies page indices, not raw addresses)
 *  - Read is memory-mapped on most MCUs (RT-safe, instant)
 *  - Write/erase block the CPU (NOT RT-safe)
 *  - Geometry struct reports hardware constraints (page size, alignment)
 *
 * Implementations:
 *  - Stm32Flash (STM32 internal flash via HAL, dual-bank support)
 *  - PicoFlash (RP2040 XIP flash)
 *  - Esp32NvsFlash (ESP32 NVS partition)
 */

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace hal {

/* ----------------------------- FlashStatus ----------------------------- */

/**
 * @brief Status codes for flash operations.
 */
enum class FlashStatus : uint8_t {
  OK = 0,                ///< Operation succeeded.
  BUSY,                  ///< Flash busy (operation in progress).
  ERROR_TIMEOUT,         ///< Operation timed out.
  ERROR_NOT_INIT,        ///< Flash not initialized.
  ERROR_INVALID_ARG,     ///< Invalid argument (null pointer, zero length, out of range).
  ERROR_ALIGNMENT,       ///< Write address not aligned to required boundary.
  ERROR_WRITE_PROTECTED, ///< Region is write-protected.
  ERROR_ERASE_FAILED,    ///< Erase operation failed.
  ERROR_PROGRAM_FAILED,  ///< Write/program operation failed.
  ERROR_VERIFY_FAILED    ///< Read-back verification failed.
};

/**
 * @brief Convert FlashStatus to string.
 * @param s Status value.
 * @return Human-readable string.
 * @note RT-safe: Returns static string literal.
 */
inline const char* toString(FlashStatus s) noexcept {
  switch (s) {
  case FlashStatus::OK:
    return "OK";
  case FlashStatus::BUSY:
    return "BUSY";
  case FlashStatus::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case FlashStatus::ERROR_NOT_INIT:
    return "ERROR_NOT_INIT";
  case FlashStatus::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  case FlashStatus::ERROR_ALIGNMENT:
    return "ERROR_ALIGNMENT";
  case FlashStatus::ERROR_WRITE_PROTECTED:
    return "ERROR_WRITE_PROTECTED";
  case FlashStatus::ERROR_ERASE_FAILED:
    return "ERROR_ERASE_FAILED";
  case FlashStatus::ERROR_PROGRAM_FAILED:
    return "ERROR_PROGRAM_FAILED";
  case FlashStatus::ERROR_VERIFY_FAILED:
    return "ERROR_VERIFY_FAILED";
  default:
    return "UNKNOWN";
  }
}

/* ----------------------------- FlashGeometry ----------------------------- */

/**
 * @brief Flash memory geometry descriptor.
 *
 * Reports hardware-specific constraints that callers need to plan
 * read/write/erase operations. Populated by the implementation at init time.
 */
struct FlashGeometry {
  uint32_t baseAddress = 0;    ///< Start address of flash region.
  uint32_t totalSize = 0;      ///< Total flash size in bytes.
  uint32_t pageSize = 0;       ///< Erase granularity in bytes.
  uint32_t writeAlignment = 0; ///< Minimum write alignment (bytes).
  uint16_t pageCount = 0;      ///< Total number of pages.
  uint8_t bankCount = 0;       ///< Number of banks (1 or 2 for dual-bank).
};

/* ----------------------------- FlashStats ----------------------------- */

/**
 * @brief Flash operation statistics for monitoring.
 */
struct FlashStats {
  uint32_t bytesWritten = 0; ///< Total bytes programmed.
  uint32_t bytesRead = 0;    ///< Total bytes read.
  uint32_t pagesErased = 0;  ///< Total pages erased.
  uint32_t writeErrors = 0;  ///< Program operation errors.
  uint32_t eraseErrors = 0;  ///< Erase operation errors.
  uint32_t readErrors = 0;   ///< Read operation errors.

  /**
   * @brief Reset all counters to zero.
   * @note RT-safe.
   */
  void reset() noexcept {
    bytesWritten = 0;
    bytesRead = 0;
    pagesErased = 0;
    writeErrors = 0;
    eraseErrors = 0;
    readErrors = 0;
  }

  /**
   * @brief Get total error count.
   * @return Sum of all error counters.
   * @note RT-safe.
   */
  [[nodiscard]] uint32_t totalErrors() const noexcept {
    return writeErrors + eraseErrors + readErrors;
  }
};

/* ----------------------------- IFlash ----------------------------- */

/**
 * @class IFlash
 * @brief Abstract internal flash memory interface.
 *
 * Provides a common API for MCU internal flash across different platforms.
 * Flash is fundamentally different from communication peripherals: it is
 * internal storage with page-based erase granularity and strict write
 * alignment requirements.
 *
 * Lifecycle:
 *  1. Construct implementation (platform-specific)
 *  2. Call init() to unlock and prepare flash
 *  3. Call read()/write()/erasePage()/erasePages() for storage operations
 *  4. Call deinit() to lock flash
 *
 * Thread Safety:
 *  - Implementations are NOT thread-safe by default.
 *  - For RTOS use, wrap with mutex or use from single task.
 *
 * RT-Safety:
 *  - init()/deinit(): NOT RT-safe
 *  - read(): RT-safe (memory-mapped on most MCUs)
 *  - write(): NOT RT-safe (flash programming stalls CPU bus)
 *  - erasePage()/erasePages(): NOT RT-safe (erase stalls CPU for milliseconds)
 *  - geometry()/pageForAddress()/addressForPage(): RT-safe
 *  - isBusy()/stats()/resetStats(): RT-safe
 */
class IFlash {
public:
  virtual ~IFlash() = default;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Initialize flash for read/write/erase operations.
   * @return OK on success, ERROR_* on failure.
   * @note NOT RT-safe: Unlocks flash, clears error flags.
   */
  [[nodiscard]] virtual FlashStatus init() noexcept = 0;

  /**
   * @brief Deinitialize and lock flash.
   * @note NOT RT-safe: Locks flash to prevent accidental writes.
   */
  virtual void deinit() noexcept = 0;

  /**
   * @brief Check if flash is initialized and ready.
   * @return true if initialized.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isInitialized() const noexcept = 0;

  /* ----------------------------- Operations ----------------------------- */

  /**
   * @brief Read data from flash.
   *
   * On MCUs with memory-mapped flash, this is a simple memcpy from the
   * flash address. Always succeeds if arguments are valid and flash is
   * initialized.
   *
   * @param address Absolute flash address to read from.
   * @param data Buffer to read into. Must not be null.
   * @param len Number of bytes to read. Must be > 0.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if null pointer, zero length, or out of range.
   * @note RT-safe: Memory-mapped read, no bus stall.
   */
  [[nodiscard]] virtual FlashStatus read(uint32_t address, uint8_t* data, size_t len) noexcept = 0;

  /**
   * @brief Write (program) data to flash.
   *
   * Programs @p len bytes from @p data to the specified flash address.
   * The address must be aligned to the write alignment boundary reported
   * by geometry(). Flash must be erased before writing (writes can only
   * clear bits, not set them).
   *
   * @param address Absolute flash address to write to. Must be aligned.
   * @param data Data to program. Must not be null.
   * @param len Number of bytes to write. Must be > 0.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if null/zero/out-of-range,
   *         ERROR_ALIGNMENT if address not aligned,
   *         ERROR_PROGRAM_FAILED on hardware error.
   * @note NOT RT-safe: Flash programming stalls the CPU bus.
   */
  [[nodiscard]] virtual FlashStatus write(uint32_t address, const uint8_t* data,
                                          size_t len) noexcept = 0;

  /**
   * @brief Erase a single flash page.
   *
   * Fills the entire page with 0xFF. All data in the page is lost.
   *
   * @param pageIndex Zero-based page index.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if page index out of range,
   *         ERROR_ERASE_FAILED on hardware error.
   * @note NOT RT-safe: Erase stalls CPU for milliseconds.
   */
  [[nodiscard]] virtual FlashStatus erasePage(uint32_t pageIndex) noexcept = 0;

  /**
   * @brief Erase a contiguous range of flash pages.
   *
   * Erases @p count pages starting at @p startPage.
   *
   * @param startPage First page index to erase.
   * @param count Number of pages to erase. Must be > 0.
   * @return OK on success, ERROR_NOT_INIT if not initialized,
   *         ERROR_INVALID_ARG if range out of bounds,
   *         ERROR_ERASE_FAILED on hardware error.
   * @note NOT RT-safe: Erase stalls CPU for milliseconds per page.
   */
  [[nodiscard]] virtual FlashStatus erasePages(uint32_t startPage, uint32_t count) noexcept = 0;

  /* ----------------------------- Geometry ----------------------------- */

  /**
   * @brief Get flash memory geometry.
   * @return Geometry descriptor (page size, total size, alignment, etc.).
   * @note RT-safe: Returns cached value.
   */
  [[nodiscard]] virtual FlashGeometry geometry() const noexcept = 0;

  /**
   * @brief Convert absolute address to page index.
   * @param address Absolute flash address.
   * @return Page index containing the address.
   * @note RT-safe.
   */
  [[nodiscard]] virtual uint32_t pageForAddress(uint32_t address) const noexcept = 0;

  /**
   * @brief Convert page index to absolute start address.
   * @param pageIndex Zero-based page index.
   * @return Absolute address of the first byte in the page.
   * @note RT-safe.
   */
  [[nodiscard]] virtual uint32_t addressForPage(uint32_t pageIndex) const noexcept = 0;

  /* ----------------------------- Status ----------------------------- */

  /**
   * @brief Check if flash is currently busy with an operation.
   * @return true if an operation is in progress.
   * @note RT-safe.
   */
  [[nodiscard]] virtual bool isBusy() const noexcept = 0;

  /**
   * @brief Get accumulated statistics.
   * @return Reference to statistics structure.
   * @note RT-safe.
   */
  [[nodiscard]] virtual const FlashStats& stats() const noexcept = 0;

  /**
   * @brief Reset statistics to zero.
   * @note RT-safe.
   */
  virtual void resetStats() noexcept = 0;

protected:
  IFlash() = default;
  IFlash(const IFlash&) = delete;
  IFlash& operator=(const IFlash&) = delete;
  IFlash(IFlash&&) = default;
  IFlash& operator=(IFlash&&) = default;
};

} // namespace hal
} // namespace apex

#endif // APEX_HAL_IFLASH_HPP
