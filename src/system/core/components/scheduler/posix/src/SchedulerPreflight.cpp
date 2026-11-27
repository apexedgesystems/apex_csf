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

/// What one group showed over the analysis, and its scratch for the
/// instant being looked at.
struct GroupInfo {
  std::size_t firstIdx{0};     ///< First entry of the group, for its rate and pool.
  bool mixedRate{false};       ///< Rates/offsets differ within the group.
  bool orderHazard{false};     ///< A later phase listed ahead of an earlier one it runs with.
  bool orderMaybe{false};      ///< The same, on a slot analyzed as an upper bound.
  std::size_t worstWaiters{0}; ///< Most tasks waiting on one instant.
  std::uint64_t instant{0};    ///< Instant the fields below belong to.
  int lastPhase{0};            ///< Phase of the group's last task in list order.
  int minPhase{0};             ///< Lowest phase among the group's tasks due.
  std::size_t due{0};          ///< The group's tasks due.
  std::size_t dueAtMin{0};     ///< Of those, the ones at the lowest phase.
};

/// Largest number of (slot, second) instants the analysis looks at for
/// slots that hold tasks paced over several seconds. It bounds the work
/// of a table whose paces share no short common period.
constexpr std::uint64_t PACED_INSTANT_BUDGET = 16384U;

constexpr std::uint8_t COUNT_MAX = 0xFFU;

/// Count one more, stopping at the width of the report's fields.
void countUp(std::uint8_t& count) noexcept {
  if (count < COUNT_MAX) {
    ++count;
  }
}

std::uint64_t gcd(std::uint64_t a, std::uint64_t b) noexcept {
  while (b != 0U) {
    const std::uint64_t T = a % b;
    a = b;
    b = T;
  }
  return a;
}

/// Whether an entry paced over several seconds runs on SLOT in second
/// SECOND of the run. Run k of N in every D seconds falls on tick
/// offset + floor(k * Hz * D / N) (TaskCadence), so the tick runs exactly
/// when its distance X past the offset, taken within the pattern period,
/// is floor(k * Hz * D / N) for some k: the smallest k that reaches X is
/// ceil(X * N / (Hz * D)).
bool runsOn(const TaskConfig& cfg, std::uint64_t hz, std::uint64_t slot,
            std::uint64_t second) noexcept {
  const std::uint64_t HD = hz * cfg.freqD;
  const std::uint64_t TICK = ((second % cfg.freqD) * hz + slot) % HD;
  const std::uint64_t X = (TICK + HD - (cfg.offset % HD)) % HD;
  const std::uint64_t K = (X * cfg.freqN + HD - 1U) / HD;
  return K < cfg.freqN && (K * HD) / cfg.freqN == X;
}

} // namespace

TablePreflight analyzeTaskTable(const std::vector<TaskEntry>& entries,
                                const std::vector<std::vector<std::size_t>>& schedule,
                                const std::vector<std::uint16_t>& poolWorkers) noexcept {
  TablePreflight out{};
  const auto WORKERS_OF = [&poolWorkers](std::uint8_t pool) noexcept {
    return pool < poolWorkers.size() && poolWorkers[pool] > 0 ? poolWorkers[pool]
                                                              : static_cast<std::uint16_t>(1U);
  };

  // ---- Pool references -----------------------------------------------------
  for (const auto& e : entries) {
    if (e.config.poolId >= poolWorkers.size()) {
      ++out.missingPoolTasks;
    }
  }
  if (out.missingPoolTasks > 0) {
    out.poolRefs = PreflightVerdict::FAIL;
  }

  // ---- Sequence groups -------------------------------------------------------
  std::map<const std::atomic<int>*, GroupInfo> groups;
  for (std::size_t i = 0; i < entries.size(); ++i) {
    if (!entries[i].isSequenced()) {
      continue;
    }
    const auto [it, isNew] = groups.try_emplace(entries[i].seqCounter);
    if (isNew) {
      it->second.firstIdx = i;
      continue;
    }
    const auto& first = entries[it->second.firstIdx].config;
    const auto& cur = entries[i].config;
    if (first.freqN != cur.freqN || first.freqD != cur.freqD || first.offset != cur.offset) {
      it->second.mixedRate = true;
    }
  }

  // ---- Every instant the table dispatches -------------------------------------
  // The burst, order and shape checks all ask what is dispatched together,
  // so they are taken instant by instant. A slot whose tasks all run every
  // second is one instant. A slot that holds tasks paced over several
  // seconds repeats after the least common multiple of their paces, and
  // each second of that span is an instant with its own tasks due: two
  // paced tasks can share a slot and never a cycle. When the span would
  // pass the budget, the slot is taken once with every task on it due,
  // which is an upper bound, and it is reported as one.
  const std::uint64_t HZ = schedule.size();
  std::uint64_t budget = PACED_INSTANT_BUDGET;
  std::uint64_t instant = 0;
  float worstFactor = 0.0F;
  std::vector<std::size_t> due;
  std::vector<std::uint16_t> perPool(poolWorkers.empty() ? 1U : poolWorkers.size());
  for (std::size_t slot = 0; slot < schedule.size(); ++slot) {
    const auto& slotList = schedule[slot];

    // The span of the slot, in seconds, and whether it is within budget.
    std::uint64_t span = 1;
    std::size_t paced = 0;
    bool exact = true;
    for (const std::size_t IDX : slotList) {
      const std::uint64_t D = entries[IDX].config.freqD;
      if (D <= 1U) {
        continue;
      }
      ++paced;
      span = span / gcd(span, D) * D;
      if (span > budget) {
        exact = false;
        break;
      }
    }
    // One paced task meets everything else on its slot whenever it runs:
    // taking them all as due is exact and needs no walk.
    if (paced <= 1U) {
      span = 1;
      exact = true;
    } else if (exact) {
      budget -= span;
    } else {
      span = 1;
      if (out.boundedSlots < 0xFFFFU) {
        ++out.boundedSlots;
      }
    }

    for (std::uint64_t second = 0; second < span; ++second) {
      ++instant;
      due.clear();
      for (const std::size_t IDX : slotList) {
        const auto& cfg = entries[IDX].config;
        if (paced > 1U && exact && cfg.freqD > 1U && !runsOn(cfg, HZ, slot, second)) {
          continue;
        }
        due.push_back(IDX);
      }

      // Dispatch burst: the worst load factor on one pool. A burst above
      // the worker count queues tasks within the frame; that is legal,
      // but it is where deadline pressure concentrates -- report the
      // worst factor so offset staggering is an informed choice.
      std::fill(perPool.begin(), perPool.end(), static_cast<std::uint16_t>(0U));
      for (const std::size_t IDX : due) {
        const std::uint8_t POOL =
            entries[IDX].config.poolId < poolWorkers.size() ? entries[IDX].config.poolId : 0U;
        ++perPool[POOL];
      }
      for (std::size_t pool = 0; pool < perPool.size(); ++pool) {
        if (perPool[pool] == 0U) {
          continue;
        }
        const std::uint16_t WORKERS = WORKERS_OF(static_cast<std::uint8_t>(pool));
        const float FACTOR = static_cast<float>(perPool[pool]) / static_cast<float>(WORKERS);
        if (FACTOR > worstFactor) {
          worstFactor = FACTOR;
          out.worstBurstTick = static_cast<std::uint16_t>(slot);
          out.worstBurstTasks = perPool[pool];
          out.worstBurstWorkers = WORKERS;
          out.worstBurstPool = static_cast<std::uint8_t>(pool);
        }
      }

      // Chain order: the list order is the pool-queue order, and a later
      // phase queued ahead of an earlier one it waits for occupies a
      // worker the earlier one may need. Chain shape: every task of a
      // group above the lowest phase due can park, against the workers
      // of the group's pool.
      for (const std::size_t IDX : due) {
        const auto& e = entries[IDX];
        if (!e.isSequenced()) {
          continue;
        }
        GroupInfo& g = groups[e.seqCounter];
        if (g.instant != instant) {
          g.instant = instant;
          g.lastPhase = e.seqPhase;
          g.minPhase = e.seqPhase;
          g.due = 0;
          g.dueAtMin = 0;
        }
        if (e.seqPhase < g.lastPhase) {
          (exact ? g.orderHazard : g.orderMaybe) = true;
        }
        g.lastPhase = e.seqPhase;
        if (e.seqPhase < g.minPhase) {
          g.minPhase = e.seqPhase;
          g.dueAtMin = 0;
        }
        ++g.due;
        if (e.seqPhase == g.minPhase) {
          ++g.dueAtMin;
        }
        g.worstWaiters = std::max(g.worstWaiters, g.due - g.dueAtMin);
      }
    }
  }
  if (worstFactor > 1.0F) {
    out.dispatchBurst = PreflightVerdict::WARN;
  }

  // The verdicts are set where a hazard is seen, apart from its count:
  // the counts stop at the width of their fields.
  bool orderMaybe = false;
  for (const auto& [counter, g] : groups) {
    static_cast<void>(counter);
    if (g.orderHazard) {
      countUp(out.chainOrderViolations);
      out.chainOrder = PreflightVerdict::FAIL;
    } else if (g.orderMaybe) {
      orderMaybe = true;
    }
    if (g.worstWaiters >= WORKERS_OF(entries[g.firstIdx].config.poolId)) {
      countUp(out.chainsOverWorkers);
      out.chainShape = PreflightVerdict::WARN;
    }
    // Rates or offsets that differ within a group are counted and are
    // not a hazard: each tick orders the group's tasks that run on it.
    if (g.mixedRate) {
      countUp(out.mixedRateChains);
    }
  }
  // An order hazard seen only on a slot taken as an upper bound may be
  // between tasks that never run together: it warns and does not fail.
  if (orderMaybe && out.chainOrder == PreflightVerdict::PASS) {
    out.chainOrder = PreflightVerdict::WARN;
  }

  // ---- Sequence references ----------------------------------------------------
  for (const auto& e : entries) {
    if (e.seqRequested && !e.isSequenced()) {
      countUp(out.unsequencedTasks);
      out.seqRefs = PreflightVerdict::FAIL;
    }
  }

  return out;
}

} // namespace scheduler
} // namespace system_core
