# ==============================================================================
# apps/apex_ops_demo/CMakeLists.txt - Ops Demo Application
#
# Pure SIL demo for Zenith C2 system development. Configurable waveform
# generators with rich telemetry, exercising every APROTO capability.
# ==============================================================================

if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(wave)
  add_subdirectory(exec)
  add_subdirectory(test/plugin)
endif ()
