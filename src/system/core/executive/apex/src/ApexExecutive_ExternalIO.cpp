/**
 * @file ApexExecutive_ExternalIO.cpp
 * @brief External I/O thread for stdin commands and network interface polling.
 *
 * Design:
 *  - Handles interactive stdin commands (pause, resume, quit)
 *  - Polls network interface for APROTO commands/telemetry
 *  - Uses poll() with timeout for responsive shutdown without busy-looping
 */

#include "src/system/core/executive/apex/inc/ApexExecutive.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include <fmt/core.h>

using enum executive::Status;

namespace executive {

// Fixed buffer size for command input (single command + newline + null)
static constexpr std::size_t CMD_BUFFER_SIZE = 128;

/* ----------------------------- Command Parsing ----------------------------- */

/**
 * @enum Command
 * @brief Interactive control commands from external input.
 */
enum class Command : std::uint8_t { NONE = 0, PAUSE, RESUME, FAST_FORWARD, QUIT, UNKNOWN };

/**
 * @brief Parse single-character command efficiently.
 * @param c Character to parse.
 * @return Parsed command type.
 */
inline Command parseCommandChar(char c) noexcept {
  // Convert to lowercase via bitwise OR with 0x20
  const char lc = c | 0x20;

  switch (lc) {
  case 'p':
    return Command::PAUSE;
  case 'r':
    return Command::RESUME;
  case 'f':
    return Command::FAST_FORWARD;
  case 'q':
    return Command::QUIT;
  default:
    return Command::UNKNOWN;
  }
}

/**
 * @brief Trim whitespace from buffer in-place (modifies buffer).
 * @param buf Buffer to trim.
 * @param len Current length.
 * @return New length after trimming.
 */
inline std::size_t trimWhitespace(char* buf, std::size_t len) noexcept {
  if (len == 0)
    return 0;

  // Trim trailing whitespace
  while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' || buf[len - 1] == '\r' ||
                     buf[len - 1] == '\n')) {
    --len;
  }
  buf[len] = '\0';

  // Trim leading whitespace
  std::size_t start = 0;
  while (start < len && (buf[start] == ' ' || buf[start] == '\t')) {
    ++start;
  }

  if (start > 0) {
    std::memmove(buf, buf + start, len - start + 1); // +1 for null terminator
    len -= start;
  }

  return len;
}

/* ----------------------------- External I/O Thread ----------------------------- */

void ApexExecutive::externalIO(std::promise<std::uint8_t>&& p) noexcept {
  sysLog_->debug(label(), "External I/O thread started", 1);

  // Fixed buffer for command input (stack allocation, reused)
  char cmdBuffer[CMD_BUFFER_SIZE];

  // poll() structure for stdin monitoring
  struct pollfd pfd;
  pfd.fd = STDIN_FILENO;
  pfd.events = POLLIN;

  // Log interface status
  const bool INTERFACE_ENABLED = interface_ && interface_->isInitialized();
  sysLog_->info(label(),
                fmt::format("External I/O: interface_={}, isInitialized={}, "
                            "isSocketsConfigured={}, INTERFACE_ENABLED={}",
                            interface_ != nullptr, interface_ ? interface_->isInitialized() : false,
                            interface_ ? interface_->isSocketsConfigured() : false,
                            INTERFACE_ENABLED));
  if (INTERFACE_ENABLED) {
    sysLog_->info(label(), "External I/O: Interface polling enabled (port 9000)");
  }

  // Main processing loop - poll stdin and interface
  // Use shorter stdin timeout when interface is enabled for responsive network I/O
  const int STDIN_TIMEOUT_MS = INTERFACE_ENABLED ? 10 : 100;

  while (!externalIOShouldStop_.load(std::memory_order_relaxed) &&
         !controlState_.shutdownRequested.load(std::memory_order_relaxed)) {

    // Poll stdin
    const int POLL_RESULT = ::poll(&pfd, 1, STDIN_TIMEOUT_MS);

    if (POLL_RESULT < 0) {
      if (errno == EINTR) {
        continue;
      }
      ioState_.pollErrors.fetch_add(1, std::memory_order_relaxed);
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_IO_ERROR),
                       fmt::format("poll() error: {} ({})", strerror(errno), errno));
      break;

    } else if (POLL_RESULT == 0) {
      // Timeout - no stdin input, fall through to interface poll
    } else if (pfd.revents & POLLIN) {
      // Input available - read it
      const ssize_t BYTES_READ = ::read(STDIN_FILENO, cmdBuffer, CMD_BUFFER_SIZE - 1);

      if (BYTES_READ < 0) {
        if (errno == EINTR) {
          continue;
        }
        ioState_.readErrors.fetch_add(1, std::memory_order_relaxed);
        sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_IO_ERROR),
                         fmt::format("read() error: {} ({})", strerror(errno), errno));
        break;

      } else if (BYTES_READ == 0) {
        // EOF - stdin closed
        sysLog_->debug(label(), "stdin EOF detected", 2);
        pfd.fd = -1;
        continue;

      } else {
        // Valid input received
        cmdBuffer[BYTES_READ] = '\0';
        std::size_t cmdLen = trimWhitespace(cmdBuffer, static_cast<std::size_t>(BYTES_READ));

        if (cmdLen == 0) {
          ioState_.emptyLines.fetch_add(1, std::memory_order_relaxed);
          continue;
        }

        sysLog_->debug(label(), fmt::format("Received command: '{}'", cmdBuffer), 3);

        const Command CMD = parseCommandChar(cmdBuffer[0]);

        switch (CMD) {
        case Command::PAUSE:
          if (!controlState_.isPaused.load(std::memory_order_acquire)) {
            pause();
            std::cout << "Pause signal sent to system..." << std::endl;
          } else {
            std::cout << "System is already paused" << std::endl;
          }
          ioState_.commandsProcessed.fetch_add(1, std::memory_order_relaxed);
          break;

        case Command::RESUME:
          if (controlState_.isPaused.load(std::memory_order_acquire)) {
            resume();
            std::cout << "Resume signal sent to system..." << std::endl;
          } else {
            std::cout << "System is not paused" << std::endl;
          }
          ioState_.commandsProcessed.fetch_add(1, std::memory_order_relaxed);
          break;

        case Command::FAST_FORWARD:
          fastForward();
          std::cout << "Fast-forward: Not yet implemented" << std::endl;
          ioState_.commandsProcessed.fetch_add(1, std::memory_order_relaxed);
          break;

        case Command::QUIT:
          sysLog_->info(label(), "Quit command received, initiating shutdown");
          std::cout << "Shutdown initiated by user command..." << std::endl;
          controlState_.shutdownRequested.store(true, std::memory_order_release);
          cvShutdown_.notify_all();
          ioState_.commandsProcessed.fetch_add(1, std::memory_order_relaxed);
          break;

        case Command::UNKNOWN:
          std::cout << "Unknown command: '" << cmdBuffer
                    << "' (P=Pause, R=Resume, F=Fast-forward, Q=Quit)" << std::endl;
          ioState_.unknownCommands.fetch_add(1, std::memory_order_relaxed);
          break;

        case Command::NONE:
          break;
        }
      }
    } else if (pfd.revents & POLLHUP) {
      sysLog_->debug(label(), "stdin closed (POLLHUP)", 2);
      pfd.fd = -1;
    } else if (pfd.revents & POLLERR) {
      sysLog_->warning(label(), static_cast<std::uint8_t>(WARN_IO_ERROR), "stdin error (POLLERR)");
      pfd.fd = -1;
    }

    // Poll interface for network commands
    // Use short timeout (10ms) - combined with 10ms stdin poll gives ~20ms response latency
    if (INTERFACE_ENABLED) {
      interface_->pollSockets(10);
    }
  }

  // Shutdown interface
  if (INTERFACE_ENABLED) {
    sysLog_->debug(label(), "Shutting down interface...", 1);
    interface_->logStatsSummary();
    (void)interface_->shutdown();
  }

  // Log statistics on exit
  const std::uint64_t TOTAL_CMDS = ioState_.commandsProcessed.load(std::memory_order_relaxed);
  const std::uint64_t UNKNOWN_CMDS = ioState_.unknownCommands.load(std::memory_order_relaxed);
  const std::uint64_t EMPTY_LINES = ioState_.emptyLines.load(std::memory_order_relaxed);
  const std::uint64_t POLL_ERRS = ioState_.pollErrors.load(std::memory_order_relaxed);
  const std::uint64_t READ_ERRS = ioState_.readErrors.load(std::memory_order_relaxed);

  sysLog_->info(label(),
                fmt::format("External I/O stopped: {} stdin commands, {} unknown, {} empty, "
                            "{} poll errors, {} read errors",
                            TOTAL_CMDS, UNKNOWN_CMDS, EMPTY_LINES, POLL_ERRS, READ_ERRS));

  p.set_value(static_cast<uint8_t>(Status::SUCCESS));
}

/* ----------------------------- Interactive Control Commands ----------------------------- */

void ApexExecutive::pause() noexcept {
  if (controlState_.isPaused.load(std::memory_order_acquire)) {
    sysLog_->debug(label(), "Pause requested but system already paused", 2);
    return;
  }

  controlState_.pauseRequested.store(true, std::memory_order_release);
  sysLog_->debug(label(), "Pause requested via external command", 1);
}

void ApexExecutive::resume() noexcept {
  if (!controlState_.isPaused.load(std::memory_order_acquire)) {
    sysLog_->debug(label(), "Resume requested but system not paused", 2);
    return;
  }

  controlState_.pauseRequested.store(false, std::memory_order_release);
  controlState_.isPaused.store(false, std::memory_order_release);
  cvPause_.notify_all();
  sysLog_->debug(label(), "Resume requested via external command", 1);
}

void ApexExecutive::fastForward() noexcept {
  // Planned feature: Run simulation without real-time throttling.
  // When implemented, this will disable the clock thread's sleep between ticks,
  // allowing the simulation to run as fast as possible.
  sysLog_->info(label(), "Fast-forward requested (not yet implemented)");
}

} // namespace executive
