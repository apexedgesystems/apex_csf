# ==============================================================================
# ApexActionDemo release manifest
#
# Action engine + DataTransform demonstration. RPi only (no firmware).
# ==============================================================================

APP_REGISTRY += ApexActionDemo

APP_ApexActionDemo_PLATFORMS          := rpi
APP_ApexActionDemo_TPRM               := apps/apex_action_demo/tprm/master.tprm
APP_ApexActionDemo_rpi_TYPE           := posix
APP_ApexActionDemo_rpi_BINARY         := ApexActionDemo
APP_ApexActionDemo_rpi_EXTRA_BINS     :=
