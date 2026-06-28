# ==============================================================================
# Module: system_core_support_shm_ring_bridge
#
# Optional support component for streaming registered data to a horizon-
# spec shared-memory channel. Implements Side A (owner) of the bridge wire
# format independently of horizon's reference implementation -- the
# BRIDGE_FORMAT.md spec is the contract; apex_csf has zero
# build- or link-time dependency on horizon.
# ==============================================================================

include(apex/All)
apex_module(system_core_support_shm_ring_bridge)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/ShmRingBridge.cpp")

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
  DEPS_PUBLIC
  system_core_system_component
  utilities_concurrency
  DEPS_PRIVATE
  utilities_helpers
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs()
