/**
 * @file KeyStore.cpp
 * @brief EEPROM-backed key storage implementation (Arduino version).
 *
 * Keys are stored in ATmega328P EEPROM starting at address 0. Each of the
 * 4 slots occupies 32 bytes (one AES-256 key). No RAM cache -- keys are
 * read directly from EEPROM on demand. Only the bitmap and populated count
 * are held in RAM (3 bytes).
 *
 * Unlike the STM32 version, writes go directly to EEPROM without page
 * erase since EEPROM is byte-granular (eeprom_update_block skips unchanged
 * bytes to reduce wear).
 */

#include "KeyStore.hpp"

#include <string.h>

namespace encryptor {

/* ----------------------------- Constants ----------------------------- */

/// Value for empty EEPROM bytes (erased state).
static constexpr uint8_t EEPROM_ERASED = 0xFF;

/* ----------------------------- File Helpers ----------------------------- */

/**
 * @brief Check if a 32-byte region is all 0xFF (empty/erased).
 */
static bool isSlotErased(const uint8_t* data) noexcept {
  for (size_t i = 0; i < KeyStore::SLOT_SIZE; ++i) {
    if (data[i] != EEPROM_ERASED) {
      return false;
    }
  }
  return true;
}

/* ----------------------------- KeyStore Methods ----------------------------- */

KeyStore::KeyStore(apex::hal::IFlash& flash) noexcept
    : flash_(flash), bitmap_(0), populatedCount_(0) {}

KeyStoreStatus KeyStore::init() noexcept {
  auto status = flash_.init();
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_INIT;
  }

  scanBitmap();
  return KeyStoreStatus::OK;
}

KeyStoreStatus KeyStore::writeKey(uint8_t slot, const uint8_t* key) noexcept {
  if (slot >= KEY_SLOT_COUNT) {
    return KeyStoreStatus::ERROR_INVALID_SLOT;
  }

  const size_t OFFSET = static_cast<size_t>(slot) * SLOT_SIZE;

  // Write directly to EEPROM (eeprom_update_block skips unchanged bytes)
  auto status = flash_.write(static_cast<uint32_t>(OFFSET), key, SLOT_SIZE);
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_WRITE;
  }

  // Update bitmap
  bitmap_ |= static_cast<uint8_t>(1U << slot);
  populatedCount_ = 0;
  for (uint8_t i = 0; i < KEY_SLOT_COUNT; ++i) {
    if (bitmap_ & (1U << i)) {
      ++populatedCount_;
    }
  }

  return KeyStoreStatus::OK;
}

KeyStoreStatus KeyStore::readKey(uint8_t slot, uint8_t* keyOut) const noexcept {
  if (slot >= KEY_SLOT_COUNT) {
    return KeyStoreStatus::ERROR_INVALID_SLOT;
  }

  if (!isSlotPopulated(slot)) {
    return KeyStoreStatus::ERROR_SLOT_EMPTY;
  }

  // Read directly from EEPROM (~8 us for 32 bytes at 16 MHz)
  const size_t OFFSET = static_cast<size_t>(slot) * SLOT_SIZE;
  auto status = flash_.read(static_cast<uint32_t>(OFFSET), keyOut, SLOT_SIZE);
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_READ;
  }

  return KeyStoreStatus::OK;
}

KeyStoreStatus KeyStore::eraseAll() noexcept {
  auto status = flash_.erasePage(KEY_STORE_PAGE);
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_ERASE;
  }

  bitmap_ = 0;
  populatedCount_ = 0;

  return KeyStoreStatus::OK;
}

bool KeyStore::isSlotPopulated(uint8_t slot) const noexcept {
  if (slot >= KEY_SLOT_COUNT) {
    return false;
  }
  return (bitmap_ & (1U << slot)) != 0;
}

KeyStoreStatus KeyStore::selectKey(uint8_t* keyOut, uint8_t& slotOut) const noexcept {
  if (populatedCount_ == 0) {
    return KeyStoreStatus::ERROR_SLOT_EMPTY;
  }

  for (uint8_t i = 0; i < KEY_SLOT_COUNT; ++i) {
    if (bitmap_ & (1U << i)) {
      const size_t OFFSET = static_cast<size_t>(i) * SLOT_SIZE;
      auto status = flash_.read(static_cast<uint32_t>(OFFSET), keyOut, SLOT_SIZE);
      if (status != apex::hal::FlashStatus::OK) {
        return KeyStoreStatus::ERROR_FLASH_READ;
      }
      slotOut = i;
      return KeyStoreStatus::OK;
    }
  }

  return KeyStoreStatus::ERROR_SLOT_EMPTY;
}

void KeyStore::scanBitmap() noexcept {
  bitmap_ = 0;
  populatedCount_ = 0;

  uint8_t slotBuf[SLOT_SIZE];
  for (uint8_t i = 0; i < KEY_SLOT_COUNT; ++i) {
    const size_t OFFSET = static_cast<size_t>(i) * SLOT_SIZE;
    auto status = flash_.read(static_cast<uint32_t>(OFFSET), slotBuf, SLOT_SIZE);
    if (status != apex::hal::FlashStatus::OK) {
      continue;
    }
    if (!isSlotErased(slotBuf)) {
      bitmap_ |= static_cast<uint8_t>(1U << i);
      ++populatedCount_;
    }
  }
}

} // namespace encryptor
