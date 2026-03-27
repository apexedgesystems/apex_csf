/**
 * @file Interfaces_uTest.cpp
 * @brief Unit tests for runtime-polymorphic encryption interfaces.
 *
 * Notes:
 *  - Tests cover IHasher, IMac, ICipher, and IAead adapters.
 *  - Verifies runtime algorithm swapping and roundtrip encryption.
 */

#include "src/utilities/encryption/openssl/inc/IHasher.hpp"
#include "src/utilities/encryption/openssl/inc/IMac.hpp"
#include "src/utilities/encryption/openssl/inc/ICipher.hpp"
#include "src/utilities/encryption/openssl/inc/IAead.hpp"

#include "src/utilities/encryption/openssl/inc/Sha256Hash.hpp"
#include "src/utilities/encryption/openssl/inc/Sha512Hash.hpp"
#include "src/utilities/encryption/openssl/inc/HmacSha256.hpp"
#include "src/utilities/encryption/openssl/inc/Aes256Cbc.hpp"
#include "src/utilities/encryption/openssl/inc/Aes256Gcm.hpp"

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

using apex::encryption::AeadAdapter;
using apex::encryption::Aes256Cbc;
using apex::encryption::Aes256Gcm;
using apex::encryption::CipherAdapter;
using apex::encryption::HasherAdapter;
using apex::encryption::HmacSha256;
using apex::encryption::IAead;
using apex::encryption::ICipher;
using apex::encryption::IHasher;
using apex::encryption::IMac;
using apex::encryption::MacAdapter;
using apex::encryption::Sha256Hash;
using apex::encryption::Sha512Hash;

/* ----------------------------- IHasher Tests ----------------------------- */

/** @test Verify HasherAdapter<Sha256Hash> works through IHasher interface. */
TEST(IHasherTest, Sha256AdapterWorks) {
  std::unique_ptr<IHasher> hasher = std::make_unique<HasherAdapter<Sha256Hash>>();

  EXPECT_EQ(hasher->digestSize(), 32U);
  EXPECT_STREQ(hasher->algorithmName(), "SHA256");

  std::vector<uint8_t> message = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
  std::vector<uint8_t> digest;

  const auto STATUS = hasher->hash(message, digest);

  EXPECT_EQ(STATUS, IHasher::Status::SUCCESS);
  EXPECT_EQ(digest.size(), 32U);
}

/** @test Verify HasherAdapter<Sha512Hash> works through IHasher interface. */
TEST(IHasherTest, Sha512AdapterWorks) {
  std::unique_ptr<IHasher> hasher = std::make_unique<HasherAdapter<Sha512Hash>>();

  EXPECT_EQ(hasher->digestSize(), 64U);
  EXPECT_STREQ(hasher->algorithmName(), "SHA512");

  std::vector<uint8_t> message = {0x48, 0x65, 0x6c, 0x6c, 0x6f};
  std::vector<uint8_t> digest;

  const auto STATUS = hasher->hash(message, digest);

  EXPECT_EQ(STATUS, IHasher::Status::SUCCESS);
  EXPECT_EQ(digest.size(), 64U);
}

/** @test Verify runtime algorithm swap via IHasher pointer. */
TEST(IHasherTest, RuntimeAlgorithmSwap) {
  std::vector<uint8_t> message = {0x01, 0x02, 0x03};
  std::vector<uint8_t> digest1, digest2;

  std::unique_ptr<IHasher> hasher = std::make_unique<HasherAdapter<Sha256Hash>>();
  const auto ST_256 = hasher->hash(message, digest1);
  EXPECT_EQ(ST_256, IHasher::Status::SUCCESS);
  EXPECT_EQ(digest1.size(), 32U);

  hasher = std::make_unique<HasherAdapter<Sha512Hash>>();
  const auto ST_512 = hasher->hash(message, digest2);
  EXPECT_EQ(ST_512, IHasher::Status::SUCCESS);
  EXPECT_EQ(digest2.size(), 64U);
}

/* ----------------------------- IMac Tests ----------------------------- */

/** @test Verify MacAdapter<HmacSha256> works through IMac interface. */
TEST(IMacTest, HmacSha256AdapterWorks) {
  std::unique_ptr<IMac> mac = std::make_unique<MacAdapter<HmacSha256>>();

  EXPECT_EQ(mac->keySize(), 32U);
  EXPECT_EQ(mac->digestSize(), 32U);

  std::array<uint8_t, 32> key{};
  std::fill(key.begin(), key.end(), 0xAB);
  mac->setKey(key);

  std::vector<uint8_t> message = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> tag;

  const auto STATUS = mac->mac(message, tag);

  EXPECT_EQ(STATUS, IMac::Status::SUCCESS);
  EXPECT_EQ(tag.size(), 32U);
}

/* ----------------------------- ICipher Tests ----------------------------- */

/** @test Verify CipherAdapter<Aes256Cbc> works through ICipher interface. */
TEST(ICipherTest, Aes256CbcAdapterWorks) {
  std::unique_ptr<ICipher> cipher = std::make_unique<CipherAdapter<Aes256Cbc>>();

  EXPECT_EQ(cipher->keySize(), 32U);
  EXPECT_EQ(cipher->ivSize(), 16U);
  EXPECT_EQ(cipher->blockSize(), 16U);

  std::array<uint8_t, 32> key{};
  std::array<uint8_t, 16> iv{};
  std::fill(key.begin(), key.end(), 0x11);
  std::fill(iv.begin(), iv.end(), 0x22);

  cipher->setKey(key);
  cipher->setIv(iv);

  std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
  std::vector<uint8_t> ciphertext;

  const auto STATUS = cipher->encrypt(plaintext, ciphertext);

  EXPECT_EQ(STATUS, ICipher::Status::SUCCESS);
  EXPECT_GE(ciphertext.size(), plaintext.size());
}

/* ----------------------------- IAead Tests ----------------------------- */

/** @test Verify AeadAdapter<Aes256Gcm> works through IAead interface. */
TEST(IAeadTest, Aes256GcmAdapterWorks) {
  std::unique_ptr<IAead> aead = std::make_unique<AeadAdapter<Aes256Gcm>>();

  EXPECT_EQ(aead->keySize(), 32U);
  EXPECT_EQ(aead->ivSize(), 12U);
  EXPECT_EQ(aead->tagSize(), 16U);

  std::array<uint8_t, 32> key{};
  std::array<uint8_t, 12> iv{};
  std::fill(key.begin(), key.end(), 0x33);
  std::fill(iv.begin(), iv.end(), 0x44);

  aead->setKey(key);
  aead->setIv(iv);

  std::vector<uint8_t> plaintext = {0x01, 0x02, 0x03, 0x04};
  std::vector<uint8_t> aad = {0xAA, 0xBB};
  std::vector<uint8_t> ciphertext, tag;

  const auto STATUS = aead->encrypt(plaintext, aad, ciphertext, tag);

  EXPECT_EQ(STATUS, IAead::Status::SUCCESS);
  EXPECT_EQ(ciphertext.size(), plaintext.size());
  EXPECT_EQ(tag.size(), 16U);
}

/** @test Verify AEAD encrypt/decrypt roundtrip through interface. */
TEST(IAeadTest, EncryptDecryptRoundtrip) {
  std::unique_ptr<IAead> aead = std::make_unique<AeadAdapter<Aes256Gcm>>();

  std::array<uint8_t, 32> key{};
  std::array<uint8_t, 12> iv{};
  std::fill(key.begin(), key.end(), 0x55);
  std::fill(iv.begin(), iv.end(), 0x66);

  aead->setKey(key);
  aead->setIv(iv);

  std::vector<uint8_t> original = {0xDE, 0xAD, 0xBE, 0xEF};
  std::vector<uint8_t> aad = {0x01, 0x02, 0x03};
  std::vector<uint8_t> ciphertext, tag, decrypted;

  const auto ENC_STATUS = aead->encrypt(original, aad, ciphertext, tag);
  ASSERT_EQ(ENC_STATUS, IAead::Status::SUCCESS);

  const auto DEC_STATUS = aead->decrypt(ciphertext, aad, decrypted, tag);
  ASSERT_EQ(DEC_STATUS, IAead::Status::SUCCESS);

  EXPECT_EQ(decrypted, original);
}
