/**
 * @file SchedulerStatus.cpp
 * @brief String representations for scheduler status codes.
 */

#include "src/system/core/components/scheduler/apex/inc/SchedulerStatus.hpp"

namespace system_core {
namespace scheduler {

const char* toString(Status s) noexcept {
  switch (s) {
  case Status::SUCCESS:
    return "SUCCESS";
  case Status::ERROR_TFREQN_GT_FFREQ:
    return "ERROR_TFREQN_GT_FFREQ";
  case Status::ERROR_FFREQ_MOD_TFREQN_DNE0:
    return "ERROR_FFREQ_MOD_TFREQN_DNE0";
  case Status::ERROR_OFFSET_GTE_TPT:
    return "ERROR_OFFSET_GTE_TPT";
  case Status::ERROR_TFREQD_LT1:
    return "ERROR_TFREQD_LT1";
  case Status::ERROR_TASK_EXECUTION_FAIL:
    return "ERROR_TASK_EXECUTION_FAIL";
  case Status::ERROR_THREAD_LAUNCH_FAIL:
    return "ERROR_THREAD_LAUNCH_FAIL";
  case Status::ERROR_IO:
    return "ERROR_IO";
  case Status::ERROR_THREADPOOL_OVERLOAD:
    return "ERROR_THREADPOOL_OVERLOAD";
  case Status::ERROR_AFFINITY_SETTING_FAIL:
    return "ERROR_AFFINITY_SETTING_FAIL";
  case Status::ERROR_POLICY_SETTING_FAIL:
    return "ERROR_POLICY_SETTING_FAIL";
  case Status::WARN_TASK_STARVATION:
    return "WARN_TASK_STARVATION";
  case Status::WARN_TASK_NON_SUCCESS_RET:
    return "WARN_TASK_NON_SUCCESS_RET";
  case Status::WARN_CPU_UNDERUTILIZATION:
    return "WARN_CPU_UNDERUTILIZATION";
  case Status::WARN_PERIOD_VIOLATION:
    return "WARN_PERIOD_VIOLATION";
  case Status::EOE_SCHEDULER:
    return "EOE_SCHEDULER";
  }
  return "UNKNOWN_STATUS";
}

} // namespace scheduler
} // namespace system_core