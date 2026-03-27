# ==============================================================================
# system_core_support_system_monitor - Runtime system health monitor
#
# Monitors CPU load, temperature, memory, FD usage, RT throttle events, and
# optionally GPU telemetry. Uses seeker diagnostics library for platform-
# portable /proc and /sys reads.
#
# Domains (TPRM-configurable):
#   CPU    - per-core utilization, thermal zone, RT throttle
#   Memory - RAM/swap usage, file descriptor count
#   GPU    - temperature, utilization, throttle (CUDA builds only)
# ==============================================================================

include(apex/All)
apex_module(system_core_support_system_monitor)

# ------------------------------------------------------------------------------
# Sources (1 file)
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/SystemMonitor.cpp")

# ------------------------------------------------------------------------------
# Library
# ------------------------------------------------------------------------------

set(SEEKER_DEPS seeker_cpu seeker_memory seeker_system seeker_timing seeker_helpers)

# Conditionally add GPU domain (when CUDA toolkit is available).
# seeker_gpu links NVML as PUBLIC, but SystemMonitor uses DEPS_PRIVATE for
# seeker. The NVML shared lib needs to be on the link line of any executable
# that transitively loads libseeker_gpu.so at runtime. We use INTERFACE on
# the SystemMonitor target so consumers get the NVML rpath without exposing
# seeker internals in the public API.
if (CUDAToolkit_FOUND AND TARGET seeker_gpu)
  list(APPEND SEEKER_DEPS seeker_gpu)
endif ()

apex_add_library(
  NAME
  ${LIB_NAME}
  TYPE
  SHARED
  INC
  "${INC_DIR}"
  SRC
  ${CPP_SOURCES}
  DEPS_PUBLIC
  system_core_system_component
  DEPS_PRIVATE
  ${SEEKER_DEPS}
  utilities_helpers
  fmt::fmt
  REQUIRES
  POSIX
)

# NVML transitive linkage: seeker_gpu links libnvidia-ml as PUBLIC, but since
# SystemMonitor links seeker_gpu as PRIVATE, the NVML dependency does not
# propagate to executables that link SystemMonitor. Add it explicitly so the
# linker can resolve seeker_gpu's NVML symbols in cross-compile (stub) and
# native (runtime) builds.
if (CUDAToolkit_FOUND AND TARGET seeker_gpu)
  get_target_property(_NVML_LIBS seeker_gpu INTERFACE_LINK_LIBRARIES)
  foreach (_lib IN LISTS _NVML_LIBS)
    if ("${_lib}" MATCHES "nvidia-ml")
      target_link_libraries(${LIB_NAME} PUBLIC "${_lib}")
      break()
    endif ()
  endforeach ()
endif ()

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})

apex_add_data_manifest()

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst)
