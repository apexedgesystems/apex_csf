/**
 * @file EncryptionMcu_pTest.cpp
 * @brief Performance tests for AES-256-GCM MCU (software, no OpenSSL).
 *
 * Measures:
 *  - AES-256-GCM encrypt throughput (software, no AES-NI)
 *  - AES-256-GCM decrypt throughput (software, no AES-NI)
 *
 * Usage:
 *   ./EncryptionMcu_PTEST --csv results.csv
 *   ./EncryptionMcu_PTEST --quick
 *   ./EncryptionMcu_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/utilities/encryption/mcu/inc/Aes256GcmMcu.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

namespace ub = vernier::bench;
namespace mcu = apex::encryption::mcu;

/* ----------------------------- Test Data ----------------------------- */

static std::vector<uint8_t> generateData(size_t size, unsigned seed = 42) {
  std::vector<uint8_t> data(size);
  std::mt19937 gen(seed);
  std::uniform_int_distribution<unsigned int> dist(0, 255);
  for (auto& b : data) {
    b = static_cast<uint8_t>(dist(gen));
  }
  return data;
}

/* ----------------------------- Encrypt/Decrypt Throughput ----------------------------- */

/**
 * @brief AES-256-GCM MCU encrypt throughput at 256B payload.
 *
 * Tests the software AES path (no AES-NI) used on bare-metal targets.
 */
PERF_TEST(Aes256GcmMcu, EncryptThroughput) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD = 256;
  auto plaintext = generateData(PAYLOAD);
  auto aad = generateData(16);

  std::array<uint8_t, mcu::AES256_KEY_LEN> key{};
  for (size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<uint8_t>(i ^ 0xAB);
  }
  std::array<uint8_t, mcu::GCM_NONCE_LEN> nonce{};
  for (size_t i = 0; i < nonce.size(); ++i) {
    nonce[i] = static_cast<uint8_t>(i ^ 0xCD);
  }

  std::vector<uint8_t> ciphertext(PAYLOAD);
  std::array<uint8_t, mcu::GCM_TAG_LEN> tag{};

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      mcu::aes256GcmEncrypt(key.data(), nonce.data(), aad.data(),
                            static_cast<uint32_t>(aad.size()), plaintext.data(),
                            static_cast<uint32_t>(plaintext.size()), ciphertext.data(), tag.data());
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        mcu::aes256GcmEncrypt(key.data(), nonce.data(), aad.data(),
                              static_cast<uint32_t>(aad.size()), plaintext.data(),
                              static_cast<uint32_t>(plaintext.size()), ciphertext.data(),
                              tag.data());
      },
      "encrypt-256B");

  double mbps = (PAYLOAD * result.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("\nAES-256-GCM MCU encrypt (256B): %.0f ops/s  %.1f MB/s  (%.1f us/call)\n",
              result.callsPerSecond, mbps, result.stats.median);
}

/**
 * @brief AES-256-GCM MCU decrypt throughput at 256B payload.
 */
PERF_TEST(Aes256GcmMcu, DecryptThroughput) {
  UB_PERF_GUARD(perf);

  constexpr size_t PAYLOAD = 256;
  auto plaintext = generateData(PAYLOAD);
  auto aad = generateData(16);

  std::array<uint8_t, mcu::AES256_KEY_LEN> key{};
  for (size_t i = 0; i < key.size(); ++i) {
    key[i] = static_cast<uint8_t>(i ^ 0xAB);
  }
  std::array<uint8_t, mcu::GCM_NONCE_LEN> nonce{};
  for (size_t i = 0; i < nonce.size(); ++i) {
    nonce[i] = static_cast<uint8_t>(i ^ 0xCD);
  }

  // Encrypt first to get valid ciphertext + tag
  std::vector<uint8_t> ciphertext(PAYLOAD);
  std::array<uint8_t, mcu::GCM_TAG_LEN> tag{};
  mcu::aes256GcmEncrypt(key.data(), nonce.data(), aad.data(), static_cast<uint32_t>(aad.size()),
                        plaintext.data(), static_cast<uint32_t>(plaintext.size()),
                        ciphertext.data(), tag.data());

  std::vector<uint8_t> decrypted(PAYLOAD);

  perf.warmup([&] {
    for (int i = 0; i < perf.cycles(); ++i) {
      mcu::aes256GcmDecrypt(key.data(), nonce.data(), aad.data(),
                            static_cast<uint32_t>(aad.size()), ciphertext.data(),
                            static_cast<uint32_t>(ciphertext.size()), tag.data(), decrypted.data());
    }
  });

  auto result = perf.throughputLoop(
      [&] {
        mcu::aes256GcmDecrypt(key.data(), nonce.data(), aad.data(),
                              static_cast<uint32_t>(aad.size()), ciphertext.data(),
                              static_cast<uint32_t>(ciphertext.size()), tag.data(),
                              decrypted.data());
      },
      "decrypt-256B");

  double mbps = (PAYLOAD * result.callsPerSecond) / (1024.0 * 1024.0);
  std::printf("\nAES-256-GCM MCU decrypt (256B): %.0f ops/s  %.1f MB/s  (%.1f us/call)\n",
              result.callsPerSecond, mbps, result.stats.median);
}

PERF_MAIN()
