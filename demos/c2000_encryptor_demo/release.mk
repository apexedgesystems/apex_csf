# ==============================================================================
# C2000 Encryptor Demo release manifest
#
# AES-256-GCM encryption firmware for LAUNCHXL-F280049C (C28x DSP):
#   C2000: Bare-metal firmware (.elf/.hex)
# ==============================================================================

APP_REGISTRY += c2000_encryptor_demo

APP_c2000_encryptor_demo_PLATFORMS          := c2000
APP_c2000_encryptor_demo_c2000_TYPE         := firmware
APP_c2000_encryptor_demo_c2000_BINARY       := c2000_encryptor_demo
