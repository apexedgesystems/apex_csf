#ifndef APEX_ARDUINO_ENCRYPTOR_KEY_STORE_HPP
#define APEX_ARDUINO_ENCRYPTOR_KEY_STORE_HPP
/**
 * @file KeyStore.hpp
 * @brief EEPROM-backed AES-256 key storage for the arduino_encryptor_demo.
 *
 * Manages 4 key slots (32 bytes each) on the ATmega328P's 1 KB EEPROM.
 * No RAM cache -- keys are read directly from EEPROM on demand. EEPROM
 * reads are fast (~4 cycles/byte = 8 us for 32 bytes at 16 MHz) so this
 * is acceptable within a 10 ms tick budget.
 *
 * Only the bitmap and populated count are cached in RAM (3 bytes total).
 *
 * EEPROM layout (byte addresses):
 *   Slot 0: bytes 0x000-0x01F  (32 bytes)
 *   Slot 1: bytes 0x020-0x03F  (32 bytes)
 *   Slot 2: bytes 0x040-0x05F  (32 bytes)
 *   Slot 3: bytes 0x060-0x07F  (32 bytes)
 *   Unused: bytes 0x080-0x3FF  (896 bytes reserved)
 *
 * Slot detection: empty = all 0xFF (EEPROM erased state).
 *
 * @note Write/erase are NOT RT-safe (~3.3 ms per byte on EEPROM).
 * @note Read/select are RT-safe (EEPROM reads are ~8 us for 32 bytes).
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
 * @brief EEPROM-persistent key storage (no RAM cache).
 *
 * Lifecycle:
 *  1. Construct with IFlash reference (AvrEeprom)
 *  2. Call init() to scan EEPROM for populated slots
 *  3. Call writeKey()/readKey()/eraseAll() as needed
 *  4. Call selectKey() to pick a key for encryption
 *
 * @note RT-safe methods: readKey(), selectKey(), populatedCount(), bitmap(),
 *       isSlotPopulated().
 * @note NOT RT-safe: init(), writeKey(), eraseAll().
 */
class KeyStore {
public:
  /// EEPROM page index for key storage (page 0 -- EEPROM is a single page).
  static constexpr uint32_t KEY_STORE_PAGE = 0;

  /// Size of each key slot in bytes.
  static constexpr size_t SLOT_SIZE = AES_KEY_LEN;

  /// Total key data size (KEY_SLOT_COUNT * SLOT_SIZE).
  static constexpr size_t TOTAL_KEY_DATA = KEY_SLOT_COUNT * SLOT_SIZE;

  /**
   * @brief Construct key store bound to an EEPROM flash peripheral.
   * @param flash Flash interface (AvrEeprom, must outlive this object).
   */
  explicit KeyStore(apex::hal::IFlash& flash) noexcept;

  /* ----------------------------- Lifecycle ----------------------------- */

  /**
   * @brief Initialize EEPROM and scan populated slots.
   * @return OK on success, ERROR_FLASH_INIT or ERROR_FLASH_READ on failure.
   * @note NOT RT-safe: EEPROM read.
   */
  KeyStoreStatus init() noexcept;

  /* ----------------------------- Key Operations ----------------------------- */

  /**
   * @brief Write a 32-byte key to a slot.
   *
   * Writes directly to EEPROM (eeprom_update_block skips unchanged bytes).
   * No page erase needed -- EEPROM is byte-granular.
   *
   * @param slot Slot index (0-3).
   * @param key 32-byte AES-256 key to write.
   * @return OK on success, ERROR_INVALID_SLOT if slot >= KEY_SLOT_COUNT.
   * @note NOT RT-safe: EEPROM write ~3.3 ms/byte.
   */
  KeyStoreStatus writeKey(uint8_t slot, const uint8_t* key) noexcept;

  /**
   * @brief Read a key from a slot (directly from EEPROM).
   * @param slot Slot index (0-3).
   * @param keyOut Output buffer (must be >= 32 bytes).
   * @return OK on success, ERROR_SLOT_EMPTY if the slot has no key.
   * @note RT-safe: EEPROM read is ~8 us for 32 bytes.
   */
  KeyStoreStatus readKey(uint8_t slot, uint8_t* keyOut) const noexcept;

  /**
   * @brief Erase all keys (write 0xFF to all slot bytes).
   * @return OK on success, ERROR_FLASH_ERASE on hardware error.
   * @note NOT RT-safe: EEPROM write.
   */
  KeyStoreStatus eraseAll() noexcept;

  /* ----------------------------- Query ----------------------------- */

  uint8_t populatedCount() const noexcept { return populatedCount_; }
  uint8_t bitmap() const noexcept { return bitmap_; }
  bool isSlotPopulated(uint8_t slot) const noexcept;

  /* ----------------------------- Key Selection ----------------------------- */

  /**
   * @brief Select a key for encryption (returns first populated slot).
   * @param keyOut Output buffer for 32-byte key.
   * @param slotOut Output: slot index of the selected key.
   * @return OK on success, ERROR_SLOT_EMPTY if no keys are provisioned.
   * @note RT-safe: EEPROM read is ~8 us for 32 bytes.
   */
  KeyStoreStatus selectKey(uint8_t* keyOut, uint8_t& slotOut) const noexcept;

private:
  apex::hal::IFlash& flash_;

  uint8_t bitmap_;
  uint8_t populatedCount_;

  void scanBitmap() noexcept;
};

} // namespace encryptor

#endif // APEX_ARDUINO_ENCRYPTOR_KEY_STORE_HPP
