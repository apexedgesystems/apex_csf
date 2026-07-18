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
add_subdirectory(time)
