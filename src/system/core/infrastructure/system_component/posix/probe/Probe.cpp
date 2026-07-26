/**
 * @file Probe.cpp
 * @brief Compile probe: instantiates the library's public surface. Built at
 *        each declared posix_cpp dialect on hosted builds — so a regression
 *        against the support contract fails the build that owns the claim.
 *
 * Probes carry no target dependencies, so the surface exercised here is the
 * dependency-light core: the component bases that forward-declare their
 * heavy collaborators, the ParamBank staging engine, and the transport
 * provisioning types.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/CoreComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/ParamBank.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/TransportLink.hpp"

using namespace system_core::system_component;

namespace {

struct ProbeParams {
  int32_t rate{100};
};

struct ProbeCoreComponent final : CoreComponentBase {
  ProbeCoreComponent() noexcept { setConfigured(true); }

  [[nodiscard]] uint16_t componentId() const noexcept override { return 7; }
  [[nodiscard]] const char* componentName() const noexcept override { return "Probe"; }
  [[nodiscard]] const char* label() const noexcept override { return "PROBE"; }

protected:
  [[nodiscard]] uint8_t doInit() noexcept override { return 0; }
};

} // namespace

uint32_t probe() {
  ProbeCoreComponent comp;
  const uint8_t RC = comp.init();

  ParamBank<ProbeParams> bank;
  const Status LOADED =
      bank.load(ProbeParams{250}, [](const ProbeParams& p) noexcept { return p.rate > 0; });
  const Status PUBLISHED = bank.publishInitial();

  TransportLink link;

  return comp.fullUid() + RC + static_cast<uint32_t>(LOADED) + static_cast<uint32_t>(PUBLISHED) +
         static_cast<uint32_t>(bank.active().rate) + static_cast<uint32_t>(link.isValid()) +
         static_cast<uint32_t>(TransportKind::NONE);
}
