# ==============================================================================
# ApexTimeDemo release manifest
#
# PPS time-distribution demo. Pure SIL (MockPps + GPS simulator). RPi
# only -- no firmware variant.
# ==============================================================================

APP_REGISTRY += ApexTimeDemo

APP_ApexTimeDemo_PLATFORMS          := rpi
APP_ApexTimeDemo_rpi_TYPE           := posix
APP_ApexTimeDemo_rpi_BINARY         := ApexTimeDemo
APP_ApexTimeDemo_rpi_EXTRA_BINS     :=
