# ==============================================================================
# STM32 Encryptor release manifest
#
# AES-256-GCM encryption firmware for STM32L476RG (Cortex-M4):
#   STM32: Bare-metal firmware (.elf/.bin/.hex)
# ==============================================================================

APP_REGISTRY += stm32_encryptor_demo

APP_stm32_encryptor_demo_PLATFORMS          := stm32
APP_stm32_encryptor_demo_stm32_TYPE         := firmware
APP_stm32_encryptor_demo_stm32_BINARY       := stm32_encryptor_demo
