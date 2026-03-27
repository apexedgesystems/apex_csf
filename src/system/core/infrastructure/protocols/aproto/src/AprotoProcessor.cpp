/**
 * @file AprotoProcessor.cpp
 * @brief Cold-path helpers for APROTO stream processor.
 */

#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoProcessor.hpp"

namespace system_core {
namespace protocols {
namespace aproto {

/* --------------------------------- API ---------------------------------- */

const char* toString(ProcessorStatus s) noexcept {
  switch (s) {
  case ProcessorStatus::OK:
    return "OK";
  case ProcessorStatus::NEED_MORE:
    return "NEED_MORE";
  case ProcessorStatus::WARNING_DESYNC_DROPPED:
    return "WARNING_DESYNC_DROPPED";
  case ProcessorStatus::ERROR_LENGTH_OVER_MAX:
    return "ERROR_LENGTH_OVER_MAX";
  case ProcessorStatus::ERROR_BUFFER_FULL:
    return "ERROR_BUFFER_FULL";
  default:
    return "UNKNOWN";
  }
}

} // namespace aproto
} // namespace protocols
} // namespace system_core
