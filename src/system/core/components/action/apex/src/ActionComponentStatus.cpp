/**
 * @file ActionComponentStatus.cpp
 * @brief String representations for ActionComponent status codes.
 */

#include "src/system/core/components/action/apex/inc/ActionComponentStatus.hpp"

namespace system_core {
namespace action {

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::ERROR_NO_RESOLVER:
    return "ERROR_NO_RESOLVER";
  case Status::ERROR_QUEUE_FULL:
    return "ERROR_QUEUE_FULL";
  case Status::WARN_RESOLVE_FAILURES:
    return "WARN_RESOLVE_FAILURES";
  case Status::EOE_ACTION:
    return "EOE_ACTION";
  }
  return "UNKNOWN_STATUS";
}

} // namespace action
} // namespace system_core
