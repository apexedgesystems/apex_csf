# ==============================================================================
# apex/Firmware.cmake - Bare-metal firmware target factory
# ==============================================================================
#
# Provides apex_add_firmware() for creating bare-metal executables.
# Active only when CMAKE_SYSTEM_NAME is "Generic" (bare-metal toolchain).
#
# Supported platforms (via APEX_HAL_PLATFORM):
#   stm32  - ARM Cortex-M: custom linker script, -nostartfiles/-nostdlib, .bin+.hex
#   avr    - AVR ATmega: avr-libc provides crt0 and linker script, .hex only
#   pico   - RP2040: Pico SDK provides startup, linker script, UF2 generation
#   esp32  - ESP32-S3: ESP-IDF provides startup, linker, FreeRTOS, NVS
#   c2000  - TI C28x DSP: TI CGT compiler, .cmd linker script, .hex via hex2000
#
# All platform-specific configuration (HAL sources, RTOS sources, linker
# scripts, include paths) belongs in the application CMakeLists.txt.
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
  set(_elf "${CMAKE_BINARY_DIR}/firmware/${_target}.elf")
  set(_bin "${CMAKE_BINARY_DIR}/firmware/${_target}.bin")
  set(_hex "${CMAKE_BINARY_DIR}/firmware/${_target}.hex")
  add_custom_command(
    TARGET ${_target}
    POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary ${_elf} ${_bin}
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${_elf} ${_hex}
    COMMAND ${CMAKE_SIZE} --format=berkeley ${_elf}
    COMMENT "[firmware] Generating ${_target}.bin and ${_target}.hex"
    VERBATIM
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
  set(_elf "${CMAKE_BINARY_DIR}/firmware/${_target}.elf")
  set(_hex "${CMAKE_BINARY_DIR}/firmware/${_target}.hex")
  if (_mcu)
    add_custom_command(
      TARGET ${_target}
      POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O ihex ${_elf} ${_hex}
      COMMAND ${CMAKE_SIZE} --format=avr --mcu=${_mcu} ${_elf}
      COMMENT "[firmware] Generating ${_target}.hex (AVR ${_mcu})"
      VERBATIM
    )
  else ()
    add_custom_command(
      TARGET ${_target}
      POST_BUILD
      COMMAND ${CMAKE_OBJCOPY} -O ihex ${_elf} ${_hex}
      COMMAND ${CMAKE_SIZE} ${_elf}
      COMMENT "[firmware] Generating ${_target}.hex (AVR)"
      VERBATIM
    )
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
  set(_elf "${CMAKE_BINARY_DIR}/firmware/${_target}.elf")
  add_custom_command(
    TARGET ${_target}
    POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=berkeley ${_elf}
    COMMENT "[firmware] ${_target} size report (Pico RP2040)"
    VERBATIM
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
  set(_elf "${CMAKE_BINARY_DIR}/firmware/${_target}.elf")
  add_custom_command(
    TARGET ${_target}
    POST_BUILD
    COMMAND ${CMAKE_SIZE} --format=berkeley ${_elf}
    COMMENT "[firmware] ${_target} size report (ESP32-S3)"
    VERBATIM
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
# apex_finalize_firmware()
#
# Creates the firmware aggregate target from all registered firmware.
# Call once at the end of apps that use apex_add_firmware().
# ------------------------------------------------------------------------------
function (apex_finalize_firmware)
  get_property(_targets GLOBAL PROPERTY APEX_FIRMWARE_TARGETS)
  if (_targets)
    add_custom_target(firmware DEPENDS ${_targets})
    if (APEX_TARGETS_VERBOSE)
      list(LENGTH _targets _count)
      message(STATUS "[apex] firmware aggregate target: ${_count} targets")
    endif ()
  endif ()
endfunction ()
