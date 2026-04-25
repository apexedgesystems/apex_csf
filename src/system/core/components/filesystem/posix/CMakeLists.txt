# ==============================================================================
# system_core_filesystem - Full-featured filesystem implementation
#
# Deterministic, no-throw abstraction for filesystem management.
#
# Components:
#   - FileSystemBase   - Abstract base with maintenance, atomic write, archival
#   - ApexFileSystem   - Standard deployment layout (libs, tlm, logs, etc.)
#   - FileSystemStatus - Typed status codes
# ==============================================================================

include(apex/All)
apex_module(system_core_filesystem)

# ------------------------------------------------------------------------------
# Sources (3 files)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/FileSystemBase.cpp" "${SRC_DIR}/FileSystemStatus.cpp"
                "${SRC_DIR}/ApexFileSystem.cpp"
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
  DEPS_PUBLIC
  filesystem_base
  system_core_system_component
  fmt::fmt
)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
