#ifndef APEX_SYSTEM_COMPONENT_SW_MODEL_BASE_HPP
#define APEX_SYSTEM_COMPONENT_SW_MODEL_BASE_HPP
/**
 * @file SwModelBase.hpp
 * @brief Base class for software/environment simulation models.
 *
 * Software models simulate environmental or computational phenomena:
 *   - Gravity models (EGM2008, WGS84)
 *   - Atmosphere models (NRLMSISE, Jacchia)
 *   - Terrain/elevation models
 *   - Signal propagation models
 *   - Math/physics utilities
 *
 * SwModelBase inherits task machinery from SchedulableComponentBase and adds
 * ComponentType::SW_MODEL classification. Use this base class for models that
 * simulate software-computable phenomena rather than hardware behavior.
 *
 * All functions are RT-safe unless noted otherwise.
 */

#include "src/system/core/infrastructure/system_component/apex/inc/SchedulableComponentBase.hpp"

namespace system_core {
namespace system_component {

/* ----------------------------- SwModelBase ----------------------------- */

/**
 * @class SwModelBase
 * @brief Abstract base for software/environment simulation models.
 *
 * Provides organizational clarity for models that simulate environmental
 * or computational phenomena rather than hardware behavior.
 *
 * Example derived classes: GravityModel, AtmosphereModel, TerrainModel.
 */
class SwModelBase : public SchedulableComponentBase {
public:
  /** @brief Default constructor. */
  SwModelBase() noexcept = default;

  /** @brief Virtual destructor. */
  ~SwModelBase() override = default;

  // Non-copyable, non-movable (inherited from SchedulableComponentBase)
  SwModelBase(const SwModelBase&) = delete;
  SwModelBase& operator=(const SwModelBase&) = delete;
  SwModelBase(SwModelBase&&) = delete;
  SwModelBase& operator=(SwModelBase&&) = delete;

  /** @brief Component type for software models. */
  [[nodiscard]] ComponentType componentType() const noexcept override {
    return ComponentType::SW_MODEL;
  }
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_COMPONENT_SW_MODEL_BASE_HPP
