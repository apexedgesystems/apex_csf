# ==============================================================================
# ApexHilDemo release manifest
#
# HIL flight simulation:
#   RPi:   Plant model + orchestrator + watchdog supervisor (POSIX executables)
#   STM32: Flight controller (bare-metal firmware)
# ==============================================================================

APP_REGISTRY += ApexHilDemo

APP_ApexHilDemo_PLATFORMS          := rpi stm32
APP_ApexHilDemo_TPRM               := apps/apex_hil_demo/tprm/master_1khz.tprm
APP_ApexHilDemo_rpi_TYPE           := posix
APP_ApexHilDemo_rpi_BINARY         := ApexHilDemo
APP_ApexHilDemo_rpi_EXTRA_BINS     := ApexWatchdog
APP_ApexHilDemo_stm32_TYPE         := firmware
APP_ApexHilDemo_stm32_BINARY       := apex_hil_demo_firmware
