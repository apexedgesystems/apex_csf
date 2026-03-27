#ifndef APEX_HAL_ESP32_NVS_FLASH_HPP
#define APEX_HAL_ESP32_NVS_FLASH_HPP
/**
 * @file Esp32NvsFlash.hpp
 * @brief ESP32-S3 NVS-backed flash implementation for key storage.
 *
 * Uses ESP-IDF's Non-Volatile Storage (NVS) for persistent key storage.
 * NVS provides wear-leveled key-value storage over flash, which is simpler
 * and more reliable than the raw flash sector approach used on STM32 and Pico.
 *
 * Instead of page-based erase/write operations, this implementation maps
 * the IFlash interface onto NVS blob operations:
 *   - write() -> nvs_set_blob() to a named key
 *   - read()  -> nvs_get_blob() from a named key
 *   - erasePage() -> nvs_erase_all() in the namespace
 *
 * The address parameter in read/write is reinterpreted as a byte offset
 * into a virtual linear region. The actual storage uses NVS key-value pairs
 * with a single blob key "keys" for the entire key store.
 *
 * In mock mode (APEX_HAL_ESP32_MOCK), uses a 512-byte static shadow buffer.
 * No ESP-IDF dependencies required.
 *
 * @note NOT thread-safe. Use from a single task.
 */

#include "src/system/core/hal/base/IFlash.hpp"

#ifndef APEX_HAL_ESP32_MOCK
#include "nvs.h"
#include "nvs_flash.h"
#endif

#include <string.h>

namespace apex {
namespace hal {
namespace esp32 {

/* ----------------------------- Constants ----------------------------- */

/// NVS namespace for the encryptor key store.
static constexpr const char* NVS_NAMESPACE = "encryptor";

/// NVS key name for the key blob.
static constexpr const char* NVS_KEY_NAME = "keys";

/// Virtual flash base address (arbitrary, used for IFlash compatibility).
static constexpr uint32_t NVS_VIRTUAL_BASE = 0x00000000;

/// Virtual flash size (16 key slots x 32 bytes = 512 bytes).
static constexpr uint32_t NVS_VIRTUAL_SIZE = 16 * 32;

/// Virtual page size (entire store is one "page" for erase purposes).
static constexpr uint32_t NVS_PAGE_SIZE = NVS_VIRTUAL_SIZE;

/* ----------------------------- Esp32NvsFlash ----------------------------- */

/**
 * @class Esp32NvsFlash
 * @brief NVS-backed flash implementation for key storage.
 *
 * Implements the IFlash interface using NVS blob operations. The entire
 * key store (512 bytes) is stored as a single NVS blob, read/written
 * atomically.
 *
 * Memory usage: 512-byte shadow buffer for read-modify-write operations.
 */
class Esp32NvsFlash final : public IFlash {
public:
  Esp32NvsFlash() noexcept = default;
  ~Esp32NvsFlash() override { deinit(); }

  Esp32NvsFlash(const Esp32NvsFlash&) = delete;
  Esp32NvsFlash& operator=(const Esp32NvsFlash&) = delete;
  Esp32NvsFlash(Esp32NvsFlash&&) = delete;
  Esp32NvsFlash& operator=(Esp32NvsFlash&&) = delete;

  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] FlashStatus init() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    // Initialize NVS flash partition
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated or new version found, erase and retry
      nvs_flash_erase();
      err = nvs_flash_init();
    }
    if (err != ESP_OK) {
      return FlashStatus::ERROR_NOT_INIT;
    }

    // Open NVS handle
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle_);
    if (err != ESP_OK) {
      return FlashStatus::ERROR_NOT_INIT;
    }

    // Load existing data into shadow buffer (if any)
    size_t blobSize = NVS_VIRTUAL_SIZE;
    err = nvs_get_blob(handle_, NVS_KEY_NAME, shadow_, &blobSize);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      // No existing data, initialize shadow to 0xFF (erased state)
      memset(shadow_, 0xFF, NVS_VIRTUAL_SIZE);
    } else if (err != ESP_OK) {
      nvs_close(handle_);
      return FlashStatus::ERROR_NOT_INIT;
    }
#else
    // Mock: initialize shadow buffer to erased state
    memset(shadow_, 0xFF, NVS_VIRTUAL_SIZE);
#endif

    initialized_ = true;
    return FlashStatus::OK;
  }

  void deinit() noexcept override {
#ifndef APEX_HAL_ESP32_MOCK
    if (initialized_) {
      nvs_close(handle_);
    }
#endif
    initialized_ = false;
  }

  [[nodiscard]] bool isInitialized() const noexcept override { return initialized_; }

  /* ----------------------------- Operations ----------------------------- */

  [[nodiscard]] FlashStatus read(uint32_t address, uint8_t* data, size_t len) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (data == nullptr || len == 0) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    const uint32_t OFFSET = address - NVS_VIRTUAL_BASE;
    if ((OFFSET + len) > NVS_VIRTUAL_SIZE) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    // Read from shadow buffer
    memcpy(data, shadow_ + OFFSET, len);
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

    const uint32_t OFFSET = address - NVS_VIRTUAL_BASE;
    if ((OFFSET + len) > NVS_VIRTUAL_SIZE) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    // Update shadow buffer
    memcpy(shadow_ + OFFSET, data, len);

#ifndef APEX_HAL_ESP32_MOCK
    // Persist to NVS
    esp_err_t err = nvs_set_blob(handle_, NVS_KEY_NAME, shadow_, NVS_VIRTUAL_SIZE);
    if (err != ESP_OK) {
      ++stats_.writeErrors;
      return FlashStatus::ERROR_PROGRAM_FAILED;
    }

    err = nvs_commit(handle_);
    if (err != ESP_OK) {
      ++stats_.writeErrors;
      return FlashStatus::ERROR_PROGRAM_FAILED;
    }
#endif

    stats_.bytesWritten += len;
    return FlashStatus::OK;
  }

  [[nodiscard]] FlashStatus erasePage(uint32_t pageIndex) noexcept override {
    if (!initialized_) {
      return FlashStatus::ERROR_NOT_INIT;
    }
    if (pageIndex >= 1) {
      return FlashStatus::ERROR_INVALID_ARG;
    }

    // Erase shadow buffer
    memset(shadow_, 0xFF, NVS_VIRTUAL_SIZE);

#ifndef APEX_HAL_ESP32_MOCK
    // Persist erased state
    esp_err_t err = nvs_set_blob(handle_, NVS_KEY_NAME, shadow_, NVS_VIRTUAL_SIZE);
    if (err != ESP_OK) {
      ++stats_.eraseErrors;
      return FlashStatus::ERROR_ERASE_FAILED;
    }

    err = nvs_commit(handle_);
    if (err != ESP_OK) {
      ++stats_.eraseErrors;
      return FlashStatus::ERROR_ERASE_FAILED;
    }
#endif

    ++stats_.pagesErased;
    return FlashStatus::OK;
  }

  [[nodiscard]] FlashStatus erasePages(uint32_t startPage, uint32_t count) noexcept override {
    if (startPage != 0 || count != 1) {
      return FlashStatus::ERROR_INVALID_ARG;
    }
    return erasePage(0);
  }

  /* ----------------------------- Geometry ----------------------------- */

  [[nodiscard]] FlashGeometry geometry() const noexcept override {
    FlashGeometry geo;
    geo.baseAddress = NVS_VIRTUAL_BASE;
    geo.totalSize = NVS_VIRTUAL_SIZE;
    geo.pageSize = NVS_PAGE_SIZE;
    geo.writeAlignment = 1; // NVS has no alignment constraint
    geo.pageCount = 1;
    geo.bankCount = 1;
    return geo;
  }

  [[nodiscard]] uint32_t pageForAddress(uint32_t /*address*/) const noexcept override {
    return 0; // Single page
  }

  [[nodiscard]] uint32_t addressForPage(uint32_t /*pageIndex*/) const noexcept override {
    return NVS_VIRTUAL_BASE;
  }

  /* ----------------------------- Status ----------------------------- */

  [[nodiscard]] bool isBusy() const noexcept override { return false; }

  [[nodiscard]] const FlashStats& stats() const noexcept override { return stats_; }

  void resetStats() noexcept override { stats_.reset(); }

private:
  bool initialized_ = false;
#ifndef APEX_HAL_ESP32_MOCK
  nvs_handle_t handle_ = 0;
#endif
  uint8_t shadow_[NVS_VIRTUAL_SIZE] = {};
  FlashStats stats_ = {};
};

} // namespace esp32
} // namespace hal
} // namespace apex

#endif // APEX_HAL_ESP32_NVS_FLASH_HPP
