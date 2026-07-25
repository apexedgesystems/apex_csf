#ifndef APEX_UTILITIES_TIME_CONVERT_HPP
#define APEX_UTILITIES_TIME_CONVERT_HPP
/**
 * @file TimeConvert.hpp
 * @brief Forwarder: the time conversions live in the MCU-safe time subset.
 *
 * The unit and standard conversions are pure functions, freestanding-clean
 * and BAREMETAL-shipped from time/mcu; this path remains for the existing
 * hosted consumers.
 */

#include "src/utilities/time/mcu/inc/TimeConvert.hpp"

#endif // APEX_UTILITIES_TIME_CONVERT_HPP
