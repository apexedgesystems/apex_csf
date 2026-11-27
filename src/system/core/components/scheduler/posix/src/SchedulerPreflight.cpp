/**
 * @file SchedulerPreflight.cpp
 * @brief Static feasibility analysis of a loaded task table.
 */

#include "src/system/core/components/scheduler/posix/inc/SchedulerPreflight.hpp"

#include <algorithm>
#include <map>

namespace system_core {
namespace scheduler {

namespace {

/// Per-group aggregation collected in one pass over the entries.
struct GroupInfo {
  std::vector<std::size_t> entryIdx; ///< Entries in this group.
  bool mixedRate{false};             ///< Rates/offsets differ within the group.
};

} // namespace

TablePreflight analyzeTaskTable(const std::vector<TaskEntry>& entries,
                                const std::vector<std::vector<std::size_t>>& schedule,
                                const std::vector<std::uint16_t>& poolWorkers) noexcept {
  TablePreflight out{};

  // ---- Pool references -----------------------------------------------------
  for (const auto& e : entries) {
    if (e.config.poolId >= poolWorkers.size()) {
      ++out.missingPoolTasks;
    }
  }
  if (out.missingPoolTasks > 0) {
    out.poolRefs = PreflightVerdict::FAIL;
  }

  // ---- Dispatch burst (worst per-tick load per pool) ------------------------
  // A burst above the worker count queues tasks within the frame; that is
  // legal, but it is where deadline pressure concentrates -- report the
  // worst factor so offset staggering is an informed choice.
  float worstFactor = 0.0F;
  for (std::size_t tick = 0; tick < schedule.size(); ++tick) {
    std::map<std::uint8_t, std::uint16_t> perPool;
    for (const std::size_t IDX : schedule[tick]) {
      const std::uint8_t POOL =
          entries[IDX].config.poolId < poolWorkers.size() ? entries[IDX].config.poolId : 0U;
      ++perPool[POOL];
    }
    for (const auto& [pool, count] : perPool) {
      const std::uint16_t WORKERS = pool < poolWorkers.size() && poolWorkers[pool] > 0
                                        ? poolWorkers[pool]
                                        : static_cast<std::uint16_t>(1U);
      const float FACTOR = static_cast<float>(count) / static_cast<float>(WORKERS);
      if (FACTOR > worstFactor) {
        worstFactor = FACTOR;
        out.worstBurstTick = static_cast<std::uint16_t>(tick);
        out.worstBurstTasks = count;
        out.worstBurstWorkers = WORKERS;
        out.worstBurstPool = pool;
      }
    }
  }
  if (worstFactor > 1.0F) {
    out.dispatchBurst = PreflightVerdict::WARN;
  }

  // ---- Sequence groups -------------------------------------------------------
  std::map<const std::atomic<int>*, GroupInfo> groups;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (entries[i].isSequenced()) {
      auto& g = groups[entries[i].seqCounter];
      if (!g.entryIdx.empty()) {
        const auto& first = entries[g.entryIdx.front()].config;
        const auto& cur = entries[i].config;
        if (first.freqN != cur.freqN || first.freqD != cur.freqD || first.offset != cur.offset) {
          g.mixedRate = true;
        }
      }
      g.entryIdx.push_back(i);
    }
  }

  for (const auto& [counter, g] : groups) {
    // Chain order: walk every tick's dispatch list; phases of one group
    // must appear in ascending order, because the per-tick list order IS
    // the pool-queue order and a later phase parked ahead of an earlier
    // one occupies a worker the earlier phase may need.
    bool orderHazard = false;
    for (const auto& tickList : schedule) {
      int lastPhase = 0;
      for (const std::size_t IDX : tickList) {
        const auto& e = entries[IDX];
        if (!e.isSequenced() || e.seqCounter != entries[g.entryIdx.front()].seqCounter) {
          continue;
        }
        if (e.seqPhase < lastPhase) {
          orderHazard = true;
          break;
        }
        lastPhase = e.seqPhase;
      }
      if (orderHazard) {
        break;
      }
    }
    if (orderHazard) {
      ++out.chainOrderViolations;
    }

    // Chain shape: waiting phases (all but the lowest phase on a tick can
    // park) vs workers on the group's pool. A task waits only for the
    // tasks of its group dispatched on the same tick, so the count is
    // taken tick by tick and the worst tick decides.
    const std::uint8_t POOL = entries[g.entryIdx.front()].config.poolId;
    const std::uint16_t WORKERS = POOL < poolWorkers.size() && poolWorkers[POOL] > 0
                                      ? poolWorkers[POOL]
                                      : static_cast<std::uint16_t>(1U);
    std::size_t worstWaiters = 0;
    for (const auto& tickList : schedule) {
      bool any = false;
      int minPhase = 0;
      for (const std::size_t IDX : tickList) {
        const auto& e = entries[IDX];
        if (e.seqCounter != counter) {
          continue;
        }
        minPhase = any ? std::min(minPhase, e.seqPhase) : e.seqPhase;
        any = true;
      }
      std::size_t waiters = 0;
      for (const std::size_t IDX : tickList) {
        const auto& e = entries[IDX];
        if (e.seqCounter == counter && e.seqPhase > minPhase) {
          ++waiters;
        }
      }
      worstWaiters = std::max(worstWaiters, waiters);
    }
    if (worstWaiters >= WORKERS) {
      ++out.chainsOverWorkers;
    }
    if (g.mixedRate) {
      ++out.mixedRateChains;
    }
  }

  if (out.chainOrderViolations > 0) {
    out.chainOrder = PreflightVerdict::FAIL;
  }
  // Rates or offsets that differ within a group are counted and are not
  // a hazard: each tick orders the group's tasks that run on it.
  if (out.chainsOverWorkers > 0) {
    out.chainShape = PreflightVerdict::WARN;
  }

  // ---- Sequence references ----------------------------------------------------
  for (const auto& e : entries) {
    if (e.seqRequested && !e.isSequenced() && out.unsequencedTasks < 0xFFU) {
      ++out.unsequencedTasks;
    }
  }
  if (out.unsequencedTasks > 0) {
    out.seqRefs = PreflightVerdict::FAIL;
  }

  return out;
}

} // namespace scheduler
} // namespace system_core
