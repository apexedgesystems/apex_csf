# ==============================================================================
# apex/All.cmake - Single entry point for Apex CMake infrastructure
# ==============================================================================
#
# Usage:
#   include(apex/All)
#
# Provides all apex_* functions. Include this once per CMakeLists.txt.
# ==============================================================================

include_guard(GLOBAL)

# Foundation utilities (must be first)
include(apex/Core)

# Build acceleration (ccache, fast linker, split DWARF)
include(apex/BuildAcceleration)

# CUDA integration (before Targets, which depends on it)
include(apex/Cuda)

# Target factories
include(apex/Targets)

# Coverage infrastructure
include(apex/Coverage)

# Testing infrastructure
include(apex/Testing)

# Tooling (docs, UPX, clang-tidy)
include(apex/Tooling)

# Data definitions (struct dictionaries for C2)
include(apex/DataDefinitions)

# Packaging infrastructure (package_<APP> targets)
include(apex/Packaging)

# Bare-metal firmware support (active only for Generic toolchains)
include(apex/Firmware)

# Configure-time summary
include(apex/Summary)
