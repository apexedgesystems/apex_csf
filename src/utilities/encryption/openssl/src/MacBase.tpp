/**
 * @file MacBase.tpp
 * @brief Template implementation for Message Authentication Code (MAC) functions.
 */
#ifndef APEX_ENCRYPTION_MAC_BASE_TPP
#define APEX_ENCRYPTION_MAC_BASE_TPP

#include "src/utilities/encryption/openssl/inc/MacBase.hpp"

#include "src/utilities/compatibility/inc/compat_attributes.hpp"
#include "src/utilities/compatibility/inc/compat_openssl.hpp"

#include <algorithm>            // std::min
#include <cstring>              // std::memcpy
#include <openssl/core_names.h> // OSSL_MAC_PARAM_*
#include <openssl/params.h>     // OSSL_PARAM_construct_*

namespace apex::encryption {

// OpenSSL compat alias (provider helpers, version gating)
namespace o = compat::ossl;

// Construction: ensure providers (3.x), fetch MAC, build ctx (ctx holds its own ref).
template <typename Derived> MacBase<Derived>::MacBase() : ctx_(nullptr), buffer_{}, key_{} {
  o::ensureProvidersLoaded();

  const EVP_MAC* macAlg = Derived::fetchMacAlgorithm();
  if (macAlg) {
    ctx_ = EVP_MAC_CTX_new(const_cast<EVP_MAC*>(macAlg));
#if COMPAT_OPENSSL_MAJOR >= 3
    // ctx increments the refcount; release our fetched handle
    EVP_MAC_free(const_cast<EVP_MAC*>(macAlg));
#endif
  }
}

// Teardown
template <typename Derived> MacBase<Derived>::~MacBase() {
  if (ctx_) {
    EVP_MAC_CTX_free(ctx_);
    ctx_ = nullptr;
  }
}

// Vector-based one-shot MAC
template <typename Derived>
inline typename MacBase<Derived>::Status MacBase<Derived>::mac(bytes_span message,
                                                               std::vector<uint8_t>& out) noexcept {
  if (COMPAT_UNLIKELY(ctx_ == nullptr)) {
    return Status::ERROR_NULL_ALGORITHM;
  }
  if (COMPAT_UNLIKELY(key_.size() != Derived::KEY_LENGTH)) {
    return Status::ERROR_INVALID_KEY;
  }

  // Optional parameter (digest/cipher) from Derived.
  const char* name = Derived::fetchParamName();
  const char* value = Derived::fetchParamValue();

  OSSL_PARAM paramsArr[2];
  OSSL_PARAM* params = nullptr;
  if (name) {
    paramsArr[0] =
        OSSL_PARAM_construct_utf8_string(const_cast<char*>(name), const_cast<char*>(value), 0);
    paramsArr[1] = OSSL_PARAM_construct_end();
    params = paramsArr;
  }

  if (COMPAT_UNLIKELY(EVP_MAC_init(ctx_, key_.data(), key_.size(), params) <= 0)) {
    return Status::ERROR_INIT;
  }
  if (!message.empty()) {
    if (COMPAT_UNLIKELY(EVP_MAC_update(ctx_, message.data(), message.size()) <= 0)) {
      return Status::ERROR_UPDATE;
    }
  }

  size_t got = Derived::DIGEST_LENGTH;
  if (COMPAT_UNLIKELY(EVP_MAC_final(ctx_, buffer_.data(), &got, buffer_.size()) <= 0)) {
    return Status::ERROR_FINAL;
  }

  if (out.capacity() < EVP_MAX_MD_SIZE) {
    out.reserve(EVP_MAX_MD_SIZE);
  }
  out.assign(buffer_.begin(), buffer_.begin() + got);
  return Status::SUCCESS;
}

// Zero-allocation one-shot MAC
template <typename Derived>
inline typename MacBase<Derived>::Status MacBase<Derived>::mac(bytes_span message, uint8_t* outBuf,
                                                               size_t& outLen) noexcept {
  if (COMPAT_UNLIKELY(ctx_ == nullptr)) {
    return Status::ERROR_NULL_ALGORITHM;
  }
  if (COMPAT_UNLIKELY(key_.size() != Derived::KEY_LENGTH)) {
    return Status::ERROR_INVALID_KEY;
  }
  if (COMPAT_UNLIKELY(outBuf == nullptr)) {
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }

  const size_t required = Derived::DIGEST_LENGTH;
  if (COMPAT_UNLIKELY(outLen < required)) {
    // Report required size; do not write partials.
    outLen = required;
    return Status::ERROR_OUTPUT_TOO_SMALL;
  }

  const char* name = Derived::fetchParamName();
  const char* value = Derived::fetchParamValue();

  OSSL_PARAM paramsArr[2];
  OSSL_PARAM* params = nullptr;
  if (name) {
    paramsArr[0] =
        OSSL_PARAM_construct_utf8_string(const_cast<char*>(name), const_cast<char*>(value), 0);
    paramsArr[1] = OSSL_PARAM_construct_end();
    params = paramsArr;
  }

  if (COMPAT_UNLIKELY(EVP_MAC_init(ctx_, key_.data(), key_.size(), params) <= 0)) {
    return Status::ERROR_INIT;
  }
  if (!message.empty()) {
    if (COMPAT_UNLIKELY(EVP_MAC_update(ctx_, message.data(), message.size()) <= 0)) {
      return Status::ERROR_UPDATE;
    }
  }

  size_t got = required;
  if (COMPAT_UNLIKELY(EVP_MAC_final(ctx_, outBuf, &got, outLen) <= 0)) {
    return Status::ERROR_FINAL;
  }

  outLen = got;
  return Status::SUCCESS;
}

} // namespace apex::encryption

#endif // ENCRYPTION_MAC_BASE_TPP
