# ==============================================================================
# Module Group: encryption
#
# Cryptographic libraries with platform-specific implementations.
#
# Subdirectories:
#   openssl/ - Full crypto suite (hashing, MAC, ciphers, AEAD) via OpenSSL
#   lite/    - Bare-metal AES-256-GCM (standalone, no dependencies)
# ==============================================================================

add_subdirectory(lite)

if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(openssl)
endif ()
