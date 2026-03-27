/**
 * @file CcsdsEppProcessor.cpp
 * @brief Implementation of cold-path functions for CCSDS EPP Processor.
 *
 * Reference: CCSDS 133.1-B-3 "Encapsulation Packet Protocol" Blue Book
 */

#include "src/system/core/infrastructure/protocols/ccsds/epp/inc/CcsdsEppProcessor.hpp"

namespace protocols {
namespace ccsds {
namespace epp {

/* ----------------------------- Status toString ---------------------------- */

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

} // namespace epp
} // namespace ccsds
} // namespace protocols
