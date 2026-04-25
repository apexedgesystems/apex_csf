# ==============================================================================
# filesystem_base - Pure interface library (header-only)
#
# Provides the IFileSystem marker interface that all filesystem
# implementations satisfy. No heavy dependencies -- suitable for both
# POSIX and bare-metal MCU targets.
#
# Dependents:
#   - system_core_filesystem (FileSystemBase, ApexFileSystem)
#   - filesystem_mcu (NullFileSystem)
#   - Future: RTOS-tier implementations
# ==============================================================================

include(apex/All)

# ------------------------------------------------------------------------------
# Interface Library
# ------------------------------------------------------------------------------

apex_add_interface_library(NAME filesystem_base INC "${PROJECT_SOURCE_DIR}" BAREMETAL)

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen)
# ------------------------------------------------------------------------------

apex_standard_optins(filesystem_base)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/utst/CMakeLists.txt")
  add_subdirectory(utst)
endif ()
