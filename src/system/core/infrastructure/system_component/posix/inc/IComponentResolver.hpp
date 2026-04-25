#ifndef APEX_SYSTEM_CORE_ICOMPONENT_RESOLVER_HPP
#define APEX_SYSTEM_CORE_ICOMPONENT_RESOLVER_HPP
/**
 * @file IComponentResolver.hpp
 * @brief Abstract interface for component lookup by fullUid.
 *
 * Design:
 *   - Minimal interface that allows components to be looked up by their fullUid.
 *   - Implemented by registry, executive, or any other component management system.
 *   - Allows interface and other components to depend on abstraction, not registry.
 *
 * This interface exists to break the dependency between interface and registry.
 * Core components should not depend on each other directly; they depend on
 * shared infrastructure abstractions.
 */

#include <cstdint>

namespace system_core {
namespace system_component {

// Forward declaration
class SystemComponentBase;

/* ----------------------------- IComponentResolver ----------------------------- */

/**
 * @class IComponentResolver
 * @brief Abstract interface for component lookup by fullUid.
 *
 * Implementers:
 *   - RegistryBase (system_core::registry)
 *   - ApexExecutive (provides adapter)
 *
 * @note RT-safe: Implementations must ensure getComponent() is RT-safe.
 */
class IComponentResolver {
public:
  virtual ~IComponentResolver() = default;

  /**
   * @brief Look up a component by its full UID.
   * @param fullUid Component's full UID (componentId << 8 | instanceIndex).
   * @return Pointer to component, or nullptr if not found.
   * @note RT-safe: Must not allocate or block.
   */
  [[nodiscard]] virtual SystemComponentBase* getComponent(std::uint32_t fullUid) noexcept = 0;

  /**
   * @brief Const version of component lookup.
   * @param fullUid Component's full UID.
   * @return Const pointer to component, or nullptr if not found.
   * @note RT-safe: Must not allocate or block.
   */
  [[nodiscard]] virtual const SystemComponentBase*
  getComponent(std::uint32_t fullUid) const noexcept = 0;
};

} // namespace system_component
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ICOMPONENT_RESOLVER_HPP
