# Core infrastructure libraries (foundational abstractions)
# These provide base classes and rigging used by components and models.

# Base class for all system components (must come first)
add_subdirectory(system_component)

# Other infrastructure
add_subdirectory(logs)
add_subdirectory(protocols)
add_subdirectory(schedulable)
