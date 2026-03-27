/**
 * @file RfcommStatus.cpp
 * @brief Implementation of Status toString() function.
 */

#include "RfcommStatus.hpp"

namespace apex {
namespace protocols {
namespace wireless {
namespace bluetooth {

/* ----------------------------- API ----------------------------- */

const char* toString(Status status) noexcept {
  switch (status) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::WOULD_BLOCK:
    return "WOULD_BLOCK";
  case Status::ERROR_TIMEOUT:
    return "ERROR_TIMEOUT";
  case Status::ERROR_CLOSED:
    return "ERROR_CLOSED";
  case Status::ERROR_INVALID_ARG:
    return "ERROR_INVALID_ARG";
  case Status::ERROR_NOT_CONFIGURED:
    return "ERROR_NOT_CONFIGURED";
  case Status::ERROR_IO:
    return "ERROR_IO";
  case Status::ERROR_CONNECTION_REFUSED:
    return "ERROR_CONNECTION_REFUSED";
  case Status::ERROR_HOST_UNREACHABLE:
    return "ERROR_HOST_UNREACHABLE";
  case Status::ERROR_ALREADY_CONNECTED:
    return "ERROR_ALREADY_CONNECTED";
  case Status::ERROR_NOT_CONNECTED:
    return "ERROR_NOT_CONNECTED";
  }
  return "UNKNOWN";
}

} // namespace bluetooth
} // namespace wireless
} // namespace protocols
} // namespace apex
