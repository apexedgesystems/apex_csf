#ifndef APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_COMPOSITEDEVICES_HPP
#define APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_COMPOSITEDEVICES_HPP
/**
 * @file CompositeDevices.hpp
 * @brief Registry header for all composite device models.
 *
 * Import this single header to access all composite devices (CMOS gates, etc.).
 * Composite devices are built from primitive linear and nonlinear models.
 *
 * RT-safety: All models are RT-safe (static functions, no allocations).
 * Thread-safety: All models are thread-safe (stateless, pure functions).
 */

#include "src/sim/electronics/devices/composite/inc/CmosInverter.hpp"
#include "src/sim/electronics/devices/composite/inc/CmosNand.hpp"
#include "src/sim/electronics/devices/composite/inc/CmosNor.hpp"

#endif // APEX_SIM_ELECTRONICS_DEVICES_COMPOSITE_COMPOSITEDEVICES_HPP
