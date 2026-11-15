/**
 * @file SystemComponentBase.cpp
 * @brief Implementation of SystemComponentBase queue processing and command handling.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentTlm.hpp"
#include "src/system/core/infrastructure/protocols/aproto/inc/AprotoCodec.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <fmt/core.h>

#include <cstring>

namespace system_core {
namespace system_component {

/* ----------------------------- SystemComponentBase Methods ----------------------------- */

std::uint8_t SystemComponentBase::handleCommand(std::uint16_t opcode,
                                                apex::compat::rospan<std::uint8_t> payload,
                                                std::vector<std::uint8_t>& response) noexcept {

  // Handle common component opcodes (0x0080-0x00FF).
  switch (opcode) {
  case GET_COMMAND_COUNT: {
    // Serialize ComponentCommandCountTlm (16 bytes).
    ComponentCommandCountTlm tlm{};
    tlm.commandCount = commandCount_;
    tlm.rejectedCount = rejectedCommandCount_;
    response.resize(sizeof(tlm));
    std::memcpy(response.data(), &tlm, sizeof(tlm));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case GET_STATUS_INFO: {
    // Serialize ComponentStatusInfoTlm (4 bytes).
    ComponentStatusInfoTlm tlm{};
    tlm.status = status();
    tlm.initialized = isInitialized() ? 1 : 0;
    tlm.configured = isConfigured() ? 1 : 0;
    tlm.registered = isRegistered() ? 1 : 0;
    response.resize(sizeof(tlm));
    std::memcpy(response.data(), &tlm, sizeof(tlm));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case RESET_COMMAND_COUNT:
    commandCount_ = 0;
    rejectedCommandCount_ = 0;
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);

  default:
    // Unknown opcode.
    (void)payload;
    return static_cast<std::uint8_t>(CommandResult::NOT_IMPLEMENTED);
  }
}

bool SystemComponentBase::initComponentLog(const std::filesystem::path& logDir) noexcept {
  // The log tree has one subdirectory per component type. An open on a
  // missing directory fails for good, so the directory is made here and
  // the caller hears whether the log opened.
  const std::filesystem::path SUB_DIR = logDir / logSubdir(componentType());
  std::error_code ec;
  std::filesystem::create_directories(SUB_DIR, ec);

  // Format: {componentName}_{instanceIndex}.log (e.g., Scheduler_0.log, PolynomialModel_1.log)
  std::string filename = componentName();
  filename += "_";
  filename += std::to_string(instanceIndex());
  filename += ".log";
  auto logPath = SUB_DIR / filename;
  auto log =
      std::make_shared<logs::SystemLog>(logPath.string(), logs::SystemLog::Mode::ASYNC, 4096);
  log->setLevel(logs::SystemLog::Level::DEBUG);
  const bool OPENED = log->lastOpenStatus() == logs::Status::OK;
  setComponentLog(std::move(log));
  return OPENED;
}

std::string SystemComponentBase::tprmFilename(std::uint32_t fullUid) noexcept {
  return fmt::format("{:06x}.tprm", fullUid);
}

} // namespace system_component
} // namespace system_core
