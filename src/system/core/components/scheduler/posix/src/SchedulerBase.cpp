/**
 * @file SchedulerBase.cpp
 * @brief Implementation of SchedulerBase task scheduling and geometry calculations.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerBase.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerData.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerExport.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerStatus.hpp"
#include "src/system/core/components/scheduler/posix/inc/SchedulerTlm.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <cstdint>
#include <cstring>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fmt/core.h>

namespace system_core {
namespace scheduler {

/* ----------------------------- addTask (TaskConfig) ----------------------------- */

Status SchedulerBase::addTask(SchedulableTask& task, const TaskConfig& config) noexcept {
  const std::uint16_t TFREQN = config.freqN;
  const std::uint16_t TFREQD = config.freqD;
  const std::uint16_t OFFSET = config.offset;

  // Preconditions
  if (TFREQN > ffreq_) {
    setLastError("tfreqn > ffreq");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_TFREQN_GT_FFREQ));
    return Status::ERROR_TFREQN_GT_FFREQ;
  }

  if (TFREQN == 0) {
    setLastError("tfreqn == 0 (division undefined)");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FFREQ_MOD_TFREQN_DNE0));
    return Status::ERROR_FFREQ_MOD_TFREQN_DNE0;
  }

  if ((ffreq_ % TFREQN) != 0) {
    setLastError("ffreq % tfreqn != 0");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FFREQ_MOD_TFREQN_DNE0));
    return Status::ERROR_FFREQ_MOD_TFREQN_DNE0;
  }

  if (TFREQD < 1) {
    setLastError("tfreqd < 1");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_TFREQD_LT1));
    return Status::ERROR_TFREQD_LT1;
  }

  // First-touch sizing
  if (schedule_.empty() && ffreq_ > 0U) {
    schedule_.resize(static_cast<std::size_t>(ffreq_));
  }

  // Geometry
  const std::uint16_t BASE_TPT =
      (TFREQN == ffreq_) ? 1 : static_cast<std::uint16_t>(ffreq_ / TFREQN);
  const std::uint32_t PERIOD_TPT =
      (TFREQD == 1) ? static_cast<std::uint32_t>(BASE_TPT)
                    : static_cast<std::uint32_t>(BASE_TPT) * static_cast<std::uint32_t>(TFREQD);

  if (PERIOD_TPT == 0U) {
    setLastError("Computed periodTicks == 0");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_FFREQ_MOD_TFREQN_DNE0));
    return Status::ERROR_FFREQ_MOD_TFREQN_DNE0;
  }

  if (OFFSET >= PERIOD_TPT) {
    setLastError("offset >= periodTicks");
    setStatus(static_cast<std::uint8_t>(Status::ERROR_OFFSET_GTE_TPT));
    return Status::ERROR_OFFSET_GTE_TPT;
  }

  // Create TaskEntry (scheduler owns this)
  TaskEntry entry{};
  entry.task = &task;
  entry.periodNs =
      ffreq_ > 0U ? (static_cast<std::uint64_t>(PERIOD_TPT) * 1'000'000'000ULL) / ffreq_ : 0ULL;
  entry.config = config;
  entry.holdCtr = 0;

  // Add entry to registry (move-only due to unique_ptr member)
  entries_.push_back(std::move(entry));
  const std::size_t ENTRY_IDX = entries_.size() - 1;

  // Priority comparator for sorted insertion
  const auto CMP_IDX = [this](std::size_t a, std::size_t b) noexcept {
    return entries_[a].config.priority > entries_[b].config.priority;
  };

  // Capacity hinting
  auto ensureCapacity = [](std::vector<std::size_t>& vec) {
    if (vec.empty()) {
      vec.reserve(8);
    } else if (vec.size() == vec.capacity()) {
      const std::size_t SZ = vec.size();
      vec.reserve(SZ + (SZ >> 1) + 8);
    }
  };

  // Place entry on all of its ticks
  for (std::uint32_t tick = OFFSET; tick < ffreq_; tick += PERIOD_TPT) {
    const std::size_t IDX = static_cast<std::size_t>(static_cast<std::uint16_t>(tick));
    auto& vec = schedule_[IDX];

    ensureCapacity(vec);

    if (vec.empty()) {
      vec.push_back(ENTRY_IDX);
      continue;
    }

    // Fast path: append if new >= last priority
    if (!CMP_IDX(ENTRY_IDX, vec.back())) {
      vec.push_back(ENTRY_IDX);
      continue;
    }

    const auto IT = std::lower_bound(vec.begin(), vec.end(), ENTRY_IDX, CMP_IDX);
    vec.insert(IT, ENTRY_IDX);
  }

  setLastError(nullptr);
  setStatus(static_cast<std::uint8_t>(Status::SUCCESS));
  return Status::SUCCESS;
}

/* ----------------------------- addTask (Parameters) ----------------------------- */

Status SchedulerBase::addTask(SchedulableTask& task, std::uint16_t tfreqn, std::uint16_t tfreqd,
                              std::uint16_t offset, TaskPriority priority,
                              std::uint8_t poolId) noexcept {
  TaskConfig config(tfreqn, tfreqd, offset, priority, poolId);
  return addTask(task, config);
}

/* ----------------------------- addTask (with SequenceGroup) ----------------------------- */

Status SchedulerBase::addTask(SchedulableTask& task, const TaskConfig& config, SequenceGroup* seq,
                              std::uint32_t fullUid, std::uint8_t taskUid,
                              const char* componentName) noexcept {
  // Add task with base method first
  const Status RC = addTask(task, config);
  if (RC != Status::SUCCESS) {
    return RC;
  }

  // Store fullUid, taskUid, componentName, and sequencing info in the entry
  // the base call just appended. getEntry() would return the FIRST entry for
  // this task pointer, which is a different entry when one task is scheduled
  // at multiple rates -- the second row's identity would land on the first
  // entry and leave its own at fullUid 0.
  TaskEntry* entry = entries_.empty() ? nullptr : &entries_.back();
  if (entry != nullptr && entry->task == &task) {
    entry->fullUid = fullUid;
    entry->componentName = componentName;
    entry->taskUid = taskUid;

    // If sequencing is provided, populate sequencing info
    if (seq != nullptr) {
      const SeqInfo* info = seq->getSeqInfo(&task);
      if (info != nullptr) {
        entry->seqCounter = seq->counter();
        entry->seqPhase = info->phase;
        entry->seqMaxPhase = info->maxPhase;
      }
    }
  }

  return Status::SUCCESS;
}

/* ----------------------------- getEntry ----------------------------- */

TaskEntry* SchedulerBase::getEntry(SchedulableTask& task) noexcept {
  for (auto& entry : entries_) {
    if (entry.task == &task) {
      return &entry;
    }
  }
  return nullptr;
}

const TaskEntry* SchedulerBase::getEntry(const SchedulableTask& task) const noexcept {
  for (const auto& entry : entries_) {
    if (entry.task == &task) {
      return &entry;
    }
  }
  return nullptr;
}

/* ----------------------------- replaceComponentTasks ----------------------------- */

std::uint8_t
SchedulerBase::replaceComponentTasks(std::uint32_t fullUid,
                                     system_component::SystemComponentBase& newComponent) noexcept {
  std::uint8_t replaced = 0;

  for (auto& entry : entries_) {
    if (entry.fullUid != fullUid) {
      continue;
    }

    auto* newTask = newComponent.taskByUid(entry.taskUid);
    if (newTask == nullptr) {
      // New component doesn't have this task -- leave it as-is (will be skipped if locked).
      continue;
    }

    entry.task = newTask;
    ++replaced;
  }

  return replaced;
}

/* ----------------------------- populateRequirementsTlm ----------------------------- */

void SchedulerBase::populateRequirementsTlm() noexcept {
  requirementsTlm_ = SchedulerRequirementsTlm{};
  requirementsTlm_.fundamentalFreqHz = ffreq_;
  requirementsTlm_.taskCount = static_cast<std::uint16_t>(entries_.size());

  std::size_t groups = 0;
  {
    std::vector<const std::atomic<int>*> seen;
    for (const auto& e : entries_) {
      if (e.isSequenced() && std::find(seen.begin(), seen.end(), e.seqCounter) == seen.end()) {
        seen.push_back(e.seqCounter);
      }
    }
    groups = seen.size();
  }
  requirementsTlm_.seqGroupCount = static_cast<std::uint8_t>(groups);

  const auto ROWS = poolRequirements();
  std::size_t n = 0;
  for (const auto& row : ROWS) {
    if (n >= REQUIREMENTS_POOL_CAP) {
      break;
    }
    requirementsTlm_.pools[n] = row;
    ++n;
  }
  requirementsTlm_.poolCount = static_cast<std::uint8_t>(n);

  // Mirror into the generic infrastructure record: assessors discover
  // this by conventional name + shape via the registry, with no
  // knowledge of the scheduler.
  hostRequirements_ = system_component::HostRequirements{};
  hostRequirements_.rateHz = ffreq_;
  hostRequirements_.taskCount = static_cast<std::uint16_t>(entries_.size());
  std::size_t hr = 0;
  for (const auto& row : ROWS) {
    if (hr >= system_component::HOST_REQUIREMENTS_ROW_CAP) {
      break;
    }
    auto& g = hostRequirements_.rows[hr];
    g.groupId = row.poolId;
    g.policy = row.policy;
    g.priority = row.priority;
    g.threads = row.workers;
    g.affinityMask = row.affinityMask;
    ++hr;
  }
  hostRequirements_.rowCount = static_cast<std::uint8_t>(hr);
}

/* ----------------------------- runTaskCensus ----------------------------- */

bool SchedulerBase::runTaskCensus(std::uint8_t repeats) noexcept {
  if (tickCount_ > 0) {
    if (auto* log = componentLog()) {
      log->warning(label(), 0, "task census refused: ticks have already executed");
    }
    return false;
  }

  const auto NOW_US = []() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  };

  censusTlm_ = SchedulerCensusTlm{};
  std::size_t row = 0;
  for (auto& entry : entries_) {
    if (entry.task == nullptr) {
      continue;
    }
    if (row >= TASK_STATS_TLM_CAP) {
      censusTlm_.truncated = 1;
      break;
    }
    auto& r = censusTlm_.rows[row];
    r.fullUid = entry.fullUid;
    r.taskUid = entry.taskUid;
    r.budgetUs = static_cast<std::uint32_t>(entry.periodNs / 1000ULL);

    const auto T0 = NOW_US();
    (void)entry.task->execute();
    const auto T1 = NOW_US();
    r.firstUs = static_cast<std::uint32_t>(T1 - T0);

    std::uint32_t steady = r.firstUs;
    for (std::uint8_t rep = 0; rep < repeats; ++rep) {
      const auto A = NOW_US();
      (void)entry.task->execute();
      const auto B = NOW_US();
      const auto US = static_cast<std::uint32_t>(B - A);
      steady = US < steady ? US : steady;
    }
    r.steadyUs = steady;

    if (r.budgetUs != 0 && r.steadyUs > r.budgetUs) {
      r.verdict = 2;
    } else if (r.budgetUs != 0 && r.firstUs > r.budgetUs) {
      r.verdict = 1;
    }
    if (r.verdict > censusTlm_.overall) {
      censusTlm_.overall = r.verdict;
    }
    ++row;
  }
  censusTlm_.taskCount = static_cast<std::uint16_t>(row);

  if (auto* log = componentLog()) {
    const auto NAME = [](std::uint8_t v) noexcept {
      return v == 0 ? "PASS" : v == 1 ? "WARN" : "FAIL";
    };
    log->info(label(), "========== PREFLIGHT: TASK CENSUS ==========");
    for (std::size_t i = 0; i < censusTlm_.taskCount; ++i) {
      const auto& r = censusTlm_.rows[i];
      log->info(label(), fmt::format("[{}] {:#06x}:{} first={} us steady={} us budget={} us",
                                     NAME(r.verdict), r.fullUid, r.taskUid, r.firstUs, r.steadyUs,
                                     r.budgetUs));
    }
    log->info(label(),
              fmt::format("census verdict: {} ({} task(s){})", NAME(censusTlm_.overall),
                          censusTlm_.taskCount, censusTlm_.truncated != 0 ? ", truncated" : ""));
    log->info(label(), "============================================");
  }
  return true;
}

/* ----------------------------- inFlightSummary ----------------------------- */

std::string SchedulerBase::inFlightSummary(std::size_t cap) const noexcept {
  std::string out;
  std::size_t listed = 0;
  for (const auto& entry : entries_) {
    if (!entry.stillRunning()) {
      continue;
    }
    if (listed == cap) {
      out += ", ...";
      break;
    }
    if (!out.empty()) {
      out += ", ";
    }
    out += entry.componentName != nullptr ? entry.componentName : "?";
    out += ":";
    out += std::to_string(entry.taskUid);
    ++listed;
  }
  return out.empty() ? "none" : out;
}

/* ----------------------------- populateTaskStatsTlm ----------------------------- */

void SchedulerBase::populateTaskStatsTlm() noexcept {
  const auto SAT16 = [](std::uint32_t v) noexcept {
    return static_cast<std::uint16_t>(v > 0xFFFFU ? 0xFFFFU : v);
  };

  std::size_t row = 0;
  for (const auto& entry : entries_) {
    if (row >= TASK_STATS_TLM_CAP) {
      taskStatsTlm_.truncated = 1;
      break;
    }
    if (!entry.stats) {
      continue;
    }
    auto& r = taskStatsTlm_.rows[row];
    r.fullUid = entry.fullUid;
    r.taskUid = entry.taskUid;
    r.lastRuntimeUs16 = SAT16(entry.stats->lastRuntimeUs.load(std::memory_order_relaxed));
    r.maxRuntimeUs = entry.stats->maxRuntimeUs.load(std::memory_order_relaxed);
    r.overruns16 = SAT16(entry.stats->overruns.load(std::memory_order_relaxed));
    r.violations16 = SAT16(entry.stats->deadlineViolations.load(std::memory_order_relaxed));
    ++row;
  }
  taskStatsTlm_.taskCount = static_cast<std::uint16_t>(row);
}

/* ----------------------------- runTablePreflight ----------------------------- */

void SchedulerBase::runTablePreflight() noexcept {
  tablePreflight_ = analyzeTaskTable(entries_, schedule_, poolWorkerCounts());
  tablePreflightPending_ = true;
  logTablePreflight();
}

void SchedulerBase::logTablePreflight() noexcept {
  auto* log = componentLog();
  if (log == nullptr || !tablePreflightPending_) {
    return;
  }
  tablePreflightPending_ = false;

  const auto NAME = [](PreflightVerdict v) noexcept {
    return v == PreflightVerdict::PASS ? "PASS" : v == PreflightVerdict::WARN ? "WARN" : "FAIL";
  };
  const auto& P = tablePreflight_;

  log->info(label(), "========== PREFLIGHT: TASK TABLE ==========");
  log->info(label(), fmt::format("[{}] pool references: {} task(s) on missing pools",
                                 NAME(P.poolRefs), P.missingPoolTasks));
  log->info(label(), fmt::format("[{}] dispatch burst: worst tick {} loads pool {} with {} task(s) "
                                 "over {} worker(s)",
                                 NAME(P.dispatchBurst), P.worstBurstTick, P.worstBurstPool,
                                 P.worstBurstTasks, P.worstBurstWorkers));
  log->info(label(), fmt::format("[{}] chain dispatch order: {} group(s) with a later phase "
                                 "dispatched ahead of an earlier one",
                                 NAME(P.chainOrder), P.chainOrderViolations));
  log->info(label(), fmt::format("[{}] chain shape: {} group(s) with waiters >= workers, "
                                 "{} group(s) mixing rates/offsets",
                                 NAME(P.chainShape), P.chainsOverWorkers, P.mixedRateChains));
  const char* OVERALL = NAME(P.overall());
  if (P.overall() == PreflightVerdict::FAIL) {
    log->error(label(), 0,
               fmt::format("preflight verdict: {} -- the table describes "
                           "behavior the runtime cannot honor as written",
                           OVERALL));
  } else {
    log->info(label(), fmt::format("preflight verdict: {}", OVERALL));
  }
  log->info(label(), "===========================================");
}

/* ----------------------------- initSchedulerLog ----------------------------- */

void SchedulerBase::initSchedulerLog(const std::filesystem::path& logDir) noexcept {
  componentLogPath_ = logDir / SCHED_LOG_FN;
  auto log = std::make_shared<logs::SystemLog>(componentLogPath_.string(),
                                               logs::SystemLog::Mode::ASYNC, 4096);
  log->setLevel(logs::SystemLog::Level::DEBUG);
  setComponentLog(std::move(log));
}

/* ----------------------------- logTprmConfig ----------------------------- */

void SchedulerBase::logTprmConfig(const std::string& source, std::uint8_t numPools,
                                  std::uint8_t workersPerPool, std::uint8_t numTasks) noexcept {
  auto* log = componentLog();
  if (log == nullptr) {
    return;
  }
  log->info(label(), "=== TPRM Configuration ===");
  log->info(label(), fmt::format("Source: {}", source));
  log->info(label(), fmt::format("numPools={}, workersPerPool={}, numTasks={}", numPools,
                                 workersPerPool, numTasks));
  log->info(label(), "==========================");
}

/* ----------------------------- logScheduleLayout ----------------------------- */

void SchedulerBase::logScheduleLayout(std::string_view modeDescription) noexcept {
  if (!componentLog()) {
    return;
  }

  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "    COMPREHENSIVE SCHEDULE LAYOUT      ");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "");

  componentLog()->info(label(), fmt::format("Fundamental Frequency: {} Hz", ffreq_));
  componentLog()->info(label(), fmt::format("Frame Period: {:.3f} ms", 1000.0 / ffreq_));
  componentLog()->info(label(), fmt::format("Total Ticks in Schedule: {}", schedule_.size()));
  componentLog()->info(label(), "");

  std::set<SchedulableTask*> allTasks;
  for (const auto& entry : entries_) {
    allTasks.insert(entry.task);
  }
  componentLog()->info(label(), fmt::format("Total Unique Tasks: {}", allTasks.size()));
  if (!modeDescription.empty()) {
    componentLog()->info(label(), fmt::format("Execution Mode: {}", modeDescription));
  }
  componentLog()->info(label(), "");

  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "  TICK-BY-TICK TASK DISTRIBUTION");
  componentLog()->info(label(), "========================================");

  for (size_t tick = 0; tick < schedule_.size(); ++tick) {
    const auto& indices = schedule_[tick];
    if (indices.empty())
      continue;

    componentLog()->info(label(), "");
    componentLog()->info(label(), fmt::format("Tick {}: {} task(s)", tick, indices.size()));
    componentLog()->info(label(), "----------------------------------------");

    for (std::size_t idx : indices) {
      const TaskEntry& entry = entries_[idx];
      const float freq = entry.config.frequency();
      const int8_t prio = entry.config.priority;
      const std::uint8_t pool = entry.config.poolId;

      // Format: [Component[idx]::taskLabel] freq=X prio=Y pool=Z
      // Instance index extracted from fullUid (fullUid & 0xFF)
      std::string taskInfo = "  [";
      if (entry.componentName != nullptr) {
        taskInfo += entry.componentName;
        const std::uint8_t instanceIdx = static_cast<std::uint8_t>(entry.fullUid & 0xFF);
        taskInfo += fmt::format("[{}]", instanceIdx);
        taskInfo += "::";
      }
      taskInfo += entry.task->getLabel();
      taskInfo += "]";
      taskInfo += fmt::format(" freq={:.1f}Hz", freq);
      taskInfo += fmt::format(" prio={}", prio);
      taskInfo += fmt::format(" pool={}", pool);

      componentLog()->info(label(), taskInfo);
    }
  }
  componentLog()->info(label(), "");

  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "      LOAD BALANCING ANALYSIS");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "");

  size_t maxTasksPerTick = 0;
  size_t minTasksPerTick = SIZE_MAX;
  size_t totalTaskExecutions = 0;
  size_t nonEmptyTicks = 0;

  for (const auto& tickIndices : schedule_) {
    if (!tickIndices.empty()) {
      nonEmptyTicks++;
      maxTasksPerTick = std::max(maxTasksPerTick, tickIndices.size());
      minTasksPerTick = std::min(minTasksPerTick, tickIndices.size());
      totalTaskExecutions += tickIndices.size();
    }
  }

  double avgTasksPerTick =
      nonEmptyTicks > 0 ? static_cast<double>(totalTaskExecutions) / nonEmptyTicks : 0.0;

  componentLog()->info(label(),
                       fmt::format("Non-Empty Ticks: {}/{}", nonEmptyTicks, schedule_.size()));
  componentLog()->info(label(), fmt::format("Max Tasks Per Tick: {}", maxTasksPerTick));
  componentLog()->info(label(), fmt::format("Min Tasks Per Tick: {}",
                                            minTasksPerTick == SIZE_MAX ? 0 : minTasksPerTick));
  componentLog()->info(label(), fmt::format("Avg Tasks Per Tick: {:.2f}", avgTasksPerTick));
  componentLog()->info(label(), "");

  std::map<float, int> freqDist;
  for (const auto& entry : entries_) {
    float freq = entry.config.frequency();
    freqDist[freq]++;
  }

  componentLog()->info(label(), "Task Frequency Distribution:");
  for (const auto& [freq, count] : freqDist) {
    double period_ms = 1000.0 / static_cast<double>(freq);
    componentLog()->info(
        label(), fmt::format("  {:.1f} Hz ({:.1f} ms): {} task(s)", freq, period_ms, count));
  }
  componentLog()->info(label(), "");

  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "      SCHEDULE METRICS SUMMARY");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "");

  componentLog()->info(label(), fmt::format("Unique Tasks: {}", allTasks.size()));
  componentLog()->info(label(), fmt::format("Total Ticks: {}", schedule_.size()));
  componentLog()->info(label(),
                       fmt::format("Non-Empty Ticks: {}/{}", nonEmptyTicks, schedule_.size()));
  componentLog()->info(label(), fmt::format("Empty Ticks: {}", schedule_.size() - nonEmptyTicks));
  componentLog()->info(label(), fmt::format("Max Tasks Per Tick: {}", maxTasksPerTick));
  componentLog()->info(label(), fmt::format("Avg Tasks Per Active Tick: {:.2f}", avgTasksPerTick));

  componentLog()->info(label(), "");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "   CONCURRENT TASK CONTENTION CHECK");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "");

  // Detect potential intra-component data races:
  // Multiple tasks from same fullUid on same tick without shared sequencing.
  std::size_t contentionWarnings = 0;

  for (std::size_t tick = 0; tick < schedule_.size(); ++tick) {
    const auto& indices = schedule_[tick];
    if (indices.size() < 2) {
      continue;
    }

    // Group tasks by fullUid
    std::map<std::uint32_t, std::vector<std::size_t>> byComponent;
    for (std::size_t idx : indices) {
      const TaskEntry& entry = entries_[idx];
      if (entry.fullUid != 0) {
        byComponent[entry.fullUid].push_back(idx);
      }
    }

    // Check each component with multiple concurrent tasks
    for (const auto& [uid, taskIndices] : byComponent) {
      if (taskIndices.size() < 2) {
        continue;
      }

      // Check if all tasks share the same sequence counter (sequenced together)
      bool allSequenced = true;
      std::atomic<int>* sharedCounter = nullptr;

      for (std::size_t idx : taskIndices) {
        const TaskEntry& entry = entries_[idx];
        if (!entry.isSequenced()) {
          allSequenced = false;
          break;
        }
        if (sharedCounter == nullptr) {
          sharedCounter = entry.seqCounter;
        } else if (sharedCounter != entry.seqCounter) {
          allSequenced = false;
          break;
        }
      }

      if (!allSequenced) {
        ++contentionWarnings;
        componentLog()->warning(
            label(), 0,
            fmt::format("CONTENTION: {} tasks from component 0x{:06X} run concurrently on tick {} "
                        "without sequencing barrier",
                        taskIndices.size(), uid, tick));

        for (std::size_t idx : taskIndices) {
          const TaskEntry& entry = entries_[idx];
          componentLog()->warning(label(), 0,
                                  fmt::format("  - [{}] sequenced={}", entry.task->getLabel(),
                                              entry.isSequenced() ? "yes" : "no"));
        }
      }
    }
  }

  if (contentionWarnings == 0) {
    componentLog()->info(label(), "No intra-component contention detected.");
  } else {
    componentLog()->warning(label(), 0,
                            fmt::format("Detected {} potential contention point(s). Consider using "
                                        "sequencing or ensuring tasks do not share mutable state.",
                                        contentionWarnings));
  }

  componentLog()->info(label(), "");
  componentLog()->info(label(), "========================================");
  componentLog()->info(label(), "      END OF SCHEDULE LAYOUT");
  componentLog()->info(label(), "========================================");

  componentLog()->flush();
}

/* ----------------------------- clearTasks ----------------------------- */

void SchedulerBase::clearTasks() noexcept {
  entries_.clear();
  for (auto& tick : schedule_) {
    tick.clear();
  }
}

/* ----------------------------- loadTprm ----------------------------- */

bool SchedulerBase::loadTprm(const std::filesystem::path& tprmDir) noexcept {
  // Build tprm path from fullUid (componentId << 8 | instance 0)
  // Note: Scheduler is always instance 0 and may load TPRM before registration
  const std::uint32_t FULL_UID = static_cast<std::uint32_t>(componentId()) << 8;
  std::filesystem::path tprmPath =
      tprmDir / system_component::SystemComponentBase::tprmFilename(FULL_UID);

  // Check resolver is set
  if (componentResolver_ == nullptr) {
    setLastError("Component resolver not set");
    return false;
  }

  // Check file exists
  if (!std::filesystem::exists(tprmPath)) {
    setLastError("Scheduler tprm not found");
    return false;
  }

  // Read and verify the v3 payload; the body is the schedule image.
  std::vector<std::uint8_t> tprmData;
  {
    using system_component::TprmPayloadCheck;
    const TprmPayloadCheck CHECK = system_component::readTprmPayload(tprmPath, FULL_UID, tprmData);
    if (CHECK != TprmPayloadCheck::OK) {
      // toString returns static storage, which setLastError's pointer
      // contract needs; the fault code reaches the log below.
      setLastError(system_component::toString(CHECK));
      if (auto* log = componentLog()) {
        log->error(label(), system_component::toFaultCode(CHECK),
                   fmt::format("Scheduler tprm rejected ({}): {}",
                               system_component::toString(CHECK), tprmPath.string()));
      }
      return false;
    }
  }

  // Parse header
  if (tprmData.size() < sizeof(SchedulerTprmHeader)) {
    setLastError("Scheduler tprm too small for header");
    return false;
  }

  const auto* header = reinterpret_cast<const SchedulerTprmHeader*>(tprmData.data());

  // Validate size
  const std::size_t EXPECTED_SIZE = schedulerTprmSize(header->numTasks);
  if (tprmData.size() != EXPECTED_SIZE) {
    setLastError("Scheduler tprm size mismatch");
    return false;
  }

  // Backup current schedule for restoration on failure.
  // This prevents leaving the scheduler empty if parsing fails.
  auto backupEntries = std::move(entries_);
  auto backupSchedule = std::move(schedule_);
  entries_.clear();
  schedule_.clear();

  const std::uint8_t* taskEntryBytes = tprmData.data() + sizeof(SchedulerTprmHeader);

  std::uint8_t skippedComponents = 0;
  std::uint8_t skippedTasks = 0;

  for (std::uint8_t i = 0; i < header->numTasks; ++i) {
    // The tprm is a packed byte buffer; task entries sit at an offset that is
    // not naturally aligned for SchedulerTaskEntry. Copy each entry into an
    // aligned local rather than binding a reference to misaligned storage
    // (undefined behavior). This path is init-time, not the per-tick hot path.
    SchedulerTaskEntry entry;
    std::memcpy(&entry, taskEntryBytes + i * sizeof(SchedulerTaskEntry), sizeof(entry));

    // Lookup component by full UID
    auto* component = componentResolver_->getComponent(entry.fullUid);
    if (component == nullptr) {
      ++skippedComponents;
      if (auto* log = componentLog()) {
        log->warning(
            label(), 0,
            fmt::format("TPRM task {}: component 0x{:06X} not found, skipped", i, entry.fullUid));
      }
      continue;
    }

    // Get task by UID
    auto* task = component->taskByUid(entry.taskUid);
    if (task == nullptr) {
      ++skippedTasks;
      if (auto* log = componentLog()) {
        log->warning(
            label(), 0,
            fmt::format("TPRM task {}: taskUid {} not found on component 0x{:06X}, skipped", i,
                        entry.taskUid, entry.fullUid));
      }
      continue;
    }

    // Build TaskConfig from entry
    TaskConfig cfg(entry.freqN, entry.freqD, entry.offset, entry.priority, entry.poolIndex);

    // A task table referencing a pool that does not exist is a config
    // defect; route to pool 0 loudly instead of degrading silently.
    if (entry.poolIndex >= numPools()) {
      cfg.poolId = 0;
      if (auto* log = componentLog()) {
        log->warning(label(), 0,
                     fmt::format("TPRM task {}: pool {} does not exist ({} pool(s)); "
                                 "routing to pool 0",
                                 i, entry.poolIndex, numPools()));
      }
    }

    // Get sequence group if sequenced
    SequenceGroup* seqGroup = nullptr;
    if (!isUnsequenced(entry.sequenceGroup)) {
      seqGroup = component->sequenceGroup(entry.sequenceGroup);
    }

    // Add task to scheduler (pass taskUid for registry registration, componentName for logging)
    const Status RC =
        addTask(*task, cfg, seqGroup, entry.fullUid, entry.taskUid, component->componentName());
    if (RC != Status::SUCCESS) {
      ++skippedTasks;
      if (auto* log = componentLog()) {
        log->warning(
            label(), 0,
            fmt::format("TPRM task {}: addTask failed (rc={}), skipped", i, static_cast<int>(RC)));
      }
    }
  }

  // If no tasks were resolved at all, restore the previous schedule.
  if (entries_.empty() && header->numTasks > 0) {
    entries_ = std::move(backupEntries);
    schedule_ = std::move(backupSchedule);
    setLastError("TPRM reload failed: 0 tasks resolved, keeping previous schedule");
    if (auto* log = componentLog()) {
      log->warning(label(), 0,
                   fmt::format("TPRM reload aborted: {}/{} tasks skipped, 0 resolved -- "
                               "previous schedule restored",
                               skippedComponents + skippedTasks, header->numTasks));
    }
    return false;
  }

  // Log skip summary if any tasks were skipped
  if ((skippedComponents + skippedTasks) > 0 && componentLog() != nullptr) {
    componentLog()->warning(label(), 0,
                            fmt::format("TPRM reload: {}/{} tasks skipped ({} unknown components, "
                                        "{} unknown tasks)",
                                        skippedComponents + skippedTasks, header->numTasks,
                                        skippedComponents, skippedTasks));
  }

  // Static feasibility verdicts for the table just loaded.
  runTablePreflight();
  populateRequirementsTlm();

  // Keep full TPRM binary for INSPECT readback and register with registry
  tprmRaw_ = tprmData;
  registerData(system_core::data::DataCategory::TUNABLE_PARAM, "tunableParams", tprmRaw_.data(),
               tprmRaw_.size());

  // Register health snapshot as OUTPUT for INSPECT readback.
  // Populated on each GET_HEALTH call.
  registerData(system_core::data::DataCategory::OUTPUT, system_component::HOST_REQUIREMENTS_NAME,
               &hostRequirements_, sizeof(system_component::HostRequirements));

  registerData(system_core::data::DataCategory::OUTPUT, "tablePreflight", &tablePreflight_,
               sizeof(TablePreflight));

  registerData(system_core::data::DataCategory::OUTPUT, "requirements", &requirementsTlm_,
               sizeof(SchedulerRequirementsTlm));

  registerData(system_core::data::DataCategory::OUTPUT, "taskCensus", &censusTlm_,
               sizeof(SchedulerCensusTlm));

  registerData(system_core::data::DataCategory::OUTPUT, "taskStats", &taskStatsTlm_,
               sizeof(SchedulerTaskStatsTlm));

  registerData(system_core::data::DataCategory::OUTPUT, "health", &healthTlm_,
               sizeof(SchedulerHealthTlm));

  // Log configuration after loading
  if (componentLog() != nullptr) {
    logTprmConfig(tprmPath.string(), header->numPools, header->workersPerPool, header->numTasks);
  }

  setConfigured(true);
  return true;
}

/* ----------------------------- exportSchedule ----------------------------- */

Status SchedulerBase::exportSchedule(const std::filesystem::path& dbDir) noexcept {
  // Build string table and track offsets
  std::vector<char> stringTable;
  std::vector<std::uint32_t> taskNameOffsets;
  taskNameOffsets.reserve(entries_.size());

  // Helper to add string to table
  auto addString = [&stringTable](const char* str) -> std::uint32_t {
    std::uint32_t offset = static_cast<std::uint32_t>(stringTable.size());
    if (str != nullptr) {
      std::size_t len = std::strlen(str);
      stringTable.insert(stringTable.end(), str, str + len + 1); // Include null
    } else {
      stringTable.push_back('\0');
    }
    return offset;
  };

  // Add task names (use task label)
  for (const auto& entry : entries_) {
    if (entry.task != nullptr) {
      std::string_view labelView = entry.task->getLabel();
      // Need to copy to string since addString expects null-terminated
      std::string labelStr(labelView);
      taskNameOffsets.push_back(addString(labelStr.c_str()));
    } else {
      taskNameOffsets.push_back(addString(nullptr));
    }
  }

  // Build header
  SdatHeader header{};
  header.magic = SDAT_MAGIC;
  header.version = SDAT_VERSION;
  header.flags = 0;
  header.fundamentalFreq = ffreq_;
  header.taskCount = static_cast<std::uint16_t>(entries_.size());
  header.tickCount = static_cast<std::uint16_t>(schedule_.size());
  header.reserved = 0;

  // Build task entries
  std::vector<SdatTaskEntry> taskEntries;
  taskEntries.reserve(entries_.size());

  for (std::size_t i = 0; i < entries_.size(); ++i) {
    const TaskEntry& entry = entries_[i];

    SdatTaskEntry te{};
    te.fullUid = entry.fullUid;
    te.taskUid = entry.taskUid;
    te.poolIndex = entry.config.poolId;
    te.freqN = entry.config.freqN;
    te.freqD = entry.config.freqD;
    te.offset = entry.config.offset;
    te.priority = entry.config.priority;
    te.sequenceGroup =
        entry.isSequenced() ? static_cast<std::uint8_t>(entry.seqPhase) : NO_SEQUENCE_GROUP;
    te.sequencePhase = static_cast<std::uint8_t>(entry.seqPhase);
    te.reserved1 = 0;
    te.nameOffset = taskNameOffsets[i];
    te.reserved2 = 0;

    taskEntries.push_back(te);
  }

  // Build tick schedule (variable length)
  // Format: for each non-empty tick: [SdatTickEntry][uint16_t indices...]
  std::vector<std::uint8_t> tickData;

  for (std::size_t tick = 0; tick < schedule_.size(); ++tick) {
    const auto& indices = schedule_[tick];
    if (indices.empty()) {
      continue;
    }

    SdatTickEntry tickEntry{};
    tickEntry.tick = static_cast<std::uint16_t>(tick);
    tickEntry.taskCount = static_cast<std::uint16_t>(indices.size());

    // Append tick header
    const auto* tickPtr = reinterpret_cast<const std::uint8_t*>(&tickEntry);
    tickData.insert(tickData.end(), tickPtr, tickPtr + sizeof(SdatTickEntry));

    // Append task indices
    for (std::size_t idx : indices) {
      std::uint16_t idx16 = static_cast<std::uint16_t>(idx);
      const auto* idxPtr = reinterpret_cast<const std::uint8_t*>(&idx16);
      tickData.insert(tickData.end(), idxPtr, idxPtr + sizeof(std::uint16_t));
    }
  }

  // Write to file
  std::filesystem::path outputPath = dbDir / SDAT_FILENAME;

  std::ofstream file(outputPath, std::ios::binary);
  if (!file) {
    setLastError("Failed to create sched.rdat");
    return Status::ERROR_IO;
  }

  // Write header
  file.write(reinterpret_cast<const char*>(&header), sizeof(header));

  // Write task entries
  for (const auto& te : taskEntries) {
    file.write(reinterpret_cast<const char*>(&te), sizeof(te));
  }

  // Write tick schedule
  file.write(reinterpret_cast<const char*>(tickData.data()),
             static_cast<std::streamsize>(tickData.size()));

  // Write string table
  file.write(stringTable.data(), static_cast<std::streamsize>(stringTable.size()));

  if (!file) {
    setLastError("Failed to write sched.rdat");
    return Status::ERROR_IO;
  }

  return Status::SUCCESS;
}

/* ----------------------------- Command Handling ----------------------------- */

std::uint8_t SchedulerBase::handleCommand(std::uint16_t opcode,
                                          apex::compat::rospan<std::uint8_t> payload,
                                          std::vector<std::uint8_t>& response) noexcept {
  using system_component::CommandResult;

  switch (opcode) {
  case static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_HEALTH): {
    // Populate the persistent member (also used for INSPECT OUTPUT readback).
    healthTlm_.tickCount = tickCount_;
    healthTlm_.taskCount = static_cast<std::uint32_t>(entries_.size());
    healthTlm_.totalPeriodViolations = static_cast<std::uint32_t>(totalPeriodViolations());
    healthTlm_.totalSkipCount = static_cast<std::uint32_t>(totalSkips());
    healthTlm_.fundamentalFreqHz = ffreq_;
    healthTlm_.poolCount = numPools();
    healthTlm_.sleeping = sleeping_.load(std::memory_order_acquire) ? 1 : 0;
    healthTlm_.skipOnBusy = skipOnBusyEnabled() ? 1 : 0;
    healthTlm_.violationsThisTick = static_cast<std::uint32_t>(periodViolationsThisTick());
    response.resize(sizeof(healthTlm_));
    std::memcpy(response.data(), &healthTlm_, sizeof(healthTlm_));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(SchedulerTlmOpcode::RUN_PREFLIGHT): {
    runTablePreflight();
    response.resize(sizeof(tablePreflight_));
    std::memcpy(response.data(), &tablePreflight_, sizeof(tablePreflight_));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_PREFLIGHT): {
    response.resize(sizeof(tablePreflight_));
    std::memcpy(response.data(), &tablePreflight_, sizeof(tablePreflight_));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_REQUIREMENTS): {
    populateRequirementsTlm();
    response.resize(sizeof(requirementsTlm_));
    std::memcpy(response.data(), &requirementsTlm_, sizeof(requirementsTlm_));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(SchedulerTlmOpcode::RUN_TASK_CENSUS): {
    const std::uint8_t REPEATS = payload.size() >= 1 ? payload[0] : static_cast<std::uint8_t>(3U);
    const bool RAN = runTaskCensus(REPEATS);
    response.push_back(RAN ? 1U : 0U);
    return static_cast<std::uint8_t>(RAN ? CommandResult::SUCCESS : CommandResult::EXEC_FAILED);
  }

  case static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_TASK_CENSUS): {
    response.resize(sizeof(censusTlm_));
    std::memcpy(response.data(), &censusTlm_, sizeof(censusTlm_));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  case static_cast<std::uint16_t>(SchedulerTlmOpcode::GET_TASK_STATS): {
    populateTaskStatsTlm();
    response.resize(sizeof(taskStatsTlm_));
    std::memcpy(response.data(), &taskStatsTlm_, sizeof(taskStatsTlm_));
    return static_cast<std::uint8_t>(CommandResult::SUCCESS);
  }

  default:
    return system_component::SystemComponentBase::handleCommand(opcode, payload, response);
  }
}

} // namespace scheduler
} // namespace system_core
