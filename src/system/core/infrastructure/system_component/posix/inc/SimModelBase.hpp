#ifndef APEX_SYSTEM_COMPONENT_SIM_MODEL_BASE_HPP
#define APEX_SYSTEM_COMPONENT_SIM_MODEL_BASE_HPP
/**
 * @file SimModelBase.hpp
 * @brief Base class for simulation models.
 *
 * Inherits task machinery from SchedulableComponentBase and adds:
 *   - ComponentType::MODEL classification
 *
 * Inheritance hierarchy:
 *   SystemComponentBase
 *     -> SchedulableComponentBase (task machinery)
 *       -> SimModelBase (returns MODEL type)
 *         -> SwModelBase (software/environment models)
 *         -> HwModelBase (hardware emulation models)
 *
 * For new code, prefer SwModelBase (software/environment models) or
 * HwModelBase (hardware emulation models) over direct SimModelBase inheritance.
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SchedulableComponentBase.hpp"

namespace system_core {
namespace system_component {

/* ----------------------------- SimModelBase ----------------------------- */

/**
 * @class SimModelBase
 * @brief Abstract base for simulation models.
 *
 * Provides ComponentType::MODEL classification.
 *
 * Derived classes:
 *   - Define task methods (e.g., step(), pre1(), post())
 *   - Register tasks during doInit() using registerTask/registerSequencedTask
 *   - Define TaskUid enum for task identification
 */
class SimModelBase : public SchedulableComponentBase {
public:
  /** @brief Default constructor. */
  SimModelBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~SimModelBase() override = default;

  // Non-copyable, non-movable (inherited from SchedulableComponentBase)
  SimModelBase(const SimModelBase&) = delete;
  SimModelBase& operator=(const SimModelBase&) = delete;
  SimModelBase(SimModelBase&&) = delete;
  SimModelBase& operator=(SimModelBase&&) = delete;

  /**
   * @brief Get component type classification.
   * @return SW_MODEL or HW_MODEL depending on derived class.
   * @note Pure virtual - derived classes (SwModelBase, HwModelBase) must implement.
   */
  [[nodiscard]] ComponentType componentType() const noexcept override = 0;
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_SIM_MODEL_BASE_HPP
