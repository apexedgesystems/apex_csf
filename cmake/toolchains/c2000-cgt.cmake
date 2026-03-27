# ==============================================================================
# c2000-cgt.cmake - TI C2000 Code Generation Tools toolchain
# ==============================================================================
#
# Target: TMS320F28004x (C28x DSP core)
# Compiler: TI cl2000 (C2000 Code Generation Tools)
#
# Usage:
#   cmake --preset c2000-baremetal
#   cmake --build --preset c2000-baremetal
#
# Environment:
#   C2000_CGT_ROOT  - TI CGT install path (default: /opt/ti/c2000-cgt)
#   C2000WARE_ROOT  - C2000Ware SDK path (default: /opt/ti/c2000ware-core-sdk)
#
# Note: CMake does not natively understand TI compiler dialect flags.
# C++17 is enabled via --c++17 in CMAKE_CXX_FLAGS_INIT and
# CMAKE_CXX_STANDARD is disabled to avoid CMake trying to add its own flags.
# ==============================================================================

# Bare-metal (no OS)
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR c2000)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ------------------------------------------------------------------------------
# CGT Root
# ------------------------------------------------------------------------------

if (DEFINED ENV{C2000_CGT_ROOT})
  set(C2000_CGT_ROOT "$ENV{C2000_CGT_ROOT}")
else ()
  set(C2000_CGT_ROOT "/opt/ti/c2000-cgt")
endif ()

if (DEFINED ENV{C2000WARE_ROOT})
  set(C2000WARE_ROOT "$ENV{C2000WARE_ROOT}")
else ()
  set(C2000WARE_ROOT "/opt/ti/c2000ware-core-sdk")
endif ()

# ------------------------------------------------------------------------------
# Compilers
# ------------------------------------------------------------------------------

set(CMAKE_C_COMPILER "${C2000_CGT_ROOT}/bin/cl2000")
set(CMAKE_CXX_COMPILER "${C2000_CGT_ROOT}/bin/cl2000")
set(CMAKE_ASM_COMPILER "${C2000_CGT_ROOT}/bin/cl2000")
set(CMAKE_AR "${C2000_CGT_ROOT}/bin/ar2000")
set(CMAKE_LINKER "${C2000_CGT_ROOT}/bin/cl2000")

# TI hex utility for output conversion
set(C2000_HEX "${C2000_CGT_ROOT}/bin/hex2000")
set(C2000_SIZE "${C2000_CGT_ROOT}/bin/ofd2000")

# ------------------------------------------------------------------------------
# C++ Standard
# ------------------------------------------------------------------------------
# TI C2000 CGT only supports C++03. The C28x ISA is 16-bit word-addressable
# which makes modern C++ stdlib implementation impractical. TI has confirmed
# no plans for C++11+ support on C28x.
#
# Set CXX_STANDARD to 98 (C++03) and provide the --c++03 flag.
# CMAKE_CXX_STANDARD_REQUIRED is OFF because CMake's TI compiler module
# may not know the correct dialect flags.

set(CMAKE_CXX_STANDARD
    98
    CACHE STRING "C++03 for C2000 (TI CGT limitation)" FORCE
)
set(CMAKE_CXX_STANDARD_REQUIRED
    OFF
    CACHE BOOL "" FORCE
)
set(CMAKE_CXX_EXTENSIONS
    ON
    CACHE BOOL "" FORCE
)
set(CMAKE_CXX98_STANDARD_COMPILE_OPTION "--c++03")
set(CMAKE_CXX98_EXTENSION_COMPILE_OPTION "--c++03")

# ------------------------------------------------------------------------------
# CPU Flags (C28x floating-point unit, TMU, FPU32)
# ------------------------------------------------------------------------------

set(C2000_CPU_FLAGS "--silicon_version=28 --float_support=fpu32 --tmu_support=tmu0")

# ------------------------------------------------------------------------------
# Compiler Flags
# ------------------------------------------------------------------------------

# Common flags for all languages
set(C2000_COMMON_FLAGS
    "${C2000_CPU_FLAGS} --opt_level=2 --define=CPU1 --define=_LAUNCHXL_F280049C --diag_warning=225 --diag_suppress=10063 --diag_suppress=73"
)

# C flags
set(CMAKE_C_FLAGS_INIT "${C2000_COMMON_FLAGS}")

# C++ flags (--c++03 dialect; exceptions and RTTI are off by default in TI CGT)
set(CMAKE_CXX_FLAGS_INIT "${C2000_COMMON_FLAGS} --c++03")

# Assembly flags
set(CMAKE_ASM_FLAGS_INIT "${C2000_CPU_FLAGS}")

# ------------------------------------------------------------------------------
# Compile/Link Rules for TI Compiler
# ------------------------------------------------------------------------------
# CMake needs explicit rules for the TI compiler since it doesn't follow
# the GCC/Clang convention.

set(CMAKE_C_COMPILE_OBJECT
    "<CMAKE_C_COMPILER> <FLAGS> <DEFINES> <INCLUDES> -c <SOURCE> --output_file=<OBJECT>"
)
set(CMAKE_CXX_COMPILE_OBJECT
    "<CMAKE_CXX_COMPILER> <FLAGS> <DEFINES> <INCLUDES> -c <SOURCE> --output_file=<OBJECT>"
)
# ASM: TI assembler doesn't use -I for includes and <INCLUDES> expands
# incorrectly. Most .asm files are self-contained, so skip includes.
set(CMAKE_ASM_COMPILE_OBJECT
    "<CMAKE_ASM_COMPILER> <FLAGS> <DEFINES> <SOURCE> --output_file=<OBJECT>"
)

# Static library (ar2000)
set(CMAKE_C_CREATE_STATIC_LIBRARY "<CMAKE_AR> r <TARGET> <OBJECTS>")
set(CMAKE_CXX_CREATE_STATIC_LIBRARY "<CMAKE_AR> r <TARGET> <OBJECTS>")

# Executable link (cl2000 with -z for linker pass)
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_LINKER> <FLAGS> <OBJECTS> <LINK_FLAGS> <LINK_LIBRARIES> --output_file=<TARGET>"
)
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_LINKER> <FLAGS> <OBJECTS> <LINK_FLAGS> <LINK_LIBRARIES> --output_file=<TARGET>"
)

# ------------------------------------------------------------------------------
# Build Type Flags
# ------------------------------------------------------------------------------

set(CMAKE_C_FLAGS_DEBUG_INIT "--opt_level=0 --symdebug:dwarf")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "--opt_level=0 --symdebug:dwarf")
set(CMAKE_C_FLAGS_RELEASE_INIT "--opt_level=4 --define=NDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE_INIT "--opt_level=4 --define=NDEBUG")
set(CMAKE_C_FLAGS_RELWITHDEBINFO_INIT "--opt_level=2 --symdebug:dwarf --define=NDEBUG")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "--opt_level=2 --symdebug:dwarf --define=NDEBUG")

# ------------------------------------------------------------------------------
# Linker Flags
# ------------------------------------------------------------------------------

# TI linker uses -z to invoke the linker pass, --reread_libs for lib ordering,
# --stack_size and --heap_size for memory allocation.
# Linker command file (.cmd) is specified per-target via apex_add_firmware().
set(CMAKE_EXE_LINKER_FLAGS_INIT "-z --reread_libs --rom_model --stack_size=0x300 --heap_size=0x100")

# CGT runtime library
set(CMAKE_C_STANDARD_LIBRARIES "-l${C2000_CGT_ROOT}/lib/rts2800_fpu32.lib")
set(CMAKE_CXX_STANDARD_LIBRARIES "-l${C2000_CGT_ROOT}/lib/rts2800_fpu32.lib")

# ------------------------------------------------------------------------------
# Search Paths
# ------------------------------------------------------------------------------

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# CGT include and library search paths
set(CMAKE_C_IMPLICIT_INCLUDE_DIRECTORIES "${C2000_CGT_ROOT}/include")
set(CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES "${C2000_CGT_ROOT}/include")
include_directories(SYSTEM "${C2000_CGT_ROOT}/include")
link_directories("${C2000_CGT_ROOT}/lib")

# C2000Ware path (if available)
if (EXISTS "${C2000WARE_ROOT}")
  list(APPEND CMAKE_PREFIX_PATH "${C2000WARE_ROOT}")
endif ()
