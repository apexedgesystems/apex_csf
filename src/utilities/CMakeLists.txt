# ==============================================================================
# Utilities
# ==============================================================================
# Cross-platform utility libraries: checksums, compatibility, encryption,
# helpers, concurrency, and math.
# ==============================================================================

# Bare-metal compatible libraries (header-only or explicitly flagged BAREMETAL)
add_subdirectory(checksums)
add_subdirectory(compatibility)
add_subdirectory(encryption)
add_subdirectory(helpers)

# POSIX-only libraries
if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(concurrency)
  add_subdirectory(math)
  add_subdirectory(time)
endif ()
