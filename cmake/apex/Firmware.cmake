# ==============================================================================
# apex/Firmware.cmake - Bare-metal firmware target factory
# ==============================================================================
#
# Provides apex_add_firmware() for creating bare-metal executables, plus
# apex_add_firmware_with_<vendor>() wrappers that assemble each vendor's SDK
# sources, includes, and link wiring so apps declare only their chip and the
# modules they use. Active only when CMAKE_SYSTEM_NAME is "Generic".
#
# Supported platforms (via APEX_HAL_PLATFORM):
#   stm32  - ARM Cortex-M: custom linker script, -nostartfiles/-nostdlib, .bin+.hex
#   avr    - AVR ATmega: avr-libc provides crt0 and linker script, .hex only
#   pico   - RP2040: Pico SDK provides startup, linker script, UF2 generation
#   esp32  - ESP32-S3: ESP-IDF provides startup, linker, FreeRTOS, NVS
#   c2000  - TI C28x DSP: TI CGT compiler, .cmd linker script, .hex via hex2000
#
# apex_add_firmware() stays vendor-agnostic: an app can call it directly (AVR
# needs no SDK) or go through a with_<vendor> wrapper. Vendor SDK assembly lives
# in the wrapper; app-specific middleware (e.g. FreeRTOS) stays in the app.
# ==============================================================================

include_guard(GLOBAL)

# Skip entirely on non-bare-metal builds
if (NOT APEX_PLATFORM_BAREMETAL)
  # Provide stub function that errors if called on wrong platform
  function (apex_add_firmware)
    cmake_parse_arguments(FW "" "NAME" "" ${ARGN})
    message(
      FATAL_ERROR
        "[apex] apex_add_firmware(${FW_NAME}) called but CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}'. "
        "This function is only available for bare-metal toolchains."
    )
  endfunction ()
  return()
endif ()

# Enable ASM for startup files (bare-metal targets typically need .s sources)
enable_language(ASM)

# ==============================================================================
# Platform Helpers (internal)
# ==============================================================================

# ------------------------------------------------------------------------------
# _apex_firmware_postbuild(<target> COMMENT <str> [BIN] [HEX]
#                          [SIZE_FORMAT <fmt>] [SIZE_MCU <mcu>])
#
# Shared POST_BUILD step for the GCC-based platforms: optional objcopy to .bin
# and/or .hex followed by a size report, emitted as one custom command. BIN/HEX
# use ${CMAKE_OBJCOPY}; the size report uses ${CMAKE_SIZE} (--format=<fmt> when
# given, plus --mcu=<mcu> for the avr format). c2000 keeps its own postbuild --
# it uses the TI ofd2000/hex2000 tools, not the GCC binutils.
# ------------------------------------------------------------------------------
function (_apex_firmware_postbuild _target)
  cmake_parse_arguments(PB "BIN;HEX" "COMMENT;SIZE_FORMAT;SIZE_MCU" "" ${ARGN})

  set(_elf "${CMAKE_BINARY_DIR}/firmware/${_target}.elf")

  # objcopy steps run before the (always-present) size report. They are
  # collected as extra COMMANDs prepended to the literal size COMMAND below.
  set(_objcopy)
  if (PB_BIN)
    list(
      APPEND
      _objcopy
      COMMAND
      ${CMAKE_OBJCOPY}
      -O
      binary
      ${_elf}
      "${CMAKE_BINARY_DIR}/firmware/${_target}.bin"
    )
  endif ()
  if (PB_HEX)
    list(
      APPEND
      _objcopy
      COMMAND
      ${CMAKE_OBJCOPY}
      -O
      ihex
      ${_elf}
      "${CMAKE_BINARY_DIR}/firmware/${_target}.hex"
    )
  endif ()

  set(_size_args)
  if (PB_SIZE_MCU)
    set(_size_args --format=${PB_SIZE_FORMAT} --mcu=${PB_SIZE_MCU})
  elseif (PB_SIZE_FORMAT)
    set(_size_args --format=${PB_SIZE_FORMAT})
  endif ()

  add_custom_command(
    TARGET ${_target}
    POST_BUILD ${_objcopy}
    COMMAND ${CMAKE_SIZE} ${_size_args} ${_elf}
    COMMENT "${PB_COMMENT}"
    VERBATIM
  )
endfunction ()

# ------------------------------------------------------------------------------
# _apex_firmware_stm32(<target> <linker_script> <mcu>)
#
# STM32 (ARM Cortex-M): custom linker script, -nostdlib, .bin + .hex.
# ------------------------------------------------------------------------------
function (_apex_firmware_stm32 _target _linker_script _mcu)
  # Linker script is required for STM32
  if (NOT _linker_script)
    message(FATAL_ERROR "[apex] LINKER_SCRIPT is required for STM32 firmware '${_target}'")
  endif ()
  if (NOT EXISTS "${_linker_script}")
    message(FATAL_ERROR "[apex] Linker script not found: ${_linker_script}")
  endif ()

  # MCU compile definition + HAL driver flag
  if (_mcu)
    target_compile_definitions(${_target} PRIVATE ${_mcu} USE_HAL_DRIVER)
  endif ()

  # Link options
  target_link_options(
    ${_target}
    PRIVATE
    -T${_linker_script}
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${_target}.map
    -nostartfiles
    -nostdlib
    -lc
    -lgcc
  )

  # Linker script dependency
  set_target_properties(${_target} PROPERTIES LINK_DEPENDS "${_linker_script}")

  # Post-build: .bin + .hex + berkeley size
  _apex_firmware_postbuild(
    ${_target}
    BIN
    HEX
    SIZE_FORMAT
    berkeley
    COMMENT
    "[firmware] Generating ${_target}.bin and ${_target}.hex"
  )
endfunction ()

# ------------------------------------------------------------------------------
# _apex_firmware_avr(<target> <mcu>)
#
# AVR ATmega: avr-libc provides crt0, vector table, and linker script via
# -mmcu= (set in toolchain). Generates .hex for avrdude.
# ------------------------------------------------------------------------------
function (_apex_firmware_avr _target _mcu)
  # Link options (gc-sections is already in toolchain CMAKE_EXE_LINKER_FLAGS_INIT)
  target_link_options(${_target} PRIVATE -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${_target}.map)

  # Post-build: .hex + size report (with MCU percentages when MCU is provided)
  if (_mcu)
    _apex_firmware_postbuild(
      ${_target}
      HEX
      SIZE_FORMAT
      avr
      SIZE_MCU
      ${_mcu}
      COMMENT
      "[firmware] Generating ${_target}.hex (AVR ${_mcu})"
    )
  else ()
    _apex_firmware_postbuild(${_target} HEX COMMENT "[firmware] Generating ${_target}.hex (AVR)")
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# _apex_firmware_pico(<target>)
#
# RP2040 Pico: SDK provides startup, linker script, stdlib, and map file.
# App calls pico_add_extra_outputs() after apex_add_firmware() for UF2.
# ------------------------------------------------------------------------------
function (_apex_firmware_pico _target)
  # No additional link options needed -- Pico SDK handles everything via
  # INTERFACE target properties on pico_stdlib.

  # Post-build: size report only (SDK generates .uf2, .bin, .hex, .map, .dis)
  _apex_firmware_postbuild(
    ${_target} SIZE_FORMAT berkeley COMMENT "[firmware] ${_target} size report (Pico RP2040)"
  )
endfunction ()

# ------------------------------------------------------------------------------
# _apex_firmware_esp32(<target>)
#
# ESP32-S3 (Xtensa LX7): ESP-IDF provides startup, linker scripts, and FreeRTOS.
# App calls idf_build_process()/idf_build_executable() around apex_add_firmware().
# Binary generation handled by ESP-IDF (bootloader + partition table + app).
# ------------------------------------------------------------------------------
function (_apex_firmware_esp32 _target)
  # No additional link options needed -- ESP-IDF handles everything via
  # idf_build_executable() called in the app CMakeLists.txt.

  # Post-build: size report only (ESP-IDF generates .bin via esptool)
  _apex_firmware_postbuild(
    ${_target} SIZE_FORMAT berkeley COMMENT "[firmware] ${_target} size report (ESP32-S3)"
  )
endfunction ()

# ------------------------------------------------------------------------------
# _apex_firmware_c2000(<target> <linker_script> <mcu>)
#
# TI C2000 (C28x DSP): TI CGT compiler, .cmd linker script.
# Generates .hex and .bin via hex2000 for UniFlash programming.
# ------------------------------------------------------------------------------
function (_apex_firmware_c2000 _target _linker_script _mcu)
  # Linker command file is required for C2000
  if (NOT _linker_script)
    message(FATAL_ERROR "[apex] LINKER_SCRIPT is required for C2000 firmware '${_target}'")
  endif ()
  if (NOT EXISTS "${_linker_script}")
    message(FATAL_ERROR "[apex] Linker command file not found: ${_linker_script}")
  endif ()

  # MCU compile definition
  if (_mcu)
    target_compile_definitions(${_target} PRIVATE ${_mcu})
  endif ()

  # TI linker uses -z for link phase, linker cmd via -l or positional
  target_link_options(
    ${_target} PRIVATE -z -m${CMAKE_CURRENT_BINARY_DIR}/${_target}.map ${_linker_script}
  )

  # Linker script dependency
  set_target_properties(${_target} PROPERTIES LINK_DEPENDS "${_linker_script}")

  # Post-build: .hex via hex2000 + size report via ofd2000
  set(_elf "${CMAKE_BINARY_DIR}/firmware/${_target}.elf")
  set(_hex "${CMAKE_BINARY_DIR}/firmware/${_target}.hex")
  if (C2000_HEX)
    add_custom_command(
      TARGET ${_target}
      POST_BUILD
      COMMAND ${C2000_HEX} --intel -o ${_hex} ${_elf}
      COMMENT "[firmware] Generating ${_target}.hex (C2000 F28004x)"
      VERBATIM
    )
  endif ()
  if (C2000_SIZE)
    add_custom_command(
      TARGET ${_target}
      POST_BUILD
      COMMAND ${C2000_SIZE} ${_elf}
      COMMENT "[firmware] ${_target} section report (C2000)"
      VERBATIM
    )
  endif ()
endfunction ()

# ==============================================================================
# Public API
# ==============================================================================

# ------------------------------------------------------------------------------
# apex_add_firmware(...)
#
# Define a bare-metal firmware executable with binary output generation.
# Platform is auto-detected from APEX_HAL_PLATFORM (set by CMake preset).
#
# Arguments:
#   NAME          <target>           required
#   SRC           <files...>         required
#   LINKER_SCRIPT <path>             required (ARM), not used (AVR/Pico)
#   MCU           <mcu_define>       optional -- ARM: compile definition
#                                               AVR: avr-size --mcu= name
#   LINK          <targets...>       optional
#   INC           <dirs...>          optional
#   DEFS          <defs...>          optional
#   STACK_SIZE    <bytes>            optional (default: 0x800, ARM only)
#   HEAP_SIZE     <bytes>            optional (default: 0x400, ARM only)
#
# Supported platforms:
#   stm32  -> ARM Cortex-M (custom linker script, -nostdlib)
#   avr    -> AVR ATmega (avr-libc linking)
#   pico   -> RP2040 (Pico SDK provides linker/startup)
#   esp32  -> ESP32-S3 (ESP-IDF provides linker/startup/FreeRTOS)
# ------------------------------------------------------------------------------
function (apex_add_firmware)
  cmake_parse_arguments(
    FW "" "NAME;LINKER_SCRIPT;MCU;STACK_SIZE;HEAP_SIZE" "SRC;LINK;INC;DEFS" ${ARGN}
  )
  apex_require(FW_NAME FW_SRC)

  # Platform validation
  set(_supported_platforms stm32 avr pico esp32 c2000)
  if (NOT APEX_HAL_PLATFORM IN_LIST _supported_platforms)
    message(FATAL_ERROR "[apex] Unsupported firmware platform: '${APEX_HAL_PLATFORM}'. "
                        "Supported platforms: ${_supported_platforms}"
    )
  endif ()

  # Defaults (ARM only -- AVR stack starts at RAMEND, no configurable heap)
  if (NOT FW_STACK_SIZE)
    set(FW_STACK_SIZE "0x800")
  endif ()
  if (NOT FW_HEAP_SIZE)
    set(FW_HEAP_SIZE "0x400")
  endif ()

  # Create executable
  add_executable(${FW_NAME})
  target_sources(${FW_NAME} PRIVATE ${FW_SRC})

  # User definitions
  if (FW_DEFS)
    target_compile_definitions(${FW_NAME} PRIVATE ${FW_DEFS})
  endif ()

  # Include directories
  if (FW_INC)
    target_include_directories(${FW_NAME} PRIVATE ${FW_INC})
  endif ()

  # Link libraries
  if (FW_LINK)
    target_link_libraries(${FW_NAME} PRIVATE ${FW_LINK})
  endif ()

  # C++ bare-metal flags (-fno-exceptions, --no_rtti, etc.) are set in each
  # platform's toolchain file via CMAKE_CXX_FLAGS_INIT. No per-target flags
  # needed here -- keeps the generic factory compiler-agnostic.

  # Output directory and properties
  set_target_properties(
    ${FW_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/firmware" SUFFIX ".elf"
  )

  # Dispatch to platform helper
  if (APEX_HAL_PLATFORM STREQUAL "stm32")
    _apex_firmware_stm32(${FW_NAME} "${FW_LINKER_SCRIPT}" "${FW_MCU}")
  elseif (APEX_HAL_PLATFORM STREQUAL "avr")
    _apex_firmware_avr(${FW_NAME} "${FW_MCU}")
  elseif (APEX_HAL_PLATFORM STREQUAL "pico")
    _apex_firmware_pico(${FW_NAME})
  elseif (APEX_HAL_PLATFORM STREQUAL "esp32")
    _apex_firmware_esp32(${FW_NAME})
  elseif (APEX_HAL_PLATFORM STREQUAL "c2000")
    _apex_firmware_c2000(${FW_NAME} "${FW_LINKER_SCRIPT}" "${FW_MCU}")
  endif ()

  # Track firmware for aggregate target
  set_property(GLOBAL APPEND PROPERTY APEX_FIRMWARE_TARGETS ${FW_NAME})

  if (APEX_TARGETS_VERBOSE)
    list(LENGTH FW_SRC _src_count)
    message(
      STATUS
        "[apex] FIRMWARE ${FW_NAME} mcu='${FW_MCU}' srcs=${_src_count} platform=${APEX_HAL_PLATFORM}"
    )
  endif ()
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_firmware_with_stm32(...)
#
# apex_add_firmware() plus the STM32Cube HAL/CMSIS source + include assembly, so
# an STM32 app declares only its chip and the HAL modules it uses instead of
# repeating the ~70-line vendor preamble. The Cube tree is found via
# STM32CUBE_<CUBE>_PATH (default /opt/STM32Cube<CUBE>).
#
# Arguments:
#   NAME          <target>          required
#   SRC           <files...>        required (app sources)
#   CUBE          <F4|L4>           required (STM32Cube edition)
#   MCU           <define>          required (e.g. STM32L476xx)
#   STARTUP       <name>            required (gcc startup, e.g. startup_stm32l476xx)
#   HAL_MODULES   <names...>        required (e.g. uart dma gpio; the base
#                                   stm32<fam>xx_hal.c is always included)
#   LINKER_SCRIPT <path>            required
#   LINK/INC/DEFS <...>             optional (forwarded to apex_add_firmware)
# ------------------------------------------------------------------------------
function (apex_add_firmware_with_stm32)
  cmake_parse_arguments(
    ST "" "NAME;CUBE;MCU;STARTUP;LINKER_SCRIPT" "SRC;HAL_MODULES;LINK;INC;DEFS" ${ARGN}
  )
  apex_require(
    ST_NAME
    ST_SRC
    ST_CUBE
    ST_MCU
    ST_STARTUP
    ST_LINKER_SCRIPT
    ST_HAL_MODULES
  )

  set(_cube "$ENV{STM32CUBE_${ST_CUBE}_PATH}")
  if (NOT _cube)
    set(_cube "/opt/STM32Cube${ST_CUBE}")
  endif ()
  string(TOLOWER "${ST_CUBE}" _fam) # L4 -> l4

  set(_hal "${_cube}/Drivers/STM32${ST_CUBE}xx_HAL_Driver")
  set(_cmsis "${_cube}/Drivers/CMSIS/Device/ST/STM32${ST_CUBE}xx")

  set(_vendor_src
      "${_hal}/Src/stm32${_fam}xx_hal.c" "${_cmsis}/Source/Templates/system_stm32${_fam}xx.c"
      "${_cmsis}/Source/Templates/gcc/${ST_STARTUP}.s"
  )
  foreach (_m IN LISTS ST_HAL_MODULES)
    list(APPEND _vendor_src "${_hal}/Src/stm32${_fam}xx_hal_${_m}.c")
  endforeach ()

  apex_add_firmware(
    NAME
    ${ST_NAME}
    SRC
    ${ST_SRC}
    ${_vendor_src}
    LINKER_SCRIPT
    "${ST_LINKER_SCRIPT}"
    MCU
    ${ST_MCU}
    INC
    ${ST_INC}
    LINK
    ${ST_LINK}
    DEFS
    ${ST_DEFS}
  )

  # ST vendor headers are warning-noisy -- include them as SYSTEM.
  target_include_directories(
    ${ST_NAME} SYSTEM PRIVATE "${_cube}/Drivers/CMSIS/Include" "${_cmsis}/Include" "${_hal}/Inc"
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_firmware_with_c2000(...)
#
# apex_add_firmware() plus the C2000Ware device + driverlib wiring: the
# codestart bootstrap, driverlib include paths, the pre-compiled driverlib
# archive, and the codestart entry point. The C2000Ware tree is found via
# C2000WARE_ROOT (default /opt/ti/c2000ware-core-sdk). Device-specific local
# files (e.g. a patched device.c) stay in the app's SRC.
#
# Arguments:
#   NAME          <target>          required
#   SRC           <files...>        required (app sources + local device.c)
#   DEVICE        <family>          required (e.g. f28004x)
#   MCU           <define>          required (board define, e.g. _LAUNCHXL_F280049C)
#   LINKER_SCRIPT <path>            required
#   INC/LINK/DEFS <...>             optional (forwarded to apex_add_firmware)
# ------------------------------------------------------------------------------
function (apex_add_firmware_with_c2000)
  cmake_parse_arguments(C2 "" "NAME;DEVICE;MCU;LINKER_SCRIPT" "SRC;INC;LINK;DEFS" ${ARGN})
  apex_require(C2_NAME C2_SRC C2_DEVICE C2_MCU C2_LINKER_SCRIPT)

  set(_root "$ENV{C2000WARE_ROOT}")
  if (NOT _root)
    set(_root "/opt/ti/c2000ware-core-sdk")
  endif ()

  set(_common "${_root}/device_support/${C2_DEVICE}/common")
  set(_driverlib "${_root}/driverlib/${C2_DEVICE}/driverlib")

  apex_add_firmware(
    NAME
    ${C2_NAME}
    SRC
    ${C2_SRC}
    "${_common}/source/${C2_DEVICE}_codestartbranch.asm"
    LINKER_SCRIPT
    "${C2_LINKER_SCRIPT}"
    MCU
    ${C2_MCU}
    INC
    ${C2_INC}
    "${_common}/include"
    "${_driverlib}"
    "${_driverlib}/inc"
    LINK
    ${C2_LINK}
    DEFS
    ${C2_DEFS}
  )

  # Entry point matches the codestart label in <device>_codestartbranch.asm, and
  # the pre-compiled driverlib archive provides GPIO/SysCtl/SCI/CAN/Timer/Flash.
  target_link_options(
    ${C2_NAME} PRIVATE "-ecode_start" "-l${_driverlib}/ccs/Release/driverlib_coff.lib"
  )
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_firmware_with_pico(...)
#
# apex_add_firmware() plus the Pico SDK handshake: SDK init, the hardware
# library links, stdio routing, and the .uf2/.hex/.bin/.map/.dis outputs. The
# SDK is found via PICO_SDK_PATH (default /opt/pico-sdk). This app is only ever
# configured inside the pico cross build, where the SDK is present.
#
# Arguments:
#   NAME      <target>          required
#   SRC       <files...>        required
#   PICO_LIBS <libs...>         optional (Pico SDK libs, e.g. pico_stdlib
#                               hardware_uart hardware_flash)
#   INC/LINK  <...>             optional (forwarded to apex_add_firmware)
# ------------------------------------------------------------------------------
function (apex_add_firmware_with_pico)
  cmake_parse_arguments(P "" "NAME" "SRC;INC;LINK;PICO_LIBS" ${ARGN})
  apex_require(P_NAME P_SRC)

  set(PICO_SDK_PATH
      "$ENV{PICO_SDK_PATH}"
      CACHE PATH "Pico SDK path"
  )
  if (NOT PICO_SDK_PATH)
    set(PICO_SDK_PATH "/opt/pico-sdk")
  endif ()
  include("${PICO_SDK_PATH}/pico_sdk_init.cmake")
  pico_sdk_init()

  apex_add_firmware(
    NAME
    ${P_NAME}
    SRC
    ${P_SRC}
    INC
    ${P_INC}
    LINK
    ${P_LINK}
  )

  if (P_PICO_LIBS)
    target_link_libraries(${P_NAME} PRIVATE ${P_PICO_LIBS})
  endif ()

  # USB CDC enabled for picotool remote BOOTSEL (standard flash workflow); UART
  # stdio disabled because the app drives the UARTs directly.
  pico_enable_stdio_usb(${P_NAME} 1)
  pico_enable_stdio_uart(${P_NAME} 0)
  pico_add_extra_outputs(${P_NAME})
endfunction ()

# ------------------------------------------------------------------------------
# apex_add_firmware_with_esp32(...)
#
# apex_add_firmware() plus the ESP-IDF handshake: the IDF build process for the
# esp32s3 target, the idf:: component links, and the bootloader/partition image
# generation. IDF is found via IDF_PATH. This app is only ever configured inside
# the esp32 cross build, where IDF is present.
#
# Arguments:
#   NAME       <target>         required
#   SRC        <files...>       required
#   COMPONENTS <names...>       required (IDF components to build, e.g. freertos
#                               driver nvs_flash esp_timer esptool_py)
#   IDF_LIBS   <libs...>        optional (idf:: targets to link)
#   INC/LINK   <...>            optional (forwarded to apex_add_firmware)
# ------------------------------------------------------------------------------
function (apex_add_firmware_with_esp32)
  cmake_parse_arguments(E "" "NAME" "SRC;INC;LINK;COMPONENTS;IDF_LIBS" ${ARGN})
  apex_require(E_NAME E_SRC E_COMPONENTS)

  include($ENV{IDF_PATH}/tools/cmake/idf.cmake)
  idf_build_process(
    "esp32s3"
    COMPONENTS
    ${E_COMPONENTS}
    SDKCONFIG
    "${CMAKE_BINARY_DIR}/sdkconfig"
    SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_SOURCE_DIR}/sdkconfig.defaults"
    BUILD_DIR
    "${CMAKE_BINARY_DIR}"
  )

  apex_add_firmware(
    NAME
    ${E_NAME}
    SRC
    ${E_SRC}
    INC
    ${E_INC}
    LINK
    ${E_LINK}
  )

  if (E_IDF_LIBS)
    target_link_libraries(${E_NAME} PRIVATE ${E_IDF_LIBS})
  endif ()
  idf_build_executable(${E_NAME})
endfunction ()

# ------------------------------------------------------------------------------
# apex_finalize_firmware()
#
# Creates (or extends) the firmware aggregate target from all registered
# firmware. Idempotent so every firmware app can call it: platforms that
# host several apps (two stm32 demos in one configure) compose instead of
# colliding on the aggregate target.
# ------------------------------------------------------------------------------
function (apex_finalize_firmware)
  get_property(_targets GLOBAL PROPERTY APEX_FIRMWARE_TARGETS)
  if (_targets)
    if (NOT TARGET firmware)
      add_custom_target(firmware)
    endif ()
    add_dependencies(firmware ${_targets})
    if (APEX_TARGETS_VERBOSE)
      list(LENGTH _targets _count)
      message(STATUS "[apex] firmware aggregate target: ${_count} targets")
    endif ()
  endif ()
endfunction ()
