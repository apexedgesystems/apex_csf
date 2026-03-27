#ifndef APEX_SYSTEM_CORE_EXECUTIVE_STARTUP_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_STARTUP_HPP
/**
 * @file ApexExecutive_Startup.hpp
 * @brief Startup configuration for executive system.
 */

#include <cstdint>

namespace executive {

struct StartupConfig {
  enum Mode : std::uint8_t { AUTO = 0, INTERACTIVE, SCHEDULED };

  Mode mode{AUTO};
  std::uint32_t delaySeconds{1};
  std::int64_t startAtEpochNs{0};
};

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_STARTUP_HPP