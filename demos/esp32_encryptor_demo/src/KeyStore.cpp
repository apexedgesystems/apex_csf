/**
 * @file KeyStore.cpp
 * @brief NVS-backed key storage implementation for ESP32-S3.
 *
 * Keys are stored in NVS as a single blob under namespace "encryptor",
 * key "keys". The RAM cache mirrors NVS contents and is used for all
 * reads and key selection.
 */

#include "KeyStore.hpp"

#include <string.h>

namespace encryptor {

/* ----------------------------- Constants ----------------------------- */

static constexpr uint8_t FLASH_ERASED = 0xFF;

/* ----------------------------- File Helpers ----------------------------- */

static bool isSlotErased(const uint8_t* data) noexcept {
  for (size_t i = 0; i < KeyStore::SLOT_SIZE; ++i) {
    if (data[i] != FLASH_ERASED) {
      return false;
    }
  }
  return true;
}

/* ----------------------------- KeyStore Methods ----------------------------- */

KeyStore::KeyStore(apex::hal::IFlash& flash) noexcept
    : flash_(flash), keyCache_{}, bitmap_(0), populatedCount_(0) {
  memset(keyCache_, FLASH_ERASED, sizeof(keyCache_));
}

KeyStoreStatus KeyStore::init() noexcept {
  auto status = flash_.init();
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_INIT;
  }

  const uint32_t ADDR = flash_.addressForPage(KEY_STORE_PAGE);
  status = flash_.read(ADDR, keyCache_, TOTAL_KEY_DATA);
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_READ;
  }

  scanBitmap();

  return KeyStoreStatus::OK;
}

KeyStoreStatus KeyStore::writeKey(uint8_t slot, const uint8_t* key) noexcept {
  if (slot >= KEY_SLOT_COUNT) {
    return KeyStoreStatus::ERROR_INVALID_SLOT;
  }

  const size_t OFFSET = static_cast<size_t>(slot) * SLOT_SIZE;
  const bool WAS_EMPTY = !isSlotPopulated(slot);

  memcpy(keyCache_ + OFFSET, key, SLOT_SIZE);

  if (WAS_EMPTY) {
    const uint32_t ADDR = flash_.addressForPage(KEY_STORE_PAGE) + OFFSET;
    auto status = flash_.write(ADDR, key, SLOT_SIZE);
    if (status != apex::hal::FlashStatus::OK) {
      return KeyStoreStatus::ERROR_FLASH_WRITE;
    }
  } else {
    auto ks = rewriteAllSlots();
    if (ks != KeyStoreStatus::OK) {
      return ks;
    }
  }

  bitmap_ |= static_cast<uint16_t>(1U << slot);
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

  const size_t OFFSET = static_cast<size_t>(slot) * SLOT_SIZE;
  memcpy(keyOut, keyCache_ + OFFSET, SLOT_SIZE);

  return KeyStoreStatus::OK;
}

KeyStoreStatus KeyStore::eraseAll() noexcept {
  auto status = flash_.erasePage(KEY_STORE_PAGE);
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_ERASE;
  }

  memset(keyCache_, FLASH_ERASED, sizeof(keyCache_));
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
      memcpy(keyOut, keyCache_ + OFFSET, SLOT_SIZE);
      slotOut = i;
      return KeyStoreStatus::OK;
    }
  }

  return KeyStoreStatus::ERROR_SLOT_EMPTY;
}

void KeyStore::scanBitmap() noexcept {
  bitmap_ = 0;
  populatedCount_ = 0;

  for (uint8_t i = 0; i < KEY_SLOT_COUNT; ++i) {
    const size_t OFFSET = static_cast<size_t>(i) * SLOT_SIZE;
    if (!isSlotErased(keyCache_ + OFFSET)) {
      bitmap_ |= static_cast<uint16_t>(1U << i);
      ++populatedCount_;
    }
  }
}

KeyStoreStatus KeyStore::rewriteAllSlots() noexcept {
  auto status = flash_.erasePage(KEY_STORE_PAGE);
  if (status != apex::hal::FlashStatus::OK) {
    return KeyStoreStatus::ERROR_FLASH_ERASE;
  }

  const uint32_t BASE_ADDR = flash_.addressForPage(KEY_STORE_PAGE);

  for (uint8_t i = 0; i < KEY_SLOT_COUNT; ++i) {
    const size_t OFFSET = static_cast<size_t>(i) * SLOT_SIZE;
    if (!isSlotErased(keyCache_ + OFFSET)) {
      status = flash_.write(BASE_ADDR + OFFSET, keyCache_ + OFFSET, SLOT_SIZE);
      if (status != apex::hal::FlashStatus::OK) {
        return KeyStoreStatus::ERROR_FLASH_WRITE;
      }
    }
  }

  return KeyStoreStatus::OK;
}

} // namespace encryptor
