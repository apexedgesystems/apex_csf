#ifndef APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERPREFLIGHT_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERPREFLIGHT_HPP
/**
 * @file SchedulerPreflight.hpp
 * @brief Static feasibility analysis of a loaded task table.
 *
 * Runs after the table is resolved and the schedule is built, before
 * the clock starts: every check here needs only information already in
 * hand at load time. Verdicts are per-check PASS/WARN/FAIL, logged as
 * one structured section and stored for readback -- the goal is that a
 * config defect announces itself at boot, not at cycle N in flight.
 *
 * Checks:
 *  - Pool references: tasks assigned to pools that do not exist.
 *  - Dispatch burst: worst per-instant task load vs pool workers (offset
 *    collision view -- everything at offset 0 shows up here).
 *  - Chain dispatch order: within a sequence group, the EFFECTIVE
 *    dispatch order (per-tick schedule order = priority-sorted, ties
 *    in insertion order) must be ascending in phase. A later phase
 *    dispatched ahead of an earlier one can park every worker with
 *    the earlier phase still queued behind them -- a hard deadlock on
 *    pools with fewer workers than waiting phases.
 *  - Chain shape: phases-vs-workers depth on the worst instant (parking
 *    pressure). Groups whose tasks differ in rate or offset are counted
 *    for information; each tick orders the tasks that run on it.
 *  - Sequence references: tasks the table sequences whose component
 *    offers no such sequencing; they run unsequenced.
 *
 * The burst, order and shape checks count the tasks dispatched together
 * on an instant: a slot of the schedule, and for a slot that holds tasks
 * paced over several seconds, each second of the span after which their
 * paces repeat. Tasks that share a slot and never a cycle are not counted
 * together. The walk over such spans has a fixed budget; a slot past it
 * is taken with every task on it due, counted in boundedSlots, and an
 * order hazard seen only there is a WARN.
 *
 * The hazard counts are eight bits wide and stop at 255. A verdict is
 * set where its hazard is seen and does not depend on the count.
 *
 * RT-Safety: config-time only (allocates, logs); never on the tick path.
 */

#include "src/system/core/components/scheduler/posix/inc/TaskConfig.hpp"

#include <cstdint>
#include <vector>

namespace system_core {
namespace scheduler {

/* ----------------------------- Verdicts ----------------------------- */

/// Per-check outcome. WARN boots with a documented hazard; FAIL means
/// the table described something the runtime cannot honor as written.
enum class PreflightVerdict : std::uint8_t { PASS = 0, WARN = 1, FAIL = 2 };

/**
 * @struct TablePreflight
 * @brief Verdicts + evidence from the static table analysis.
 *
 * Fixed-size POD so it can be registered for INSPECT readback.
 */
struct TablePreflight {
  PreflightVerdict poolRefs{PreflightVerdict::PASS};      ///< Missing-pool references.
  PreflightVerdict dispatchBurst{PreflightVerdict::PASS}; ///< Worst tick load vs workers.
  PreflightVerdict chainOrder{PreflightVerdict::PASS};    ///< Phase dispatch-order liveness.
  PreflightVerdict chainShape{PreflightVerdict::PASS};    ///< Parking depth on the worst tick.

  std::uint16_t missingPoolTasks{0};    ///< Tasks that referenced absent pools.
  std::uint16_t worstBurstTick{0};      ///< Tick with the highest load factor.
  std::uint16_t worstBurstTasks{0};     ///< Tasks dispatched on that tick (that pool).
  std::uint16_t worstBurstWorkers{0};   ///< Workers serving that pool.
  std::uint8_t worstBurstPool{0};       ///< Pool where the worst burst lands.
  std::uint8_t chainOrderViolations{0}; ///< Groups with order-liveness hazards.
  std::uint8_t chainsOverWorkers{0};    ///< Groups with waiting phases >= workers on a tick.
  std::uint8_t mixedRateChains{0};      ///< Groups mixing rates/offsets (information).

  PreflightVerdict seqRefs{PreflightVerdict::PASS}; ///< Unmet sequencing requests.
  std::uint8_t unsequencedTasks{0};                 ///< Tasks sequenced by the table only.

  /// Slots taken with every task on them due because the paces of their
  /// multi-second tasks share no period within the analysis budget. The
  /// burst and shape figures are upper bounds where such a slot decides
  /// them, and an order hazard seen only there warns.
  std::uint16_t boundedSlots{0};

  /** @brief Worst verdict across all checks. */
  [[nodiscard]] PreflightVerdict overall() const noexcept {
    PreflightVerdict v = poolRefs;
    if (dispatchBurst > v) {
      v = dispatchBurst;
    }
    if (chainOrder > v) {
      v = chainOrder;
    }
    if (chainShape > v) {
      v = chainShape;
    }
    if (seqRefs > v) {
      v = seqRefs;
    }
    return v;
  }
};

/* ----------------------------- Analysis ----------------------------- */

/**
 * @brief Analyze a resolved task table + built schedule.
 * @param entries      Scheduler task entries (post-load).
 * @param schedule     Per-tick entry-index lists (post-build; per-tick
 *                     order is the effective dispatch order).
 * @param poolWorkers  Worker count per constructed pool; index = poolId.
 * @return Verdicts + evidence. Pure function of its inputs.
 *
 * @note Config-time only.
 */
[[nodiscard]] TablePreflight
analyzeTaskTable(const std::vector<TaskEntry>& entries,
                 const std::vector<std::vector<std::size_t>>& schedule,
                 const std::vector<std::uint16_t>& poolWorkers) noexcept;

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_SCHEDULERPREFLIGHT_HPP
