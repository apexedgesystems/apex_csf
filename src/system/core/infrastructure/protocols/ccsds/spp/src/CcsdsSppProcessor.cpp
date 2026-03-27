#include "src/system/core/infrastructure/protocols/ccsds/spp/inc/CcsdsSppProcessor.hpp"

namespace protocols {
namespace ccsds {
namespace spp {

/* ---------------------------- Cold-path helper ---------------------------- */

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::OK:
    return "OK";
  case Status::NEED_MORE:
    return "NEED_MORE";
  case Status::WARNING_DESYNC_DROPPED:
    return "WARNING_DESYNC_DROPPED";
  case Status::ERROR_LENGTH_OVER_MAX:
    return "ERROR_LENGTH_OVER_MAX";
  case Status::ERROR_BUFFER_FULL:
    return "ERROR_BUFFER_FULL";
  default:
    return "UNKNOWN";
  }
}

} // namespace spp
} // namespace ccsds
} // namespace protocols
