# ==============================================================================
# filesystem - Filesystem management implementations
#
# Directory structure:
#   apex/ - FileSystemBase, ApexFileSystem (POSIX-based, .apex_fs directory)
#   lite/ - NullFileSystem (no-op stubs for MCUs)
#
# Libraries:
#   system_core_filesystem - Full implementation (SHARED)
# ==============================================================================

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

# Apex implementation library (FileSystemBase, ApexFileSystem)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/apex/CMakeLists.txt")
  add_subdirectory(apex)
endif ()

# Lite implementation library (NullFileSystem)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lite/CMakeLists.txt")
  add_subdirectory(lite)
endif ()
