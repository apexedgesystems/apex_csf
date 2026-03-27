/**
 * @file Aes128Ccm.cpp
 * @brief AES-128-CCM AEAD cipher implementation.
 */

#include "src/utilities/encryption/openssl/inc/Aes128Ccm.hpp"

#include <openssl/evp.h>

namespace apex::encryption {

/* ----------------------------- Aes128Ccm Methods ----------------------------- */

Aes128Ccm::Aes128Ccm() : ctx_(EVP_CIPHER_CTX_new()) {}

Aes128Ccm::~Aes128Ccm() {
  if (ctx_) {
    EVP_CIPHER_CTX_free(ctx_);
    ctx_ = nullptr;
  }
}

Aes128Ccm::Aes128Ccm(Aes128Ccm&& other) noexcept : ctx_(other.ctx_) { other.ctx_ = nullptr; }

Aes128Ccm& Aes128Ccm::operator=(Aes128Ccm&& other) noexcept {
  if (this != &other) {
    if (ctx_) {
      EVP_CIPHER_CTX_free(ctx_);
    }
    ctx_ = other.ctx_;
    other.ctx_ = nullptr;
  }
  return *this;
}

Aes128Ccm::Status Aes128Ccm::encrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                     bytes_span nonce, std::size_t tagLength,
                                     std::vector<std::uint8_t>& outCipher,
                                     std::vector<std::uint8_t>& outTag) noexcept {

  if (!ctx_) {
    return Status::ERROR_NULL_ALGORITHM;
  }

  // Validate key length
  if (key.size() != KEY_LENGTH) {
    return Status::ERROR_INVALID_KEY;
  }

  // Validate nonce length (7-13 bytes)
  if (nonce.size() < 7 || nonce.size() > 13) {
    return Status::ERROR_INVALID_NONCE;
  }

  // Validate tag length (4, 6, 8, 10, 12, 14, or 16)
  if (tagLength < 4 || tagLength > 16 || (tagLength % 2 != 0)) {
    return Status::ERROR_INVALID_TAG_LENGTH;
  }

  // Initialize cipher
  if (EVP_EncryptInit_ex(ctx_, EVP_aes_128_ccm(), nullptr, nullptr, nullptr) != 1) {
    return Status::ERROR_INIT;
  }

  // Set nonce length (must be done before setting key/nonce)
  if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_CCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) !=
      1) {
    return Status::ERROR_INIT;
  }

  // Set tag length
  if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_CCM_SET_TAG, static_cast<int>(tagLength), nullptr) != 1) {
    return Status::ERROR_INIT;
  }

  // Set key and nonce
  if (EVP_EncryptInit_ex(ctx_, nullptr, nullptr, key.data(), nonce.data()) != 1) {
    return Status::ERROR_INIT;
  }

  // Provide plaintext length (required for CCM)
  int outl = 0;
  if (EVP_EncryptUpdate(ctx_, nullptr, &outl, nullptr, static_cast<int>(plaintext.size())) != 1) {
    return Status::ERROR_UPDATE;
  }

  // Provide AAD if present
  if (!aad.empty()) {
    if (EVP_EncryptUpdate(ctx_, nullptr, &outl, aad.data(), static_cast<int>(aad.size())) != 1) {
      return Status::ERROR_AAD;
    }
  }

  // Encrypt plaintext
  outCipher.resize(plaintext.size());
  if (!plaintext.empty()) {
    if (EVP_EncryptUpdate(ctx_, outCipher.data(), &outl, plaintext.data(),
                          static_cast<int>(plaintext.size())) != 1) {
      return Status::ERROR_UPDATE;
    }
  }

  // Finalize
  if (EVP_EncryptFinal_ex(ctx_, nullptr, &outl) != 1) {
    return Status::ERROR_FINAL;
  }

  // Get tag
  outTag.resize(tagLength);
  if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_CCM_GET_TAG, static_cast<int>(tagLength), outTag.data()) !=
      1) {
    return Status::ERROR_FINAL;
  }

  return Status::SUCCESS;
}

Aes128Ccm::Status Aes128Ccm::decrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                     bytes_span nonce, bytes_span tag,
                                     std::vector<std::uint8_t>& outPlain) noexcept {

  if (!ctx_) {
    return Status::ERROR_NULL_ALGORITHM;
  }

  // Validate key length
  if (key.size() != KEY_LENGTH) {
    return Status::ERROR_INVALID_KEY;
  }

  // Validate nonce length (7-13 bytes)
  if (nonce.size() < 7 || nonce.size() > 13) {
    return Status::ERROR_INVALID_NONCE;
  }

  // Validate tag length
  if (tag.size() < 4 || tag.size() > 16) {
    return Status::ERROR_INVALID_TAG_LENGTH;
  }

  // Initialize cipher
  if (EVP_DecryptInit_ex(ctx_, EVP_aes_128_ccm(), nullptr, nullptr, nullptr) != 1) {
    return Status::ERROR_INIT;
  }

  // Set nonce length
  if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_CCM_SET_IVLEN, static_cast<int>(nonce.size()), nullptr) !=
      1) {
    return Status::ERROR_INIT;
  }

  // Set expected tag
  if (EVP_CIPHER_CTX_ctrl(ctx_, EVP_CTRL_CCM_SET_TAG, static_cast<int>(tag.size()),
                          const_cast<std::uint8_t*>(tag.data())) != 1) {
    return Status::ERROR_INIT;
  }

  // Set key and nonce
  if (EVP_DecryptInit_ex(ctx_, nullptr, nullptr, key.data(), nonce.data()) != 1) {
    return Status::ERROR_INIT;
  }

  // Provide ciphertext length (required for CCM)
  int outl = 0;
  if (EVP_DecryptUpdate(ctx_, nullptr, &outl, nullptr, static_cast<int>(ciphertext.size())) != 1) {
    return Status::ERROR_UPDATE;
  }

  // Provide AAD if present
  if (!aad.empty()) {
    if (EVP_DecryptUpdate(ctx_, nullptr, &outl, aad.data(), static_cast<int>(aad.size())) != 1) {
      return Status::ERROR_AAD;
    }
  }

  // Decrypt ciphertext (also verifies tag in CCM)
  outPlain.resize(ciphertext.size());
  if (!ciphertext.empty()) {
    int ret = EVP_DecryptUpdate(ctx_, outPlain.data(), &outl, ciphertext.data(),
                                static_cast<int>(ciphertext.size()));
    if (ret != 1) {
      outPlain.clear();
      return Status::ERROR_AUTH; // CCM verifies during update
    }
  }

  return Status::SUCCESS;
}

/* ----------------------------- API ----------------------------- */

Aes128Ccm::Status aes128CcmEncrypt(bytes_span plaintext, bytes_span aad, bytes_span key,
                                   bytes_span nonce, std::size_t tagLength,
                                   std::vector<std::uint8_t>& outCipher,
                                   std::vector<std::uint8_t>& outTag) noexcept {
  Aes128Ccm ccm;
  return ccm.encrypt(plaintext, aad, key, nonce, tagLength, outCipher, outTag);
}

Aes128Ccm::Status aes128CcmDecrypt(bytes_span ciphertext, bytes_span aad, bytes_span key,
                                   bytes_span nonce, bytes_span tag,
                                   std::vector<std::uint8_t>& outPlain) noexcept {
  Aes128Ccm ccm;
  return ccm.decrypt(ciphertext, aad, key, nonce, tag, outPlain);
}

} // namespace apex::encryption
