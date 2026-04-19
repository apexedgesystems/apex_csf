#ifndef APEX_SIM_ELECTRONICS_MNA_TYPES_HPP
#define APEX_SIM_ELECTRONICS_MNA_TYPES_HPP
/**
 * @file Types.hpp
 * @brief Core type aliases for circuit simulation (InstanceID, NetID).
 */

#include <cstdint>

namespace sim::electronics::mna {

/**
 * @brief Unique identifier for a placed component instance.
 *
 * 0 means empty cell.
 */
using InstanceID = std::uint32_t;

/**
 * @brief Unique identifier for an electrical net.
 *
 * 0 means unassigned net.
 */
using NetID = std::uint32_t;

} // namespace sim::electronics::mna

#endif // APEX_SIM_ELECTRONICS_MNA_TYPES_HPP
