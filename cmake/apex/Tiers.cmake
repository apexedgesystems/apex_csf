include_guard(GLOBAL)

# ==============================================================================
# Tiers.cmake - Build-tier classification and capability matrix
# ------------------------------------------------------------------------------
# Sets APEX_TIER to one of:
#
#   hosted       - Native Linux/macOS host build (POSIX, full stdlib, tests).
#   cross-linux  - Cross-compiled Linux target (Jetson, RPi, RISC-V).
#   baremetal    - Bare-metal MCU (no OS, restricted stdlib, static link only).
#   rtos         - RTOS build (Zephyr, etc.) — between baremetal and POSIX.
#
# Single source of truth for "what kind of build is this?".
#
# ------------------------------------------------------------------------------
# Capability matrix (informative)
# ------------------------------------------------------------------------------
# These are the broad assumptions that drive target factory gates in
# Targets.cmake, Coverage.cmake, Pch.cmake, etc. The actual checks live
# there; this matrix documents the contract.
#
#   capability    | hosted | cross-linux | baremetal | rtos
#   --------------+--------+-------------+-----------+-------
#   POSIX         |  yes   |    yes      |    no     | libc only
#   pthreads      |  yes   |    yes      |    no     | rtos-only
#   OpenSSL       |  yes   |    yes      |    no     | rare
#   Linux APIs    |  yes   |    yes      |    no     | no
#   CUDA          | maybe  |    maybe    |    no     | no
#   Dynamic link  |  yes   |    yes      |    no     | no
#   Tests (CTest) |  yes   |    no       |    no     | no (on-target)
#   Install       |  yes   |    yes      |    no     | no
#   PCH (apex)    |  yes   |    yes      |    no     | no
#
# "maybe" for CUDA = depends on CUDAToolkit_FOUND at configure time.
# Cross-linux skips CTest because tests would need on-target execution
# (qemu-user works for some, but most tests need real hardware).
#
# ------------------------------------------------------------------------------
# Helper
# ------------------------------------------------------------------------------
# apex_tier_is(<tier_name> <result_var>)
#   Sets <result_var> to TRUE if APEX_TIER == tier_name, FALSE otherwise.
#   Example:
#       apex_tier_is(baremetal _is_baremetal)
#       if (_is_baremetal) ...
# ==============================================================================

if (CMAKE_SYSTEM_NAME STREQUAL "Generic")
  set(APEX_TIER
      "baremetal"
      CACHE INTERNAL "Build tier (hosted | cross-linux | baremetal | rtos)"
  )
elseif (CMAKE_SYSTEM_NAME STREQUAL "Zephyr")
  set(APEX_TIER
      "rtos"
      CACHE INTERNAL "Build tier (hosted | cross-linux | baremetal | rtos)"
  )
elseif (CMAKE_CROSSCOMPILING)
  set(APEX_TIER
      "cross-linux"
      CACHE INTERNAL "Build tier (hosted | cross-linux | baremetal | rtos)"
  )
else ()
  set(APEX_TIER
      "hosted"
      CACHE INTERNAL "Build tier (hosted | cross-linux | baremetal | rtos)"
  )
endif ()

message(STATUS "[apex] Tier: ${APEX_TIER}")

function (apex_tier_is _tier _result)
  if (APEX_TIER STREQUAL _tier)
    set(${_result}
        TRUE
        PARENT_SCOPE
    )
  else ()
    set(${_result}
        FALSE
        PARENT_SCOPE
    )
  endif ()
endfunction ()
