/**
 * @file TimeServerData.cpp
 * @brief toString implementations for TimeServer enums.
 */

#include "src/system/core/components/time_server/apex/inc/TimeServerData.hpp"

namespace system_core {
namespace time_server {

const char* toString(TimeSource s) noexcept {
  switch (s) {
  case TimeSource::GPS:
    return "GPS";
  case TimeSource::GROUND:
    return "GROUND";
  case TimeSource::ONBOARD:
    return "ONBOARD";
  case TimeSource::MANUAL:
    return "MANUAL";
  case TimeSource::SIM:
    return "SIM";
  }
  return "UNKNOWN";
}

const char* toString(TimeQuality q) noexcept {
  switch (q) {
  case TimeQuality::UNKNOWN:
    return "UNKNOWN";
  case TimeQuality::COARSE:
    return "COARSE";
  case TimeQuality::FINE:
    return "FINE";
  case TimeQuality::PRECISE:
    return "PRECISE";
  }
  return "INVALID";
}

const char* toString(TimeValid v) noexcept {
  switch (v) {
  case TimeValid::NONE:
    return "NONE";
  case TimeValid::VALID:
    return "VALID";
  case TimeValid::STALE:
    return "STALE";
  case TimeValid::FREERUN:
    return "FREERUN";
  }
  return "UNKNOWN";
}

const char* toString(TimeServerMode m) noexcept {
  switch (m) {
  case TimeServerMode::PRIMARY:
    return "PRIMARY";
  case TimeServerMode::SECONDARY:
    return "SECONDARY";
  case TimeServerMode::PTP_SYNC:
    return "PTP_SYNC";
  case TimeServerMode::CAN_SYNC:
    return "CAN_SYNC";
  case TimeServerMode::RELAY:
    return "RELAY";
  }
  return "UNKNOWN";
}

} // namespace time_server
} // namespace system_core
