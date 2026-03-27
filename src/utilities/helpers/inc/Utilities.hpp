#ifndef APEX_UTILITIES_HELPERS_UTILITIES_HPP
#define APEX_UTILITIES_HELPERS_UTILITIES_HPP
/**
 * @file Utilities.hpp
 * @brief Convenience header for all helpers utilities.
 *
 * Includes all domain-specific helper headers plus miscellaneous utilities.
 * Individual headers can be included directly for finer-grained control.
 *
 * Domain Headers:
 *  - Args.hpp:    CLI argument parsing
 *  - Bytes.hpp:   Byte/endian manipulation
 *  - Files.hpp:   File I/O and path utilities
 *  - Format.hpp:  Output formatting
 *  - Strings.hpp: String manipulation
 */

#include "src/utilities/helpers/inc/Args.hpp"
#include "src/utilities/helpers/inc/Bytes.hpp"
#include "src/utilities/helpers/inc/Files.hpp"
#include "src/utilities/helpers/inc/Format.hpp"
#include "src/utilities/helpers/inc/Strings.hpp"

#include <cstdint>

namespace apex {
namespace helpers {

/* ----------------------------- Math Helpers ----------------------------- */

/**
 * @brief Update an online running average with the next sample.
 *
 * Computes: avg += (cur - avg) / (cyc + 1)
 *
 * @param avg  In/out accumulator (updated in place).
 * @param cur  Current sample value.
 * @param cyc  Completed sample count (0 for the first sample).
 * @note RT-SAFE: Simple arithmetic, no allocation.
 */
inline void runningAverage(double& avg, double cur, std::uint64_t cyc) noexcept {
  avg += (cur - avg) / static_cast<double>(cyc + 1U);
}

} // namespace helpers
} // namespace apex

#endif // APEX_UTILITIES_HELPERS_UTILITIES_HPP
