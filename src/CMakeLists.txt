# ==============================================================================
# Source Root
# ==============================================================================
# Routes to simulation, utilities, and system subdirectories.
# ==============================================================================

# Sim requires POSIX (skip on bare-metal)
if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(sim)
endif ()

# Utilities: always process (individual targets gate via BAREMETAL flag)
add_subdirectory(utilities)

add_subdirectory(system)
