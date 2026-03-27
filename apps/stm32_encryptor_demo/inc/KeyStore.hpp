#ifndef APEX_STM32_ENCRYPTOR_KEY_STORE_HPP
#define APEX_STM32_ENCRYPTOR_KEY_STORE_HPP
/**
 * @file KeyStore.hpp
 * @brief Flash-backed AES-256 key storage for the stm32_encryptor_demo.
 *
 * Manages 16 key slots (32 bytes each) on a dedicated flash page. Keys
 * are cached in RAM for RT-safe read access. Write and erase operations
 * are blocking (flash programming stalls the CPU bus).
 *
 * Flash layout (page 510, 0x080FF000):
 *   Slot  0: bytes 0x000-0x01F  (32 bytes)
 *   Slot  1: bytes 0x020-0x03F  (32 bytes)
 *   ...
 *   Slot 15: bytes 0x1E0-0x1FF  (32 bytes)
 *   Unused:  bytes 0x200-0x7FF  (1536 bytes reserved)
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

/**
 * @brief Status codes for key store operations.
 */
enum class KeyStoreStatus : uint8_t {
  OK = 0,             ///< Operation succeeded.
  ERROR_FLASH_INIT,   ///< Flash initialization failed.
  ERROR_FLASH_READ,   ///< Flash read failed.
  ERROR_FLASH_WRITE,  ///< Flash write/program failed.
  ERROR_FLASH_ERASE,  ///< Flash page erase failed.
  ERROR_INVALID_SLOT, ///< Slot index >= KEY_SLOT_COUNT.
  ERROR_SLOT_EMPTY    ///< Requested slot contains no key (all 0xFF).
};

/* ----------------------------- KeyStore ----------------------------- */

/**
 * @class KeyStore
 * @brief Flash-persistent key storage with RAM cache.
 *
 * Lifecycle:
 *  1. Construct with IFlash reference
 *  2. Call init() to read flash page and scan populated slots
 *  3. Call writeKey()/readKey()/eraseAll() as needed
 *  4. Call selectKey() to pick a key for encryption
 *
 * @note RT-safe methods: readKey(), selectKey(), populatedCount(), bitmap(),
 *       isSlotPopulated().
 * @note NOT RT-safe: init(), writeKey(), eraseAll().
 */
class KeyStore {
public:
  /// Flash page for key storage (page 510 on STM32L476).
  static constexpr uint32_t KEY_STORE_PAGE = 510;

  /// Size of each key slot in bytes.
  static constexpr size_t SLOT_SIZE = AES_KEY_LEN;

  /// Total key data size on flash (KEY_SLOT_COUNT * SLOT_SIZE).
  static constexpr size_t TOTAL_KEY_DATA = KEY_SLOT_COUNT * SLOT_SIZE;

  /**
   * @brief Construct key store bound to a flash peripheral.
   * @param flash Flash interface (must outlive this object).
   */
  explicit KeyStore(apex::hal::IFlash& flash) noexcept;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Initialize flash and load key cache from page 510.
   *
   * Calls flash.init(), reads all slot data into RAM cache, and scans
   * the bitmap for populated slots.
   *
   * @return OK on success, ERROR_FLASH_INIT or ERROR_FLASH_READ on failure.
   * @note NOT RT-safe: flash init unlocks flash hardware.
   */
  KeyStoreStatus init() noexcept;

  /* ----------------------------- Key Operations ----------------------------- */

  /**
   * @brief Write a 32-byte key to a slot.
   *
   * If the slot is empty (0xFF), writes directly (4 double-word writes).
   * If the slot is populated, erases the page and rewrites all populated
   * slots from the RAM cache.
   *
   * @param slot Slot index (0-15).
   * @param key 32-byte AES-256 key to write. Must not be null.
   * @return OK on success, ERROR_INVALID_SLOT if slot >= 16,
   *         ERROR_FLASH_WRITE or ERROR_FLASH_ERASE on hardware error.
   * @note NOT RT-safe: flash write/erase stalls CPU.
   */
  KeyStoreStatus writeKey(uint8_t slot, const uint8_t* key) noexcept;

  /**
   * @brief Read a key from a slot (from RAM cache).
   *
   * @param slot Slot index (0-15).
   * @param keyOut Output buffer (must be >= 32 bytes).
   * @return OK on success, ERROR_INVALID_SLOT if out of range,
   *         ERROR_SLOT_EMPTY if the slot has no key.
   * @note RT-safe: reads from RAM cache.
   */
  KeyStoreStatus readKey(uint8_t slot, uint8_t* keyOut) const noexcept;

  /**
   * @brief Erase all keys (full page erase).
   *
   * Erases flash page 510 and clears the RAM cache. All slots become empty.
   *
   * @return OK on success, ERROR_FLASH_ERASE on hardware error.
   * @note NOT RT-safe: page erase blocks ~25 ms.
   */
  KeyStoreStatus eraseAll() noexcept;

  /* ----------------------------- Query ----------------------------- */

  /**
   * @brief Get number of populated key slots.
   * @return Count (0-16).
   * @note RT-safe.
   */
  uint8_t populatedCount() const noexcept { return populatedCount_; }

  /**
   * @brief Get bitmap of populated slots.
   * @return 16-bit bitmap. Bit N set = slot N has a key.
   * @note RT-safe.
   */
  uint16_t bitmap() const noexcept { return bitmap_; }

  /**
   * @brief Check if a slot contains a key.
   * @param slot Slot index (0-15).
   * @return true if populated, false if empty or out of range.
   * @note RT-safe.
   */
  bool isSlotPopulated(uint8_t slot) const noexcept;

  /* ----------------------------- Key Selection ----------------------------- */

  /**
   * @brief Select a key for encryption.
   *
   * Returns the first populated slot. RANDOM mode rotates through all
   * populated slots; LOCKED mode uses the pinned slot index.
   *
   * @param keyOut Output buffer for 32-byte key.
   * @param slotOut Output: slot index of the selected key.
   * @return OK on success, ERROR_SLOT_EMPTY if no keys are provisioned.
   * @note RT-safe: reads from RAM cache.
   */
  KeyStoreStatus selectKey(uint8_t* keyOut, uint8_t& slotOut) const noexcept;

private:
  apex::hal::IFlash& flash_;

  /// RAM cache of all 16 key slots (512 bytes).
  uint8_t keyCache_[TOTAL_KEY_DATA];

  /// Bitmap of populated slots (bit N = slot N).
  uint16_t bitmap_;

  /// Count of populated slots.
  uint8_t populatedCount_;

  /**
   * @brief Scan the key cache and rebuild the bitmap.
   *
   * A slot is populated if any of its 32 bytes is not 0xFF.
   */
  void scanBitmap() noexcept;

  /**
   * @brief Erase the key page and rewrite all populated slots from cache.
   * @return OK on success, ERROR_FLASH_ERASE or ERROR_FLASH_WRITE on failure.
   */
  KeyStoreStatus rewriteAllSlots() noexcept;
};

} // namespace encryptor

#endif // APEX_STM32_ENCRYPTOR_KEY_STORE_HPP
