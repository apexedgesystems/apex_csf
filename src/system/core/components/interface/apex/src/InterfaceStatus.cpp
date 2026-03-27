/**
 * @file InterfaceStatus.cpp
 * @brief Implementation of InterfaceStatus string conversion.
 */

#include "src/system/core/components/interface/apex/inc/InterfaceStatus.hpp"

namespace system_core {
namespace interface {

/* ----------------------------- API ----------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  // Success
  case Status::SUCCESS:
    return "SUCCESS";

  // Lifecycle errors
  case Status::ERROR_NOT_INITIALIZED:
    return "ERROR_NOT_INITIALIZED";
  case Status::ERROR_ALREADY_INITIALIZED:
    return "ERROR_ALREADY_INITIALIZED";
  case Status::ERROR_CONFIG:
    return "ERROR_CONFIG";
  case Status::ERROR_CREATE_SERVER:
    return "ERROR_CREATE_SERVER";
  case Status::ERROR_BIND_OR_LISTEN:
    return "ERROR_BIND_OR_LISTEN";

  // Runtime errors
  case Status::ERROR_CHANNEL_CLOSED:
    return "ERROR_CHANNEL_CLOSED";
  case Status::ERROR_SEND_FAILED:
    return "ERROR_SEND_FAILED";
  case Status::ERROR_RECV_FAILED:
    return "ERROR_RECV_FAILED";
  case Status::ERROR_INVALID_PACKET:
    return "ERROR_INVALID_PACKET";
  case Status::ERROR_ROUTE_FAILED:
    return "ERROR_ROUTE_FAILED";
  case Status::ERROR_QUEUE_FULL:
    return "ERROR_QUEUE_FULL";
  case Status::ERROR_COMPONENT_NOT_FOUND:
    return "ERROR_COMPONENT_NOT_FOUND";

  // Warnings
  case Status::WARN_QUEUE_OVERFLOW:
    return "WARN_QUEUE_OVERFLOW";

  // Marker
  case Status::EOE_INTERFACE:
    return "EOE_INTERFACE";
  }
  return "UNKNOWN_STATUS";
}

} // namespace interface
} // namespace system_core
