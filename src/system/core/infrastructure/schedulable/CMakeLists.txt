# ==============================================================================
# Module: system_core_schedulable
# ==============================================================================
# Minimal task abstractions for RT-friendly scheduling.
#
# Components:
# - SchedulableTaskBase   - Pure interface (callable + label)
# - SchedulableTask       - Concrete minimal task (header-only)
# - SchedulableTaskCUDA   - CUDA-enabled task with stream/event support
#
# Design:
#   Tasks are minimal (just callable + label). All scheduling configuration
#   (frequency, priority, affinity) lives in the scheduler via TaskConfig.
#
# Libraries:
#   system_core_schedulable      - CPU task library
#   system_core_schedulable_cuda - CUDA-enabled task support
# ==============================================================================

include(apex/All)
apex_module(system_core_schedulable)

# ------------------------------------------------------------------------------
# Sources
# ------------------------------------------------------------------------------

set(CPP_SOURCES "${SRC_DIR}/SchedulableTaskBase.cpp")

set(CUDA_SOURCES "${SRC_DIR}/SchedulableTaskCUDA.cu")

# ------------------------------------------------------------------------------
# CPU Library
# ------------------------------------------------------------------------------

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
  utilities_concurrency
  utilities_helpers
  utilities_compatibility
  fmt::fmt
)

# ------------------------------------------------------------------------------
# CUDA Library
# ------------------------------------------------------------------------------

apex_add_library_cuda(NAME ${LIB_NAME}_cuda CORE ${LIB_NAME} SRC ${CUDA_SOURCES})

# ------------------------------------------------------------------------------
# Opt-ins (coverage, doxygen, UPX)
# ------------------------------------------------------------------------------

apex_standard_optins(${LIB_NAME})
apex_standard_optins(${LIB_NAME}_cuda)

# ------------------------------------------------------------------------------
# Subdirectories
# ------------------------------------------------------------------------------

apex_add_test_subdirs(utst ptst)
