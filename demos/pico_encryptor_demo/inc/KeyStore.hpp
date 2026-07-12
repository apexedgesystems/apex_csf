#ifndef APEX_PICO_ENCRYPTOR_KEY_STORE_HPP
#define APEX_PICO_ENCRYPTOR_KEY_STORE_HPP
/**
 * @file KeyStore.hpp
 * @brief Flash-backed AES-256 key storage for the pico_encryptor_demo.
 *
 * Manages 16 key slots (32 bytes each) on a dedicated flash sector. Keys
 * are cached in RAM for RT-safe read access. Write and erase operations
 * are blocking (flash programming disables XIP).
 *
 * Flash layout (sector 511, offset 0x1FF000, address 0x101FF000):
 *   Slot  0: bytes 0x000-0x01F  (32 bytes)
 *   Slot  1: bytes 0x020-0x03F  (32 bytes)
 *   ...
 *   Slot 15: bytes 0x1E0-0x1FF  (32 bytes)
 *   Unused:  bytes 0x200-0xFFF  (3,584 bytes reserved)
 *
 * Slot detection: empty = all 0xFF (flash erased state).
 *
 * @note Write/erase are NOT RT-safe. Call only from init or command handler.
 * @note Read/select are RT-safe (read from RAM cache).
 */

#include "EncryptorConfig.hpp"
#include "IFlash.hpp"

#include <stddef.h>
#include <stdint.h>

namespace encryptor {

/* ----------------------------- KeyStoreStatus ----------------------------- */

enum class KeyStoreStatus : uint8_t {
  OK = 0,
  ERROR_FLASH_INIT,
  ERROR_FLASH_READ,
  ERROR_FLASH_WRITE,
  ERROR_FLASH_ERASE,
  ERROR_INVALID_SLOT,
  ERROR_SLOT_EMPTY
};

/* ----------------------------- KeyStore ----------------------------- */

/**
 * @class KeyStore
 * @brief Flash-persistent key storage with RAM cache.
 *
 * @note RT-safe methods: readKey(), selectKey(), populatedCount(), bitmap().
 * @note NOT RT-safe: init(), writeKey(), eraseAll().
 */
class KeyStore {
public:
  /// Flash page for key storage (page 511 on RP2040, last 4 KB sector).
  static constexpr uint32_t KEY_STORE_PAGE = 511;

  static constexpr size_t SLOT_SIZE = AES_KEY_LEN;
  static constexpr size_t TOTAL_KEY_DATA = KEY_SLOT_COUNT * SLOT_SIZE;

  explicit KeyStore(apex::hal::IFlash& flash) noexcept;

  /* ----------------------------- Lifecycle ----------------------------- */

  KeyStoreStatus init() noexcept;

  /* ----------------------------- Key Operations ----------------------------- */

  KeyStoreStatus writeKey(uint8_t slot, const uint8_t* key) noexcept;
  KeyStoreStatus readKey(uint8_t slot, uint8_t* keyOut) const noexcept;
  KeyStoreStatus eraseAll() noexcept;

  /* ----------------------------- Query ----------------------------- */

  uint8_t populatedCount() const noexcept { return populatedCount_; }
  uint16_t bitmap() const noexcept { return bitmap_; }
  bool isSlotPopulated(uint8_t slot) const noexcept;

  /* ----------------------------- Key Selection ----------------------------- */

  KeyStoreStatus selectKey(uint8_t* keyOut, uint8_t& slotOut) const noexcept;

private:
  apex::hal::IFlash& flash_;
  uint8_t keyCache_[TOTAL_KEY_DATA];
  uint16_t bitmap_;
  uint8_t populatedCount_;

  void scanBitmap() noexcept;
  KeyStoreStatus rewriteAllSlots() noexcept;
};

} // namespace encryptor

#endif // APEX_PICO_ENCRYPTOR_KEY_STORE_HPP
