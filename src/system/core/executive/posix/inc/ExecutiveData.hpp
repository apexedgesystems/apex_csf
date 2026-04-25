#ifndef APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_DATA_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_DATA_HPP
/**
 * @file ExecutiveData.hpp
 * @brief Data structures for ApexExecutive tunable parameters and thread configuration.
 *
 * Contains:
 *  - ExecutiveTunableParams: Binary TPRM structure (48 bytes)
 *  - Thread configuration: TPRM structures and runtime structures
 *
 * CLI arguments can override individual values after loading.
 */

#include <cstdint>

#include <sched.h>
#include <vector>

namespace executive {

/* ----------------------------- Tunable Parameters ----------------------------- */

/**
 * @struct ExecutiveTunableParams
 * @brief Runtime-adjustable executive configuration.
 *
 * This struct is loaded from a binary .tprm file via hex2cpp.
 * All fields must be trivially copyable with no padding issues.
 *
 * Field order matches TOML template generation order - DO NOT REORDER.
 */
struct ExecutiveTunableParams {
  // Clock configuration
  std::uint16_t clockFrequencyHz{100}; ///< Fundamental clock frequency (Hz).
  std::uint16_t reserved0{0};          ///< Padding for alignment.

  // Real-time mode configuration
  std::uint8_t rtMode{0};     ///< RT mode (0=HARD_TICK_COMPLETE, 1=HARD_PERIOD_COMPLETE,
                              ///<          2=SOFT_SKIP_ON_BUSY, 3=SOFT_LAG_TOLERANT, 4=LOG_ONLY).
  std::uint8_t reserved1{0};  ///< Padding.
  std::uint16_t reserved2{0}; ///< Padding.
  std::uint32_t rtMaxLagTicks{10}; ///< Max allowed lag ticks (LAG_TOLERANT mode).

  // Startup configuration
  std::uint8_t startupMode{0};          ///< Startup mode (0=AUTO, 1=INTERACTIVE, 2=SCHEDULED).
  std::uint8_t reserved3{0};            ///< Padding.
  std::uint16_t reserved4{0};           ///< Padding.
  std::uint32_t startupDelaySeconds{1}; ///< Delay before startup (AUTO mode).

  // Shutdown configuration
  std::uint8_t shutdownMode{0}; ///< Shutdown mode (0=SIGNAL_ONLY, 1=SCHEDULED,
                                ///<               2=RELATIVE_TIME, 3=CLOCK_CYCLE, 4=COMBINED).
  std::uint8_t skipCleanup{0};  ///< Skip archive/cleanup at shutdown (0=false, 1=true).
  std::uint16_t reserved5{0};   ///< Padding.
  std::uint32_t shutdownAfterSeconds{0}; ///< Shutdown after N seconds (RELATIVE_TIME mode).
  std::uint32_t reserved6{0};            ///< Explicit padding for uint64_t alignment.
  std::uint64_t shutdownAtCycle{0};      ///< Shutdown at cycle N (CLOCK_CYCLE mode).

  // Watchdog configuration
  std::uint32_t watchdogIntervalMs{1000}; ///< Watchdog check interval (ms, min 100).

  // Profiling configuration
  std::uint32_t profilingSampleEveryN{0}; ///< Profile every N ticks (0=disabled).
};

// Ensure struct is trivially copyable for hex2cpp
static_assert(std::is_trivially_copyable_v<ExecutiveTunableParams>,
              "ExecutiveTunableParams must be trivially copyable for binary serialization");

// Document expected size for binary compatibility
static_assert(sizeof(ExecutiveTunableParams) == 48,
              "ExecutiveTunableParams size changed - update TOML template and regenerate binaries");

/* ----------------------------- Runtime Thread Configuration ----------------------------- */

/**
 * @struct PrimaryThreadConfig
 * @brief Runtime configuration for an individual primary thread.
 *
 * Defines RT scheduling policy, priority, CPU affinity, and stack size.
 * Populated from TPRM via threadConfigFromTprm().
 */
struct PrimaryThreadConfig {
  std::int8_t policy{SCHED_OTHER};      ///< Scheduling policy (SCHED_OTHER, SCHED_FIFO, SCHED_RR)
  std::int8_t priority{0};              ///< RT priority (1-99 for FIFO/RR, 0 for OTHER)
  std::vector<std::uint8_t> affinity{}; ///< CPU affinity mask (empty = inherit)
  std::size_t stackSize{0};             ///< Stack size (0 = default)
};

/**
 * @struct ExecutiveThreadConfig
 * @brief Runtime configuration for all primary executive threads.
 *
 * Populated from ExecutiveThreadConfigTprm via threadConfigFromTprm().
 */
struct ExecutiveThreadConfig {
  PrimaryThreadConfig startup{};       ///< Startup thread config
  PrimaryThreadConfig shutdown{};      ///< Shutdown thread config
  PrimaryThreadConfig clock{};         ///< Clock thread config
  PrimaryThreadConfig taskExecution{}; ///< Task execution thread config
  PrimaryThreadConfig externalIO{};    ///< External I/O thread config
  PrimaryThreadConfig watchdog{};      ///< Watchdog thread config
};

/* ----------------------------- TPRM Thread Configuration ----------------------------- */

/// Maximum CPU cores in affinity set per thread.
constexpr std::uint8_t MAX_AFFINITY_CORES = 8;

/// Marker value for unused affinity slots.
constexpr std::uint8_t AFFINITY_UNUSED = 0xFF;

#pragma pack(push, 1)

/**
 * @struct ThreadConfigEntry
 * @brief TPRM entry for a single thread's RT configuration.
 *
 * Fixed-size structure for binary serialization.
 * Unused affinity slots are marked with AFFINITY_UNUSED (0xFF).
 */
struct ThreadConfigEntry {
  std::int8_t policy;                        ///< POSIX policy (0=OTHER, 1=FIFO, 2=RR).
  std::int8_t priority;                      ///< POSIX priority (0 for OTHER, 1-99 for RT).
  std::uint8_t affinityCount;                ///< Number of valid entries in affinity[].
  std::uint8_t affinity[MAX_AFFINITY_CORES]; ///< CPU cores (unused slots = 0xFF).
};

static_assert(sizeof(ThreadConfigEntry) == 11, "ThreadConfigEntry must be exactly 11 bytes");

/**
 * @struct ExecutiveThreadConfigTprm
 * @brief TPRM block for all executive thread configurations.
 *
 * Contains RT settings for all 6 primary executive threads.
 * Total size: 66 bytes (11 bytes x 6 threads).
 */
struct ExecutiveThreadConfigTprm {
  ThreadConfigEntry startup;       ///< STARTUP thread config.
  ThreadConfigEntry shutdown;      ///< SHUTDOWN thread config.
  ThreadConfigEntry clock;         ///< CLOCK thread config (RT critical).
  ThreadConfigEntry taskExecution; ///< TASK_EXECUTION thread config (RT critical).
  ThreadConfigEntry externalIO;    ///< EXTERNAL_IO thread config.
  ThreadConfigEntry watchdog;      ///< WATCHDOG thread config.
};

static_assert(sizeof(ExecutiveThreadConfigTprm) == 66,
              "ExecutiveThreadConfigTprm must be exactly 66 bytes");

#pragma pack(pop)

/**
 * @brief Convert ThreadConfigEntry to PrimaryThreadConfig.
 * @param entry TPRM entry.
 * @param out Output config.
 * @note NOT RT-safe: May allocate for affinity vector.
 */
inline void threadConfigFromTprm(const ThreadConfigEntry& entry,
                                 PrimaryThreadConfig& out) noexcept {
  out.policy = entry.policy;
  out.priority = entry.priority;
  out.affinity.clear();
  for (std::uint8_t i = 0; i < entry.affinityCount && i < MAX_AFFINITY_CORES; ++i) {
    if (entry.affinity[i] != AFFINITY_UNUSED) {
      out.affinity.push_back(entry.affinity[i]);
    }
  }
  out.stackSize = 0; // Not configurable via TPRM (use system default)
}

} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_EXECUTIVE_DATA_HPP
