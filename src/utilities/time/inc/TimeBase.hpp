#ifndef APEX_UTILITIES_TIME_BASE_HPP
#define APEX_UTILITIES_TIME_BASE_HPP
/**
 * @file TimeBase.hpp
 * @brief Forwarder: the time-domain types live in the MCU-safe time subset.
 *
 * TimeStandard, Timestamp, and TimeProviderDelegate are freestanding-clean
 * and BAREMETAL-shipped from time/mcu so firmware names time the same way
 * hosted components do; this path remains for the existing hosted consumers.
 */

#include "src/utilities/time/mcu/inc/TimeBase.hpp"

#endif // APEX_UTILITIES_TIME_BASE_HPP
