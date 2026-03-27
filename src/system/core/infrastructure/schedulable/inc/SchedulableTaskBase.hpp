#ifndef APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASKBASE_HPP
#define APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASKBASE_HPP
/**
 * @file SchedulableTaskBase.hpp
 * @brief Base interface for schedulable tasks.
 *
 * Design philosophy:
 *  - Tasks are minimal: just callable + label
 *  - All scheduling config (frequency, priority, affinity) lives in scheduler
 *  - This separation enables scheduler-side optimization without task changes
 *
 * RT-Safety:
 *  - execute(): Direct delegate call, ~5-10ns overhead
 *  - getLabel(): Returns string_view, zero overhead
 *  - Construction: Minimal, assigns two members
 */

#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cstdint>
#include <string_view>

namespace system_core {
namespace schedulable {

/* ----------------------------- SchedulableTaskBase ----------------------------- */

/**
 * @class SchedulableTaskBase
 * @brief Abstract base for schedulable tasks.
 *
 * Minimal interface:
 *  - Callable (DelegateU8): function pointer + context, zero allocation
 *  - Label (string_view): non-owning, caller manages lifetime
 *
 * Layout: ~24 bytes (vtable ptr + delegate + label view)
 */
class SchedulableTaskBase {
public:
  using TaskFn = apex::concurrency::DelegateU8; ///< No-allocation delegate (fnptr + context).

  /**
   * @brief Construct with callable and non-owning label.
   * @param task  Callable.
   * @param label Label view; caller must ensure lifetime.
   */
  SchedulableTaskBase(TaskFn task, std::string_view label) noexcept;

  /** @brief Virtual noexcept destructor. */
  virtual ~SchedulableTaskBase() noexcept = default;

  /** @brief Execute task logic. */
  [[nodiscard]] virtual std::uint8_t execute() noexcept = 0;

  /** @brief Non-owning label view. */
  [[nodiscard]] std::string_view getLabel() const noexcept;

protected:
  TaskFn task_;            ///< Core callable.
  std::string_view label_; ///< Non-owning label.
};

} // namespace schedulable
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULABLE_SCHEDULABLETASKBASE_HPP