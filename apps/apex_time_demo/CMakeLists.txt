# ==============================================================================
# apps/apex_time_demo - PPS Time Distribution Demo
#
# Standalone demonstration of TimeServer + IPps end-to-end. Shows boot
# convergence (NONE -> FINE -> PRECISE), TNT broadcast capture, ATS
# AT_TIME trigger firing on UTC, and degradation (PPS loss -> STALE ->
# FREERUN) and recovery via resetCorrelation. No real GPS or
# /dev/pps[N] needed; uses MockPps for full determinism.
# ==============================================================================

if (NOT APEX_PLATFORM_BAREMETAL)
  add_subdirectory(exec)
endif ()
