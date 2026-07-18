# Tier-S math (BAREMETAL interface libraries): traversed on every platform.
add_subdirectory(celestial)
add_subdirectory(quaternion)
add_subdirectory(vecmat)
add_subdirectory(frames)

# Host-only math (BLAS/bench-backed): never bare-metal.
if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(integration)
  add_subdirectory(legendre)
  add_subdirectory(linalg)
endif ()
