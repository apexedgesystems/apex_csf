#ifndef APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_NONLINEARDEVICES_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_NONLINEARDEVICES_HPP
/**
 * @file NonlinearDevices.hpp
 * @brief Registry header for all nonlinear device models.
 *
 * Import this single header to access all nonlinear device models:
 * - DiodeShockley: Exponential diode model
 * - DiodeSpice: SPICE diode with series R and junction cap
 * - ZenerDiode: Zener diode with breakdown (voltage regulation)
 * - JfetShichman: Junction FET (precision op-amps, analog switches)
 * - JfetLevel2: Advanced JFET with gate leakage and capacitance
 * - MosfetBinarySwitch: Digital switch model (fast simulation)
 * - MosfetLevel1: Shichman-Hodges 3-region analog model
 * - MosfetLevel2: SPICE Level 2 with geometry and velocity saturation
 * - MosfetLevel3: SPICE Level 3 with DIBL and short-channel effects
 * - BjtEbersMoll: Bipolar junction transistor (4-region Ebers-Moll)
 *
 * RT-safety: All models are RT-safe (static functions, no allocations).
 * Thread-safety: All models are thread-safe (stateless, pure functions).
 */

#include "src/sim/electronics/devices/nonlinear/inc/BjtEbersMoll.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeShockley.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/DiodeSpice.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/JfetLevel2.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/JfetShichman.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetBinarySwitch.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel1.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel2.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/MosfetLevel3.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/SchottkyDiode.hpp"
#include "src/sim/electronics/devices/nonlinear/inc/ZenerDiode.hpp"

#endif // APEX_SIM_ELECTRONICS_DEVICES_NONLINEAR_NONLINEARDEVICES_HPP
