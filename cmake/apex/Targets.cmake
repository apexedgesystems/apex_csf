# ==============================================================================
# apex/Targets.cmake - Target factory functions
# ==============================================================================

include_guard(GLOBAL)
include(GNUInstallDirs)

option(APEX_TARGETS_VERBOSE "Print per-target creation details" OFF)

# ------------------------------------------------------------------------------
# Platform capability detection
# ------------------------------------------------------------------------------
# APEX_PLATFORM_BAREMETAL is set by root CMakeLists.txt (CACHE INTERNAL).
# All target functions below use it to gate bare-metal vs hosted behavior.

# ------------------------------------------------------------------------------
# _apex_check_requirements(result_var NAME <name> REQUIRES <reqs...>)
#
# Check if platform requirements are satisfied. Sets result_var to TRUE if
# all requirements are met, FALSE otherwise. Logs skipped targets in verbose.
#
# Supported requirements:
#   POSIX    - POSIX APIs (files, threads, signals) - unavailable on bare-metal
#   OPENSSL  - OpenSSL library - unavailable on bare-metal
#   LINUX    - Linux-specific APIs - unavailable on bare-metal
#   PTHREADS - POSIX threads - unavailable on bare-metal
#   CUDA     - CUDA toolkit - requires CUDAToolkit_FOUND
# ------------------------------------------------------------------------------
function (_apex_check_requirements result_var)
  cmake_parse_arguments(REQ "" "NAME" "REQUIRES" ${ARGN})

  set(_satisfied TRUE)
  set(_missing "")

  foreach (_req IN LISTS REQ_REQUIRES)
    set(_req_met FALSE)

    if (_req STREQUAL "POSIX")
      if (NOT APEX_PLATFORM_BAREMETAL)
        set(_req_met TRUE)
      endif ()
    elseif (_req STREQUAL "OPENSSL")
      if (NOT APEX_PLATFORM_BAREMETAL)
        set(_req_met TRUE)
      endif ()
    elseif (_req STREQUAL "LINUX")
      if (NOT APEX_PLATFORM_BAREMETAL AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(_req_met TRUE)
      endif ()
    elseif (_req STREQUAL "PTHREADS")
      if (NOT APEX_PLATFORM_BAREMETAL)
        set(_req_met TRUE)
      endif ()
    elseif (_req STREQUAL "CUDA")
      if (CUDAToolkit_FOUND AND CMAKE_CUDA_COMPILER)
        set(_req_met TRUE)
      endif ()
    else ()
      message(WARNING "[apex] Unknown requirement '${_req}' for target ${REQ_NAME}")
      set(_req_met TRUE) # Don't block on unknown requirements
    endif ()

    if (NOT _req_met)
      set(_satisfied FALSE)
      list(APPEND _missing ${_req})
    endif ()
  endforeach ()

  if (NOT _satisfied AND APEX_TARGETS_VERBOSE)
    list(JOIN _missing ", " _missing_str)
    message(STATUS "[apex] SKIP ${REQ_NAME} (missing: ${_missing_str})")
  endif ()

  set(${result_var}
      ${_satisfied}
      PARENT_SCOPE
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_interface_library(...)
#
# Define a header-only module as an INTERFACE target.
# Propagates includes, compile definitions, features, and transitive deps.
#
# By default, libraries require POSIX and are skipped on bare-metal builds.
# Use the BAREMETAL flag to opt a library into bare-metal compilation.
#
# Arguments:
#   NAME            <target>           required
#   INC             <include_dir>      required
#   DEPS_INTERFACE  <targets...>       optional
#   DEFS            <defs...>          optional
#   FEATURES        <features...>      optional
#   REQUIRES        <reqs...>          optional (POSIX, OPENSSL, LINUX, etc.)
#   BAREMETAL                          optional flag (enables bare-metal build)
# ------------------------------------------------------------------------------
function (apex_add_interface_library)
  cmake_parse_arguments(IL "BAREMETAL" "NAME;INC" "DEPS_INTERFACE;DEFS;FEATURES;REQUIRES" ${ARGN})
  apex_require(IL_NAME IL_INC)

  # Default: require POSIX unless BAREMETAL flag is set
  set(_reqs ${IL_REQUIRES})
  if (NOT IL_BAREMETAL AND APEX_PLATFORM_BAREMETAL)
    # Skip on bare-metal unless explicitly opted in
    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] SKIP ${IL_NAME} (bare-metal, no BAREMETAL flag)")
    endif ()
    return()
  endif ()

  # Check explicit platform requirements
  if (_reqs)
    _apex_check_requirements(_satisfied NAME ${IL_NAME} REQUIRES ${_reqs})
    if (NOT _satisfied)
      return()
    endif ()
  endif ()

  add_library(${IL_NAME} INTERFACE)
  add_library(apex::${IL_NAME} ALIAS ${IL_NAME})

  target_include_directories(
    ${IL_NAME} INTERFACE $<BUILD_INTERFACE:${IL_INC}>
                         $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  if (IL_DEPS_INTERFACE)
    target_link_libraries(${IL_NAME} INTERFACE ${IL_DEPS_INTERFACE})
  endif ()

  if (IL_DEFS)
    target_compile_definitions(${IL_NAME} INTERFACE ${IL_DEFS})
  endif ()

  if (IL_FEATURES)
    target_compile_features(${IL_NAME} INTERFACE ${IL_FEATURES})
  endif ()

  install(TARGETS ${IL_NAME} EXPORT apexTargets)
  install(DIRECTORY "${IL_INC}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  if (APEX_TARGETS_VERBOSE)
    message(STATUS "[apex] INTERFACE ${IL_NAME} inc='${IL_INC}' deps='${IL_DEPS_INTERFACE}'")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_library(...)
#
# Define a compiled library (STATIC/SHARED).
# Installs target and headers, exports apex:: alias.
#
# By default, libraries require POSIX and are skipped on bare-metal builds.
# Use the BAREMETAL flag to opt a library into bare-metal compilation.
#
# Arguments:
#   NAME         <target>           required
#   TYPE         STATIC|SHARED      required
#   INC          <include_dir>      required
#   SRC          <files...>         required
#   DEPS_PUBLIC  <targets...>       optional
#   DEPS_PRIVATE <targets...>       optional
#   REQUIRES     <reqs...>          optional (POSIX, OPENSSL, LINUX, etc.)
#   BAREMETAL                       optional flag (enables bare-metal build)
# ------------------------------------------------------------------------------
function (apex_add_library)
  cmake_parse_arguments(
    AL "BAREMETAL" "NAME;TYPE;INC" "SRC;DEPS_PUBLIC;DEPS_PRIVATE;REQUIRES" ${ARGN}
  )
  apex_require(AL_NAME AL_TYPE AL_INC AL_SRC)

  # Default: require POSIX unless BAREMETAL flag is set
  set(_reqs ${AL_REQUIRES})
  if (NOT AL_BAREMETAL AND APEX_PLATFORM_BAREMETAL)
    # Skip on bare-metal unless explicitly opted in
    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] SKIP ${AL_NAME} (bare-metal, no BAREMETAL flag)")
    endif ()
    return()
  endif ()

  # Check explicit platform requirements
  if (_reqs)
    _apex_check_requirements(_satisfied NAME ${AL_NAME} REQUIRES ${_reqs})
    if (NOT _satisfied)
      return()
    endif ()
  endif ()

  # Force STATIC when bare-metal or APEX_STATIC_LINK is set
  set(_type ${AL_TYPE})
  if (_type STREQUAL "SHARED" AND (APEX_PLATFORM_BAREMETAL OR APEX_STATIC_LINK))
    set(_type STATIC)
  endif ()

  add_library(${AL_NAME} ${_type})
  add_library(apex::${AL_NAME} ALIAS ${AL_NAME})

  target_sources(${AL_NAME} PRIVATE ${AL_SRC})
  target_include_directories(
    ${AL_NAME} PUBLIC $<BUILD_INTERFACE:${AL_INC}> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

  if (AL_DEPS_PUBLIC)
    target_link_libraries(${AL_NAME} PUBLIC ${AL_DEPS_PUBLIC})
  endif ()

  if (AL_DEPS_PRIVATE)
    target_link_libraries(${AL_NAME} PRIVATE ${AL_DEPS_PRIVATE})
  endif ()

  install(
    TARGETS ${AL_NAME}
    EXPORT apexTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
  install(DIRECTORY "${AL_INC}/" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

  if (APEX_TARGETS_VERBOSE)
    list(LENGTH AL_SRC _src_count)
    message(STATUS "[apex] LIB ${AL_NAME} (${AL_TYPE}) inc='${AL_INC}' srcs=${_src_count}")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# _apex_add_executable(...)
#
# Internal helper for creating executable targets (apps and tools).
#
# Arguments:
#   NAME        <target>           required
#   SRC         <files...>         required
#   OUTPUT_DIR  <dir>              required
#   KIND        <APP|TOOL>         required (for verbose output)
#   LINK        <targets...>       optional
#   INC         <dirs...>          optional
#   DEFS        <defs...>          optional
#   FEATURES    <features...>      optional
#   NO_INSTALL                     optional flag
#   ENABLE_UPX                     optional flag
# ------------------------------------------------------------------------------
function (_apex_add_executable)
  cmake_parse_arguments(
    EXE "NO_INSTALL;ENABLE_UPX" "NAME;OUTPUT_DIR;KIND" "SRC;LINK;INC;DEFS;FEATURES" ${ARGN}
  )
  apex_require(EXE_NAME EXE_SRC EXE_OUTPUT_DIR EXE_KIND)

  add_executable(${EXE_NAME})
  target_sources(${EXE_NAME} PRIVATE ${EXE_SRC})

  # Linker hint for CUDA runtime resolution
  target_link_options(
    ${EXE_NAME} PRIVATE
    $<$<TARGET_EXISTS:CUDA::cudart>:-Wl,-rpath-link,$<TARGET_FILE_DIR:CUDA::cudart>>
  )

  if (EXE_INC)
    target_include_directories(${EXE_NAME} PRIVATE ${EXE_INC})
  endif ()

  if (EXE_LINK)
    target_link_libraries(${EXE_NAME} PRIVATE ${EXE_LINK})
  endif ()

  if (EXE_DEFS)
    target_compile_definitions(${EXE_NAME} PRIVATE ${EXE_DEFS})
  endif ()

  if (EXE_FEATURES)
    target_compile_features(${EXE_NAME} PRIVATE ${EXE_FEATURES})
  endif ()

  set_target_properties(${EXE_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EXE_OUTPUT_DIR}")

  if (NOT EXE_NO_INSTALL)
    install(TARGETS ${EXE_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif ()

  if (EXE_ENABLE_UPX)
    apex_add_upx_copy(${EXE_NAME})
  endif ()

  if (APEX_TARGETS_VERBOSE)
    list(LENGTH EXE_SRC _src_count)
    message(STATUS "[apex] ${EXE_KIND} ${EXE_NAME} srcs=${_src_count} link='${EXE_LINK}'")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_app(...)
#
# Define a production application executable. Outputs to bin/, auto-enables UPX.
#
# By default, apps require POSIX and are skipped on bare-metal builds.
# Use the BAREMETAL flag to opt an app into bare-metal compilation.
#
# Arguments:
#   NAME        <target>           required
#   SRC         <files...>         required
#   LINK        <targets...>       optional
#   INC         <dirs...>          optional
#   DEFS        <defs...>          optional
#   FEATURES    <features...>      optional
#   REQUIRES    <reqs...>          optional (POSIX, OPENSSL, LINUX, etc.)
#   BAREMETAL                      optional flag (enables bare-metal build)
#   NO_INSTALL                     optional flag
#   NO_UPX                         optional flag (skip auto UPX)
# ------------------------------------------------------------------------------
function (apex_add_app)
  cmake_parse_arguments(
    APP "NO_INSTALL;NO_UPX;BAREMETAL" "NAME" "SRC;LINK;INC;DEFS;FEATURES;REQUIRES" ${ARGN}
  )
  apex_require(APP_NAME APP_SRC)

  # Default: require POSIX unless BAREMETAL flag is set
  set(_reqs ${APP_REQUIRES})
  if (NOT APP_BAREMETAL AND APEX_PLATFORM_BAREMETAL)
    # Skip on bare-metal unless explicitly opted in
    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] SKIP ${APP_NAME} (bare-metal, no BAREMETAL flag)")
    endif ()
    return()
  endif ()

  # Check explicit platform requirements
  if (_reqs)
    _apex_check_requirements(_satisfied NAME ${APP_NAME} REQUIRES ${_reqs})
    if (NOT _satisfied)
      return()
    endif ()
  endif ()

  set(_args
      NAME
      ${APP_NAME}
      SRC
      ${APP_SRC}
      OUTPUT_DIR
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}"
      KIND
      "APP"
  )

  if (APP_LINK)
    list(APPEND _args LINK ${APP_LINK})
  endif ()
  if (APP_INC)
    list(APPEND _args INC ${APP_INC})
  endif ()
  if (APP_DEFS)
    list(APPEND _args DEFS ${APP_DEFS})
  endif ()
  if (APP_FEATURES)
    list(APPEND _args FEATURES ${APP_FEATURES})
  endif ()
  if (APP_NO_INSTALL)
    list(APPEND _args NO_INSTALL)
  endif ()
  if (NOT APP_NO_UPX)
    list(APPEND _args ENABLE_UPX)
  endif ()

  _apex_add_executable(${_args})

  # Register for deferred package_<APP> target creation
  _apex_register_packagable_app(${APP_NAME})
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_tool(...)
#
# Define a CLI executable. Outputs to bin/tools/, installs unless NO_INSTALL.
#
# By default, tools require POSIX and are skipped on bare-metal builds.
# Use the BAREMETAL flag to opt a tool into bare-metal compilation.
#
# Arguments:
#   NAME        <target>           required
#   SRC         <files...>         required
#   LINK        <targets...>       optional
#   INC         <dirs...>          optional
#   DEFS        <defs...>          optional
#   FEATURES    <features...>      optional
#   REQUIRES    <reqs...>          optional (POSIX, OPENSSL, LINUX, etc.)
#   BAREMETAL                      optional flag (enables bare-metal build)
#   NO_INSTALL                     optional flag
# ------------------------------------------------------------------------------
function (apex_add_tool)
  cmake_parse_arguments(
    TOOL "NO_INSTALL;BAREMETAL" "NAME" "SRC;LINK;INC;DEFS;FEATURES;REQUIRES" ${ARGN}
  )
  apex_require(TOOL_NAME TOOL_SRC)

  # Default: require POSIX unless BAREMETAL flag is set
  set(_reqs ${TOOL_REQUIRES})
  if (NOT TOOL_BAREMETAL AND APEX_PLATFORM_BAREMETAL)
    # Skip on bare-metal unless explicitly opted in
    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] SKIP ${TOOL_NAME} (bare-metal, no BAREMETAL flag)")
    endif ()
    return()
  endif ()

  # Check explicit platform requirements
  if (_reqs)
    _apex_check_requirements(_satisfied NAME ${TOOL_NAME} REQUIRES ${_reqs})
    if (NOT _satisfied)
      return()
    endif ()
  endif ()

  set(_args
      NAME
      ${TOOL_NAME}
      SRC
      ${TOOL_SRC}
      OUTPUT_DIR
      "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/tools/cpp"
      KIND
      "TOOL"
  )

  if (TOOL_LINK)
    list(APPEND _args LINK ${TOOL_LINK})
  endif ()
  if (TOOL_INC)
    list(APPEND _args INC ${TOOL_INC})
  endif ()
  if (TOOL_DEFS)
    list(APPEND _args DEFS ${TOOL_DEFS})
  endif ()
  if (TOOL_FEATURES)
    list(APPEND _args FEATURES ${TOOL_FEATURES})
  endif ()
  if (TOOL_NO_INSTALL)
    list(APPEND _args NO_INSTALL)
  endif ()

  _apex_add_executable(${_args})

  # Track tool for aggregate target
  set_property(GLOBAL APPEND PROPERTY APEX_CPP_TOOLS ${TOOL_NAME})
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_library_cuda(...)
#
# Create a CUDA sidecar library for an existing CPU library.
# When CUDA inactive, creates INTERFACE shim linking the core.
#
# Skipped on bare-metal (CUDA not available) and when CORE target doesn't exist.
#
# Arguments:
#   NAME                   <target>           required
#   SRC                    <cu/cpp...>        required
#   CORE                   <cpu_target>       optional (link core PRIVATE)
#   TYPE                   STATIC|SHARED      optional (default SHARED)
#   INC                    <include_dir>      optional
#   DEPS_PUBLIC            <targets...>       optional
#   DEPS_PRIVATE           <targets...>       optional
#   SEPARABLE                                 optional flag
#   RESOLVE_DEVICE_SYMBOLS                    optional flag
#   NO_CUDART                                 optional flag
#   NO_NVML                                   optional flag
# ------------------------------------------------------------------------------
function (apex_add_library_cuda)
  cmake_parse_arguments(
    ACL "SEPARABLE;RESOLVE_DEVICE_SYMBOLS;NO_CUDART;NO_NVML" "NAME;CORE;TYPE;INC"
    "SRC;DEPS_PUBLIC;DEPS_PRIVATE" ${ARGN}
  )
  apex_require(ACL_NAME ACL_SRC)

  # Skip on bare-metal (no CUDA support)
  if (APEX_PLATFORM_BAREMETAL)
    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] SKIP ${ACL_NAME} (bare-metal, no CUDA)")
    endif ()
    return()
  endif ()

  # Skip if CORE target was skipped (e.g., due to BAREMETAL guard)
  if (ACL_CORE AND NOT TARGET "${ACL_CORE}")
    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] SKIP ${ACL_NAME} (CORE '${ACL_CORE}' not defined)")
    endif ()
    return()
  endif ()

  # CUDA active when toolkit found AND language enabled
  set(_cuda_active FALSE)
  if (CUDAToolkit_FOUND AND CMAKE_CUDA_COMPILER)
    set(_cuda_active TRUE)
  endif ()

  if (_cuda_active)
    # Real CUDA library
    if (NOT ACL_TYPE)
      set(ACL_TYPE "SHARED")
    endif ()
    if (ACL_TYPE STREQUAL "SHARED" AND APEX_STATIC_LINK)
      set(ACL_TYPE "STATIC")
    endif ()

    add_library(${ACL_NAME} ${ACL_TYPE})
    add_library(apex::${ACL_NAME} ALIAS ${ACL_NAME})

    if (ACL_INC)
      target_include_directories(${ACL_NAME} PRIVATE ${ACL_INC})
    endif ()

    if (ACL_CORE)
      apex_guard(${ACL_CORE})
      target_link_libraries(${ACL_NAME} PRIVATE ${ACL_CORE})
    endif ()

    # Wire CUDA sources
    set(_cuda_args FILES ${ACL_SRC})
    if (ACL_SEPARABLE)
      list(APPEND _cuda_args SEPARABLE)
    endif ()
    if (ACL_RESOLVE_DEVICE_SYMBOLS)
      list(APPEND _cuda_args RESOLVE_DEVICE_SYMBOLS)
    endif ()
    if (ACL_NO_CUDART)
      list(APPEND _cuda_args NO_CUDART)
    endif ()
    apex_cuda_sources(${ACL_NAME} ${_cuda_args})

    # Public cudart linkage for consumers
    if (NOT ACL_NO_CUDART AND TARGET CUDA::cudart)
      target_link_libraries(${ACL_NAME} PUBLIC CUDA::cudart)
      target_link_options(${ACL_NAME} INTERFACE -Wl,-rpath-link,$<TARGET_FILE_DIR:CUDA::cudart>)
    endif ()

    # NVML integration
    if (NOT ACL_NO_NVML)
      apex_nvml_enable(${ACL_NAME})
    endif ()

    if (ACL_DEPS_PUBLIC)
      target_link_libraries(${ACL_NAME} PUBLIC ${ACL_DEPS_PUBLIC})
    endif ()

    if (ACL_DEPS_PRIVATE)
      target_link_libraries(${ACL_NAME} PRIVATE ${ACL_DEPS_PRIVATE})
    endif ()

    install(
      TARGETS ${ACL_NAME}
      EXPORT apexTargets
      RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
      LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
      ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    if (APEX_TARGETS_VERBOSE)
      list(LENGTH ACL_SRC _src_count)
      message(
        STATUS "[apex] CUDA LIB ${ACL_NAME} (${ACL_TYPE}) core='${ACL_CORE}' srcs=${_src_count}"
      )
    endif ()

  else ()
    # INTERFACE shim when CUDA inactive
    add_library(${ACL_NAME} INTERFACE)
    add_library(apex::${ACL_NAME} ALIAS ${ACL_NAME})

    if (ACL_CORE)
      apex_guard(${ACL_CORE})
      target_link_libraries(${ACL_NAME} INTERFACE ${ACL_CORE})
    endif ()

    if (ACL_DEPS_PUBLIC)
      target_link_libraries(${ACL_NAME} INTERFACE ${ACL_DEPS_PUBLIC})
    endif ()

    install(TARGETS ${ACL_NAME} EXPORT apexTargets)

    if (APEX_TARGETS_VERBOSE)
      message(STATUS "[apex] CUDA SHIM ${ACL_NAME} (CUDA inactive) core='${ACL_CORE}'")
    endif ()
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_finalize_cpp_tools()
#
# Creates the ${PROJECT_NAME}_cpp_tools aggregate target from all registered tools.
# Call once at the end of tools/cpp/CMakeLists.txt.
# ------------------------------------------------------------------------------
function (apex_finalize_cpp_tools)
  get_property(_tools GLOBAL PROPERTY APEX_CPP_TOOLS)
  if (_tools)
    add_custom_target(${PROJECT_NAME}_cpp_tools DEPENDS ${_tools})
    if (APEX_TARGETS_VERBOSE)
      list(LENGTH _tools _count)
      message(STATUS "[apex] ${PROJECT_NAME}_cpp_tools aggregate target: ${_count} tools")
    endif ()
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# Export configuration (emitted once)
# ------------------------------------------------------------------------------
# Skip for bare-metal builds (no installable targets)
if (NOT APEX_PLATFORM_BAREMETAL)
  if (NOT DEFINED _APEX_TARGETS_EXPORT_INSTALLED)
    set(_APEX_TARGETS_EXPORT_INSTALLED
        TRUE
        CACHE INTERNAL ""
    )
    install(
      EXPORT apexTargets
      NAMESPACE apex::
      DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/apex
    )
  endif ()
endif ()
