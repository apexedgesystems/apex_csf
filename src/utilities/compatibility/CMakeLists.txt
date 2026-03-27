# ==============================================================================
# Module: utilities_compatibility
#
# Header-only compatibility shims for C++ standards, OpenSSL versions, and CUDA.
# Provides polyfills enabling consistent APIs across C++17/20/23.
#
# Headers:
#   compat_endian.hpp       - std::endian shim (C++20)
#   compat_byteswap.hpp     - std::byteswap shim (C++23)
#   compat_span.hpp         - bytes_span, rospan<T> (C++20)
#   compat_concurrency.hpp  - Atomic wait/notify (C++20)
#   compat_attributes.hpp   - Compiler hints (LIKELY, HOT)
#   compat_cuda_*.hpp       - CUDA device annotations, error handling
#   compat_openssl.hpp      - OpenSSL 1.1.1/3.x provider handling
# ==============================================================================

include(apex/All)
apex_module(utilities_compatibility)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME ${LIB_NAME} INC "${INC_DIR}" BAREMETAL)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst tst)
