# ==============================================================================
# ApexHilDemo release manifest
#
# HIL flight simulation:
#   RPi:   Plant model + orchestrator + watchdog supervisor (POSIX executables)
#   STM32: Flight controller (bare-metal firmware)
# ==============================================================================

APP_REGISTRY += ApexHilDemo

APP_ApexHilDemo_PLATFORMS          := rpi stm32
APP_ApexHilDemo_rpi_TYPE           := posix
APP_ApexHilDemo_rpi_BINARY         := ApexHilDemo
APP_ApexHilDemo_stm32_TYPE         := firmware
APP_ApexHilDemo_stm32_BINARY       := apex_hil_demo_firmware

# Supervised bundle (CMake apex_add_bundle: executive + watchdog + operating
# doc as one artifact). A bundle releases through the same manifest surface as
# an app: the posix template's package step resolves BINARY to the CMake
# package_<name> target -- package_ApexHilSupervised here -- and the bundle
# stages under packages/<name> like any deployment.
APP_REGISTRY += ApexHilSupervised

APP_ApexHilSupervised_PLATFORMS    := rpi
APP_ApexHilSupervised_rpi_TYPE     := posix
APP_ApexHilSupervised_rpi_BINARY   := ApexHilSupervised
