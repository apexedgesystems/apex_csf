/**
 * @file FloorProbe.cpp
 * @brief MCU-floor probe: instantiates the library's public surface; compiled
 *        at C++17 with -fno-exceptions -fno-rtti by apex_add_floor_check so a
 *        hosted-only regression fails every PR build, not the next firmware
 *        build.
 */

#include "src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp"

using namespace apex::encryption::mcu;

uint32_t probe() {
  static const uint8_t KEY[AES256_KEY_LEN] = {0x42};
  static const uint8_t NONCE[GCM_NONCE_LEN] = {0x24};
  static const uint8_t PLAIN[16] = {'f', 'l', 'o', 'o', 'r'};

  uint8_t cipher[sizeof(PLAIN)];
  uint8_t tag[GCM_TAG_LEN];
  GcmResult enc = aes256GcmEncrypt(KEY, NONCE, PLAIN, sizeof(PLAIN), nullptr, 0, cipher, tag);

  uint8_t roundTrip[sizeof(PLAIN)];
  GcmResult dec = aes256GcmDecrypt(KEY, NONCE, cipher, sizeof(cipher), nullptr, 0, tag, roundTrip);

  return enc.bytesWritten + dec.bytesWritten + static_cast<uint32_t>(enc.status == GcmStatus::OK) +
         static_cast<uint32_t>(dec.status);
}
