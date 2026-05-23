#ifndef APEX_GATECONSTANTS_HPP
#define APEX_GATECONSTANTS_HPP
/**
 * @file GateConstants.hpp
 * @brief Shared constants for CMOS gate simulation.
 *
 * Default parameters for switch-level MOSFET modeling used by all gate
 * components. Individual gates accept these as template or constructor
 * parameters when non-default values are needed.
 *
 * @note NOT RT-safe: header-only constants, no allocation.
 */

namespace sim::electronics::devices::composite {

static constexpr double DEFAULT_VDD = 5.0;
static constexpr double DEFAULT_VTH = 1.0;
static constexpr double DEFAULT_RDS_ON = 50.0;
static constexpr double DEFAULT_RDS_OFF = 1e9;

} // namespace sim::electronics::devices::composite

#endif // APEX_GATECONSTANTS_HPP
