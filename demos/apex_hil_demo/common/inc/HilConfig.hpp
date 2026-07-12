#ifndef APEX_HIL_DEMO_CONFIG_HPP
#define APEX_HIL_DEMO_CONFIG_HPP
/**
 * @file HilConfig.hpp
 * @brief Compile-time configuration for the HIL flight demonstration.
 *
 * Shared between STM32 firmware and POSIX host. All timing, sizing,
 * and protocol constants live here.
 *
 * @note RT-safe: Compile-time constants only.
 */

#include <stddef.h>
#include <stdint.h>

namespace appsim {
namespace hil {

/* ----------------------------- Timing ----------------------------- */

/// Fundamental executive frequency [Hz].
static constexpr uint16_t EXEC_FREQ_HZ = 100;

/// Control loop frequency divisor (freqD). 2 = 50 Hz control.
static constexpr uint16_t CONTROL_FREQ_D = 2;

/// Telemetry report frequency divisor. 100 = 1 Hz reporting.
static constexpr uint16_t REPORT_FREQ_D = 100;

/// LED blink frequency divisor. 50 = 2 Hz heartbeat.
static constexpr uint16_t LED_FREQ_D = 50;

/// Plant simulation timestep [seconds].
static constexpr double PLANT_DT = 0.01;

/* ----------------------------- UART ----------------------------- */

/// UART baud rate (must match on both sides).
static constexpr uint32_t BAUD_RATE = 115200;

/* ----------------------------- Sizing ----------------------------- */

/// Maximum SLIP-decoded frame payload [bytes].
static constexpr size_t MAX_FRAME_PAYLOAD = 128;

/// Maximum SLIP-encoded frame (payload * 2 + 2 delimiters).
static constexpr size_t MAX_SLIP_ENCODED = MAX_FRAME_PAYLOAD * 2 + 2;

} // namespace hil
} // namespace appsim

#endif // APEX_HIL_DEMO_CONFIG_HPP
