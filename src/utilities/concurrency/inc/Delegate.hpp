#ifndef APEX_UTILITIES_CONCURRENCY_DELEGATE_HPP
#define APEX_UTILITIES_CONCURRENCY_DELEGATE_HPP
/**
 * @file Delegate.hpp
 * @brief Forwarder: Delegate lives in the MCU-safe concurrency subset.
 *
 * The delegate types are freestanding-clean and BAREMETAL-shipped from
 * concurrency/mcu so firmware links them without the POSIX concurrency lib;
 * this path remains for the existing hosted consumers.
 */

#include "src/utilities/concurrency/mcu/inc/Delegate.hpp"

#endif // APEX_UTILITIES_CONCURRENCY_DELEGATE_HPP
