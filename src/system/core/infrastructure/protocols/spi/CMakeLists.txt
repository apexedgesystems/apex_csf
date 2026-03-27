# ==============================================================================
# Module: system_core_protocols_spi
#
# Linux SPI master device abstraction using the spidev user-space interface.
# Provides full-duplex transfers, batch operations, and statistics tracking.
#
# Libraries:
#   system_core_protocols_spi - SPI adapter with spidev backend
# ==============================================================================

include(apex/All)
apex_module(system_core_protocols_spi)

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/SpiStatus.cpp" "${SRC_DIR}/SpiConfig.cpp" "${SRC_DIR}/SpiAdapter.cpp"
                "${SRC_DIR}/SpiSocketDevice.cpp"
)

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PRIVATE
  system_core_protocols_common
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst dtst)
