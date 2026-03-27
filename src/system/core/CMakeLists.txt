# ==============================================================================
# System Core
# ==============================================================================
# Core runtime: HAL, infrastructure, components, executive, and support.
# ==============================================================================

# Hardware Abstraction Layer (platform-specific drivers)
add_subdirectory(hal)

# Core infrastructure (no component dependencies)
add_subdirectory(infrastructure)

# Core components (inherit from SystemComponentBase)
add_subdirectory(components)

# Support components (optional runtime services)
add_subdirectory(support)

# Executive (orchestration layer)
add_subdirectory(executive)

# Monte Carlo (batch execution infrastructure)
add_subdirectory(monte_carlo)
