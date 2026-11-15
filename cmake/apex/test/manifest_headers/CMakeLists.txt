# ==============================================================================
# cmake/apex/test/manifest_headers - lib.manifest `headers` contract fixture
#
# A small project that hands the real manifest helper one library directory
# per CASE, so `make check-manifest-headers` can assert from the outside that
# every declared header keeps its own generated translation unit:
#
#   same_name     two headers that share a file name in different directories
#                 each get a TU, and both compile
#   first_broken  the same pair with the first header broken: the build fails
#                 on that header, so the second did not take its place
#   collision     two declared paths that map to one generated TU name fail
#                 the configure, naming both
# ==============================================================================

cmake_minimum_required(VERSION 3.24)
project(apex_manifest_headers_fixture CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../..")
include(apex/Manifest)

if (NOT DEFINED CASE)
  message(FATAL_ERROR "pass -DCASE=<same_name|first_broken|collision>")
endif ()
if (NOT IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${CASE}")
  message(FATAL_ERROR "unknown CASE '${CASE}'")
endif ()

add_subdirectory(${CASE})
