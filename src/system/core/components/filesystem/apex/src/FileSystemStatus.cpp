/**
 * @file FileSystemStatus.cpp
 * @brief Status code string conversions.
 */

#include "src/system/core/components/filesystem/apex/inc/FileSystemStatus.hpp"

namespace system_core {
namespace filesystem {

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::ERROR_FS_CREATION_FAIL:
    return "ERROR_FS_CREATION_FAIL";
  case Status::ERROR_FS_TAR_CREATE_FAIL:
    return "ERROR_FS_TAR_CREATE_FAIL";
  case Status::ERROR_FS_TAR_MOVE_FAIL:
    return "ERROR_FS_TAR_MOVE_FAIL";
  case Status::ERROR_INVALID_FS:
    return "ERROR_INVALID_FS";
  case Status::EOE_FILESYSTEM:
    return "EOE_FILESYSTEM";
  }
  return "UNKNOWN_STATUS";
}

} // namespace filesystem
} // namespace system_core
