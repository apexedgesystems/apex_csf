/**
 * @file RegistryStatus.cpp
 * @brief String representations for registry status codes.
 */

#include "src/system/core/components/registry/apex/inc/RegistryStatus.hpp"

namespace system_core {
namespace registry {

/* ----------------------------- API ----------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::ERROR_ALREADY_FROZEN:
    return "ERROR_ALREADY_FROZEN";
  case Status::ERROR_NOT_FROZEN:
    return "ERROR_NOT_FROZEN";
  case Status::ERROR_NULL_POINTER:
    return "ERROR_NULL_POINTER";
  case Status::ERROR_DUPLICATE_COMPONENT:
    return "ERROR_DUPLICATE_COMPONENT";
  case Status::ERROR_DUPLICATE_TASK:
    return "ERROR_DUPLICATE_TASK";
  case Status::ERROR_DUPLICATE_DATA:
    return "ERROR_DUPLICATE_DATA";
  case Status::ERROR_COMPONENT_NOT_FOUND:
    return "ERROR_COMPONENT_NOT_FOUND";
  case Status::ERROR_CAPACITY_EXCEEDED:
    return "ERROR_CAPACITY_EXCEEDED";
  case Status::ERROR_INVALID_CATEGORY:
    return "ERROR_INVALID_CATEGORY";
  case Status::ERROR_ZERO_SIZE:
    return "ERROR_ZERO_SIZE";
  case Status::ERROR_NOT_FOUND:
    return "ERROR_NOT_FOUND";
  case Status::ERROR_IO:
    return "ERROR_IO";
  case Status::WARN_EMPTY_NAME:
    return "WARN_EMPTY_NAME";
  case Status::EOE_REGISTRY:
    return "EOE_REGISTRY";
  }
  return "UNKNOWN_STATUS";
}

} // namespace registry
} // namespace system_core
