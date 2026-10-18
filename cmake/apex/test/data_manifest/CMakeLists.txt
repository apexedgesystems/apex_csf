# ==============================================================================
# cmake/apex/test/data_manifest - apex_add_data_manifest contract fixture
#
# A configure-only project that calls the real helper in one way per CASE
# and finalizes the registered list, so `make check-data-manifest-helper`
# can assert the helper's contract from the outside: the default form is a
# no-op without a manifest, a present registration lands in the list, and
# an explicit path that does not exist (typed, or a failed lookup's
# -NOTFOUND value) or a malformed call fails configure.
# ==============================================================================

cmake_minimum_required(VERSION 3.24)
project(apex_data_manifest_fixture NONE)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
include(apex/DataDefinitions)

if (NOT DEFINED CASE)
  message(
    FATAL_ERROR "pass -DCASE=<default-absent|valid|explicit-missing|notfound|keyword-without-value>"
  )
endif ()

if (CASE STREQUAL "default-absent")
  # No apex_data.toml lives in this directory: the documented no-op.
  apex_add_data_manifest()
elseif (CASE STREQUAL "valid")
  apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/present_data.toml")
elseif (CASE STREQUAL "explicit-missing")
  apex_add_data_manifest(MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/missing_data.toml")
elseif (CASE STREQUAL "notfound")
  find_file(
    FIXTURE_LOOKUP missing_data.toml
    PATHS "${CMAKE_CURRENT_SOURCE_DIR}"
    NO_DEFAULT_PATH
  )
  apex_add_data_manifest(MANIFEST "${FIXTURE_LOOKUP}")
elseif (CASE STREQUAL "keyword-without-value")
  apex_add_data_manifest(MANIFEST)
else ()
  message(FATAL_ERROR "unknown CASE '${CASE}'")
endif ()

apex_finalize_data_manifests()
