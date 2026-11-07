/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built on
 *        every platform the lib.manifest declares — under each cross
 *        toolchain on MCU builds, at each declared posix_cpp dialect on
 *        hosted builds — so a regression against the support contract fails
 *        the build that owns the claim.
 */

#include "src/utilities/build_info/inc/BuildInfo.hpp"

const char* probe() noexcept {
  const char* fields[] = {apex::build_info::summary(),   apex::build_info::project(),
                          apex::build_info::version(),   apex::build_info::describe(),
                          apex::build_info::commit(),    apex::build_info::branch(),
                          apex::build_info::treeState(), apex::build_info::buildType(),
                          apex::build_info::compiler(),  apex::build_info::targetSystem()};
  return fields[apex::build_info::dirty() ? 0 : 1];
}
