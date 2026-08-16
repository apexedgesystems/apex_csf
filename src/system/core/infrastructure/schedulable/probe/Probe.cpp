/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built at
 *        each declared posix_cpp dialect on hosted builds -- so a regression
 *        against the support contract fails the build that owns the claim.
 *
 * Probes carry no target dependencies; the CUDA task is excluded (it needs
 * the toolkit), so the surface here is the CPU task, the binding helpers,
 * and the sequencing primitives.
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"

namespace {

struct ProbeOwner {
  std::uint8_t run() { return 0U; }
};

std::uint8_t freeTask() noexcept { return 0U; }

} // namespace

std::uint32_t probe() {
  using namespace system_core::schedulable;

  ProbeOwner owner;
  SchedulableTask member(bindMember<ProbeOwner, &ProbeOwner::run>(&owner), "member");
  SchedulableTask lambda(bindLambda([]() -> std::uint8_t { return 0U; }), "lambda");
  SchedulableTask freeFn(bindFreeFunction(&freeTask), "free");

  SequenceGroup seq(2);
  seq.addTask(member, 1);
  seq.addTask(lambda, 2);
  const bool WAITED = waitForPhase(*seq.counter(), 1); // Fast path: counter starts at 1.
  advancePhase(*seq.counter(), seq.maxPhase());
  seq.reset();

  const SeqInfo* info = seq.getSeqInfo(&member);

  return static_cast<std::uint32_t>(member.execute()) +
         static_cast<std::uint32_t>(lambda.execute()) +
         static_cast<std::uint32_t>(freeFn.execute()) +
         static_cast<std::uint32_t>(member.getLabel().size()) +
         static_cast<std::uint32_t>(info != nullptr ? info->phase : 0) +
         static_cast<std::uint32_t>(seq.maxPhase()) + static_cast<std::uint32_t>(WAITED);
}
