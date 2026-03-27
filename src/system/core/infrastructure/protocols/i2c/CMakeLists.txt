# ==============================================================================
# Module: system_core_protocols_i2c
#
# Linux I2C master device abstraction using the i2c-dev user-space interface.
# Provides 7-bit/10-bit addressing, combined transactions, and register helpers.
#
# Libraries:
#   system_core_protocols_i2c - I2C adapter with i2c-dev backend
# ==============================================================================

include(apex/All)
apex_module(system_core_protocols_i2c)

# ------------------------------------------------------------------------------
# Sources (4 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/I2cStatus.cpp" "${SRC_DIR}/I2cConfig.cpp" "${SRC_DIR}/I2cAdapter.cpp"
                "${SRC_DIR}/I2cSocketDevice.cpp"
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
