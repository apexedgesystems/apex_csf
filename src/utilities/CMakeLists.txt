# ==============================================================================
# Utilities
# ==============================================================================
# Cross-platform utility libraries: checksums, compatibility, data_proxy,
# encryption, helpers, concurrency, math, and time.
# ==============================================================================

# Bare-metal compatible trees (header-only, explicitly flagged BAREMETAL, or
# internally gated per subdirectory)
add_subdirectory(checksums)
add_subdirectory(compatibility)
add_subdirectory(concurrency)
add_subdirectory(data_proxy)
add_subdirectory(encryption)
add_subdirectory(helpers)
add_subdirectory(math)

# POSIX-only libraries
if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(time)
endif ()
