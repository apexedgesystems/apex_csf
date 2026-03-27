# ==============================================================================
# apex/DataDefinitions.cmake - C2 struct dictionary manifest registration
# ==============================================================================
#
# Registers apex_data.toml manifests for struct dictionary generation.
# Actual generation is done via `make apex-data-db` (separate from CMake build).
#
# Usage (per-library approach):
#   # In your library's CMakeLists.txt:
#   apex_add_data_manifest()  # Uses apex_data.toml in current directory
#
#   # At end of root CMakeLists.txt:
#   apex_finalize_data_manifests()
#
# Output:
#   ${CMAKE_BINARY_DIR}/apex_data_manifests.txt - List of registered manifests
#
# To generate struct dictionaries:
#   make apex-data-db   # Runs apex_data_gen on all registered manifests
#
# ==============================================================================

include_guard(GLOBAL)

# Output file listing all registered manifests (for Makefile to consume)
set(APEX_DATA_MANIFESTS_FILE
    "${CMAKE_BINARY_DIR}/apex_data_manifests.txt"
    CACHE INTERNAL "File listing registered apex_data.toml manifests"
)

# Output directory for struct dictionaries
set(APEX_DATA_DB_DIR
    "${CMAKE_BINARY_DIR}/apex_data_db"
    CACHE INTERNAL "Output directory for struct dictionaries"
)

# ------------------------------------------------------------------------------
# apex_add_data_manifest([MANIFEST <path>])
#
# Register an apex_data.toml manifest for struct dictionary generation.
# Call this from your library's CMakeLists.txt.
#
# Arguments:
#   MANIFEST - Path to apex_data.toml (default: ${CMAKE_CURRENT_SOURCE_DIR}/apex_data.toml)
#
# Example:
#   apex_add_data_manifest()  # Uses apex_data.toml in current directory
#   apex_add_data_manifest(MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/data/my_manifest.toml)
#
# No-op if manifest file doesn't exist.
# ------------------------------------------------------------------------------
function (apex_add_data_manifest)
  cmake_parse_arguments(ADM "" "MANIFEST" "" ${ARGN})

  # Default manifest path
  if (NOT ADM_MANIFEST)
    set(ADM_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/apex_data.toml")
  endif ()

  # Check manifest exists
  if (NOT EXISTS "${ADM_MANIFEST}")
    return()
  endif ()

  # Track manifest path globally
  set_property(GLOBAL APPEND PROPERTY APEX_DATA_MANIFEST_PATHS "${ADM_MANIFEST}")
endfunction ()

# ------------------------------------------------------------------------------
# apex_finalize_data_manifests()
#
# Write list of registered manifests to apex_data_manifests.txt for Makefile.
# Call this once at the end of root CMakeLists.txt, after all subdirectories.
# ------------------------------------------------------------------------------
function (apex_finalize_data_manifests)
  # Get all tracked manifest paths
  get_property(_manifests GLOBAL PROPERTY APEX_DATA_MANIFEST_PATHS)

  if (NOT _manifests)
    message(STATUS "[apex] No data manifests registered")
    file(WRITE "${APEX_DATA_MANIFESTS_FILE}" "")
    return()
  endif ()

  # Write manifest list to file (one per line)
  list(LENGTH _manifests _count)
  string(REPLACE ";" "\n" _content "${_manifests}")
  file(WRITE "${APEX_DATA_MANIFESTS_FILE}" "${_content}\n")

  message(
    STATUS "[apex] Registered ${_count} data manifest(s) -> run 'make apex-data-db' to generate"
  )
endfunction ()
