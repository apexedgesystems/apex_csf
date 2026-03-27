/**
 * @file HashBase.tpp
 * @brief Template implementation for cryptographic hash functions.
 */
#ifndef APEX_ENCRYPTION_HASH_BASE_TPP
#define APEX_ENCRYPTION_HASH_BASE_TPP

#include "src/utilities/encryption/openssl/inc/HashBase.hpp"

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_openssl.hpp"

#include <cstring> // std::memcpy

namespace apex::encryption {

// OpenSSL helper alias
namespace o = compat::ossl;

// Reusable OpenSSL context
template <typename Derived> HashBase<Derived>::HashBase() : ctx_(EVP_MD_CTX_new()), buffer_{} {}

// Free context
template <typename Derived> HashBase<Derived>::~HashBase() {
  if (ctx_) {
    EVP_MD_CTX_free(ctx_);
    ctx_ = nullptr;
  }
}

// Vector-based one-shot hash
template <typename Derived>
inline typename HashBase<Derived>::Status
HashBase<Derived>::hash(bytes_span message, std::vector<uint8_t>& outDigest) noexcept {
  if (COMPAT_UNLIKELY(ctx_ == nullptr)) {
    return Status::ERROR_DIGEST_INIT;
  }

  auto mdh = o::fetchMd<Derived>();
  const EVP_MD* md = mdh.md;
  if (COMPAT_UNLIKELY(md == nullptr)) {
    return Status::ERROR_NULL_ALGORITHM;
  }

  if (COMPAT_UNLIKELY(EVP_DigestInit_ex(ctx_, md, nullptr) != 1)) {
    return Status::ERROR_DIGEST_INIT;
  }

  if (!message.empty()) {
    if (COMPAT_UNLIKELY(EVP_DigestUpdate(ctx_, message.data(), message.size()) != 1)) {
      EVP_MD_CTX_reset(ctx_);
      return Status::ERROR_DIGEST_UPDATE;
    }
  }

  unsigned int got = 0;
  if (COMPAT_UNLIKELY(EVP_DigestFinal_ex(ctx_, buffer_.data(), &got) != 1)) {
    EVP_MD_CTX_reset(ctx_);
    return Status::ERROR_DIGEST_FINAL;
  }

  // Reserve if caller forgot; avoids allocation on assign.
  if (outDigest.capacity() < EVP_MAX_MD_SIZE) {
    outDigest.reserve(EVP_MAX_MD_SIZE);
  }
  outDigest.assign(buffer_.begin(), buffer_.begin() + got);

  EVP_MD_CTX_reset(ctx_);
  return Status::SUCCESS;
}

// Zero-allocation one-shot hash
template <typename Derived>
inline typename HashBase<Derived>::Status
HashBase<Derived>::hash(bytes_span message, uint8_t* outBuf, size_t& outLen) noexcept {
  if (COMPAT_UNLIKELY(ctx_ == nullptr)) {
    return Status::ERROR_DIGEST_INIT;
  }
  if (COMPAT_UNLIKELY(outBuf == nullptr)) {
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }

  auto mdh = o::fetchMd<Derived>();
  const EVP_MD* md = mdh.md;
  if (COMPAT_UNLIKELY(md == nullptr)) {
    return Status::ERROR_NULL_ALGORITHM;
  }

  const int mdSize = EVP_MD_size(md); // -1 on error
  if (COMPAT_UNLIKELY(mdSize <= 0)) {
    return Status::ERROR_DIGEST_INIT;
  }
  if (COMPAT_UNLIKELY(outLen < static_cast<size_t>(mdSize))) {
    outLen = static_cast<size_t>(mdSize);
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }

  if (COMPAT_UNLIKELY(EVP_DigestInit_ex(ctx_, md, nullptr) != 1)) {
    return Status::ERROR_DIGEST_INIT;
  }

  if (!message.empty()) {
    if (COMPAT_UNLIKELY(EVP_DigestUpdate(ctx_, message.data(), message.size()) != 1)) {
      EVP_MD_CTX_reset(ctx_);
      return Status::ERROR_DIGEST_UPDATE;
    }
  }

  unsigned int got = 0;
  if (COMPAT_UNLIKELY(EVP_DigestFinal_ex(ctx_, buffer_.data(), &got) != 1)) {
    EVP_MD_CTX_reset(ctx_);
    return Status::ERROR_DIGEST_FINAL;
  }

  std::memcpy(outBuf, buffer_.data(), got);
  outLen = static_cast<size_t>(got);

  EVP_MD_CTX_reset(ctx_);
  return Status::SUCCESS;
}

} // namespace apex::encryption

#endif // HASH_BASE_TPP
