# ==============================================================================
# demos/test_plugin - The hot-swap test plugin the demos share
#
# One component, built twice with different version numbers:
#   - TestPlugin_v1.so (VERSION=1)
#   - TestPlugin_v2.so (VERSION=2)
#
# The ops and HIL demo executives link the component as their hot-swap
# target; their checkouts upload TestPlugin_v2.so and swap it in with
# RELOAD_LIBRARY. The libraries are not installed.
# ==============================================================================

include(apex/All)

foreach (_version 1 2)
  apex_add_test_plugin(
    TARGET TestPlugin_v${_version} SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/TestPlugin_v${_version}.cpp" LINK system_core_system_component
  )
endforeach ()

# Unit tests of the plugin's header.
add_subdirectory(utst)
