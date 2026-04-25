# ==============================================================================
# filesystem - Filesystem management implementations
#
# Directory structure:
#   base/  - IFileSystem interface (header-only) [Phase 3.9]
#   posix/ - FileSystemBase, ApexFileSystem (POSIX-based, .apex_fs directory)
#   mcu/   - NullFileSystem (no-op stubs for bare-metal targets)
#
# Libraries:
#   system_core_filesystem - POSIX implementation (SHARED)
#   filesystem_mcu - MCU implementation (INTERFACE, header-only)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Base interface library (IFileSystem) -- added in Phase 3.9
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/base/CMakeLists.txt")
  add_subdirectory(base)
endif ()

# POSIX implementation library (FileSystemBase, ApexFileSystem)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/posix/CMakeLists.txt")
  add_subdirectory(posix)
endif ()

# MCU implementation library (NullFileSystem)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/mcu/CMakeLists.txt")
  add_subdirectory(mcu)
endif ()
