/**
 * @file AtmosphereModule.cpp
 * @brief Anchor TU for the sim_environment_atmosphere library.
 *
 * The analytic models (Constant, Exponential) are header-only; the
 * file-backed model lives in Atm.cpp / LayeredAtmosphere.cpp. This file
 * provides a single external symbol so the SHARED library always has a
 * defined anchor to link against.
 */

#include "src/sim/environment/atmosphere/inc/AtmosphereStatus.hpp"

namespace sim {
namespace environment {
namespace atmosphere {

// Anchor symbol so the shared library has external linkage.
extern const char* moduleName() noexcept;
const char* moduleName() noexcept { return "sim_environment_atmosphere"; }

} // namespace atmosphere
} // namespace environment
} // namespace sim
