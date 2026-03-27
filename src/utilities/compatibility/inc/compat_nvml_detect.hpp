#ifndef APEX_UTILITIES_COMPATIBILITY_NVML_DETECT_HPP
#define APEX_UTILITIES_COMPATIBILITY_NVML_DETECT_HPP
/**
 * @file compat_nvml_detect.hpp
 * @brief Lightweight NVML availability/version detection + missing-flag shims.
 *
 * Macros:
 *  - COMPAT_NVML_AVAILABLE         : 1 if NVML is available for this build, else 0
 *  - COMPAT_NVML_HEADER_AVAILABLE  : 1 if <nvml.h> is includable, else 0
 *  - COMPAT_NVML_API_VERSION       : NVML_API_VERSION if provided by the header, else 0
 *
 * Notes:
 *  - The build system may force availability (e.g., via apex_nvml_enable):
 *      -DCOMPAT_NVML_AVAILABLE=1   or   -DCOMPAT_NVML_AVAILABLE=0
 *  - This header does not link or initialize NVML; it only provides compile-time guards
 *    and safe fallbacks for some enums/flags that vary by NVML version.
 */

/* ---------------------- Availability Detection ---------------------------- */
#ifndef COMPAT_NVML_AVAILABLE
#if defined(__has_include)
#if __has_include(<nvml.h>)
#define COMPAT_NVML_HEADER_AVAILABLE 1
#else
#define COMPAT_NVML_HEADER_AVAILABLE 0
#endif
#else
// Conservative default when __has_include is not supported.
#define COMPAT_NVML_HEADER_AVAILABLE 0
#endif

#if COMPAT_NVML_HEADER_AVAILABLE
#define COMPAT_NVML_AVAILABLE 1
#else
#define COMPAT_NVML_AVAILABLE 0
#endif
#else
// Mirror availability into HEADER_AVAILABLE if caller forced it.
#ifndef COMPAT_NVML_HEADER_AVAILABLE
#define COMPAT_NVML_HEADER_AVAILABLE COMPAT_NVML_AVAILABLE
#endif
#endif

/* ---------------------- Header Import + Version -------------------------- */
#if COMPAT_NVML_AVAILABLE
#include <nvml.h>
#ifdef NVML_API_VERSION
#define COMPAT_NVML_API_VERSION NVML_API_VERSION
#else
#define COMPAT_NVML_API_VERSION 0
#endif
#else
#define COMPAT_NVML_API_VERSION 0
#endif

/* --------------------- Compatibility Shims (Macros) ----------------------- */
#if COMPAT_NVML_AVAILABLE

// Some distros ship older headers that miss newer constants.
// Provide benign defaults so code can compile and bit-test safely.

// Device name buffer (96 is NVML's typical size).
#ifndef NVML_DEVICE_NAME_BUFFER_SIZE
#define NVML_DEVICE_NAME_BUFFER_SIZE 96
#endif

// Clocks throttle reasons (bit flags). Default to 0ULL when missing.
#ifndef NVML_CLOCKS_THROTTLE_REASON_GPU_IDLE
#define NVML_CLOCKS_THROTTLE_REASON_GPU_IDLE 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_APPLICATIONS_CLOCKS_SETTING
#define NVML_CLOCKS_THROTTLE_REASON_APPLICATIONS_CLOCKS_SETTING 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP
#define NVML_CLOCKS_THROTTLE_REASON_SW_POWER_CAP 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_HW_SLOWDOWN
#define NVML_CLOCKS_THROTTLE_REASON_HW_SLOWDOWN 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_THERMAL
#define NVML_CLOCKS_THROTTLE_REASON_THERMAL 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_SYNC_BOOST
#define NVML_CLOCKS_THROTTLE_REASON_SYNC_BOOST 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_SW_THERMAL_SLOWDOWN
#define NVML_CLOCKS_THROTTLE_REASON_SW_THERMAL_SLOWDOWN 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN
#define NVML_CLOCKS_THROTTLE_REASON_HW_THERMAL_SLOWDOWN 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN
#define NVML_CLOCKS_THROTTLE_REASON_HW_POWER_BRAKE_SLOWDOWN 0ULL
#endif
#ifndef NVML_CLOCKS_THROTTLE_REASON_DISPLAY_CLOCK_SETTING
#define NVML_CLOCKS_THROTTLE_REASON_DISPLAY_CLOCK_SETTING 0ULL
#endif

// (Intentionally no shim for NVML_TEMPERATURE_GPU: it exists on all supported headers;
//  and if the header is missing, COMPAT_NVML_AVAILABLE==0 so the call sites are skipped.)

#endif // COMPAT_NVML_AVAILABLE

#endif // APEX_UTILITIES_COMPATIBILITY_NVML_DETECT_HPP
