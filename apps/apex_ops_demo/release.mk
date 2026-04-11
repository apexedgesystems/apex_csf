# ==============================================================================
# ApexOpsDemo release manifest
#
# Pure SIL waveform generator demo for Zenith C2 system testing.
# RPi only (no firmware targets).
# ==============================================================================

APP_REGISTRY += ApexOpsDemo

APP_ApexOpsDemo_PLATFORMS          := rpi
APP_ApexOpsDemo_TPRM               := apps/apex_ops_demo/tprm/master.tprm
APP_ApexOpsDemo_rpi_TYPE           := posix
APP_ApexOpsDemo_rpi_BINARY         := ApexOpsDemo
APP_ApexOpsDemo_rpi_EXTRA_BINS     :=
