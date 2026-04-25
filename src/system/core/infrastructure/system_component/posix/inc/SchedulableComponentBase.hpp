#ifndef APEX_SYSTEM_COMPONENT_SCHEDULABLE_COMPONENT_BASE_HPP
#define APEX_SYSTEM_COMPONENT_SCHEDULABLE_COMPONENT_BASE_HPP
/**
 * @file SchedulableComponentBase.hpp
 * @brief Base class for components that can have scheduled tasks.
 *
 * Provides task registration and sequence group management for components
 * that participate in scheduled execution (models, support components, drivers).
 *
 * Non-schedulable components (executive, core infrastructure) inherit directly
 * from SystemComponentBase without task registration capability.
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SchedulableTask.hpp"
#include "src/system/core/infrastructure/schedulable/inc/SequenceGroup.hpp"
#include "src/system/core/infrastructure/schedulable/inc/TaskBuilder.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace system_core {
namespace system_component {

// Re-export types for convenience
using apex::concurrency::DelegateU8;
using system_core::schedulable::bindMember;
using system_core::schedulable::SchedulableTask;
using system_core::schedulable::SequenceGroup;

/* ----------------------------- Constants ----------------------------- */

/// Maximum tasks per schedulable component.
constexpr std::size_t MAX_TASKS_PER_COMPONENT = 16;

/// Maximum sequence groups per schedulable component.
constexpr std::size_t MAX_GROUPS_PER_COMPONENT = 4;

/* ----------------------------- SchedulableComponentBase ----------------------------- */

/**
 * @class SchedulableComponentBase
 * @brief Abstract base for components with scheduled tasks.
 *
 * Provides:
 *  - Centralized task storage with UID lookup
 *  - Sequence group management for task ordering
 *  - Task registration helpers (registerTask, registerSequencedTask)
 *
 * Derived classes (SimModelBase, SupportComponentBase, DriverBase):
 *  - Override componentType() to return appropriate type
 *  - Define task methods and register them during init()
 *  - Define TaskUid enum for task identification
 */
class SchedulableComponentBase : public SystemComponentBase {
public:
  /** @brief Default constructor. */
  SchedulableComponentBase() noexcept { setConfigured(true); }

  /** @brief Virtual destructor. */
  ~SchedulableComponentBase() override = default;

  // Non-copyable, non-movable (components have bound delegates)
  SchedulableComponentBase(const SchedulableComponentBase&) = delete;
  SchedulableComponentBase& operator=(const SchedulableComponentBase&) = delete;
  SchedulableComponentBase(SchedulableComponentBase&&) = delete;
  SchedulableComponentBase& operator=(SchedulableComponentBase&&) = delete;

protected:
  /**
   * @brief Boot-time initialization hook.
   * @return Status code (SUCCESS on success).
   * @note NOT RT-safe: May allocate, perform I/O.
   * @note Called by base init().
   */
  [[nodiscard]] std::uint8_t doInit() noexcept override = 0;

public:
  /** @brief Get number of registered tasks. */
  [[nodiscard]] std::size_t taskCount() const noexcept { return taskCount_; }

  /**
   * @brief Look up task by UID.
   * @param uid Task identifier.
   * @return Pointer to SchedulableTask, or nullptr if not found.
   */
  [[nodiscard]] SchedulableTask* taskByUid(std::uint8_t uid) noexcept override {
    for (std::size_t i = 0; i < taskCount_; ++i) {
      if (tasks_[i].uid == uid && tasks_[i].task.has_value()) {
        return &tasks_[i].task.value();
      }
    }
    return nullptr;
  }

  /** @brief Get number of sequence groups. */
  [[nodiscard]] std::size_t sequenceGroupCount() const noexcept { return groupCount_; }

  /**
   * @brief Get sequence group by index.
   * @param idx Group index.
   * @return Pointer to SequenceGroup, or nullptr if not found.
   */
  [[nodiscard]] SequenceGroup* sequenceGroup(std::uint8_t idx) noexcept override {
    if (idx < groupCount_ && groups_[idx]) {
      return groups_[idx].get();
    }
    return nullptr;
  }

  /**
   * @brief Load tunable parameters from TPRM file.
   * @param tprmDir Directory containing extracted TPRM files.
   * @return true on success, false if no TPRM found or load failed (uses defaults).
   * @pre Must call setInstanceIndex() first to assign UID.
   * @note Override in derived class to load component-specific TPRM.
   * @note Default implementation does nothing and returns true.
   * @note NOT RT-safe: File I/O.
   */
  bool loadTprm(const std::filesystem::path& /*tprmDir*/) noexcept override { return true; }

protected:
  /**
   * @brief Register an independent task (no sequencing).
   * @tparam T Component class type.
   * @tparam Fn Pointer to member function.
   * @param uid Task UID.
   * @param self Pointer to component instance.
   * @param label Task label for debugging.
   * @return true on success.
   */
  template <typename T, std::uint8_t (T::*Fn)() noexcept>
  bool registerTask(std::uint8_t uid, T* self, std::string_view label) noexcept {
    if (taskCount_ >= MAX_TASKS_PER_COMPONENT) {
      return false;
    }
    auto& entry = tasks_[taskCount_++];
    entry.uid = uid;
    entry.task.emplace(bindMember<T, Fn>(self), label);
    entry.groupIdx = NO_GROUP;
    entry.phase = 0;
    return true;
  }

  /**
   * @brief Register a sequenced task (belongs to a group at specific phase).
   * @tparam T Component class type.
   * @tparam Fn Pointer to member function.
   * @param uid Task UID.
   * @param self Pointer to component instance.
   * @param label Task label for debugging.
   * @param groupIdx Sequence group index.
   * @param phase Phase within group.
   * @return true on success.
   */
  template <typename T, std::uint8_t (T::*Fn)() noexcept>
  bool registerSequencedTask(std::uint8_t uid, T* self, std::string_view label,
                             std::uint8_t groupIdx, int phase) noexcept {
    if (taskCount_ >= MAX_TASKS_PER_COMPONENT) {
      return false;
    }
    if (groupIdx >= groupCount_ || !groups_[groupIdx]) {
      return false;
    }

    auto& entry = tasks_[taskCount_++];
    entry.uid = uid;
    entry.task.emplace(bindMember<T, Fn>(self), label);
    entry.groupIdx = groupIdx;
    entry.phase = static_cast<std::uint8_t>(phase);

    // Register with sequence group
    groups_[groupIdx]->addTask(entry.task.value(), phase);
    return true;
  }

  /**
   * @brief Create a sequence group.
   * @param idx Group index (0 to MAX_GROUPS_PER_COMPONENT-1).
   * @param maxPhase Maximum phase (= total sequenced task count in group).
   * @return true on success.
   */
  bool createSequenceGroup(std::uint8_t idx, int maxPhase) noexcept {
    if (idx >= MAX_GROUPS_PER_COMPONENT) {
      return false;
    }
    groups_[idx] = std::make_unique<SequenceGroup>(maxPhase);
    if (idx >= groupCount_) {
      groupCount_ = idx + 1;
    }
    return true;
  }

private:
  /// Marker for no sequence group.
  static constexpr std::uint8_t NO_GROUP = 0xFF;

  /// Internal task storage.
  struct TaskEntry {
    std::uint8_t uid = 0;
    std::optional<SchedulableTask> task;
    std::uint8_t groupIdx = NO_GROUP;
    std::uint8_t phase = 0;
  };

  std::array<TaskEntry, MAX_TASKS_PER_COMPONENT> tasks_{};
  std::size_t taskCount_ = 0;

  std::array<std::unique_ptr<SequenceGroup>, MAX_GROUPS_PER_COMPONENT> groups_{};
  std::size_t groupCount_ = 0;
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_SCHEDULABLE_COMPONENT_BASE_HPP
