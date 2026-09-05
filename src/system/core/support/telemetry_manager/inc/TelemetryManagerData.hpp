#ifndef APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
#define APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
/**
 * @file TelemetryManagerData.hpp
 * @brief Aggregated data structures for the TelemetryManager component.
 *
 * The structs themselves are spec-generated (apex_data.toml is the
 * source of truth); this header gathers them plus the subscription
 * capacity constant the runtime loops share with the spec.
 *
 * @note RT-safe: Pure data structures, no allocation or I/O.
 */

#include <cstddef>
#include <cstdint>

#include "src/system/core/support/telemetry_manager/.auto/TelemetryManagerHealthTlm_auto.hpp"
#include "src/system/core/support/telemetry_manager/.auto/TelemetryManagerState_auto.hpp"
#include "src/system/core/support/telemetry_manager/.auto/TelemetryManagerTprm_auto.hpp"

namespace system_core {
namespace support {

/// Maximum telemetry subscriptions.
static constexpr std::size_t MAX_TELEMETRY_SUBSCRIPTIONS = 32;

static_assert(sizeof(TelemetryManagerTprm) ==
                  8 + MAX_TELEMETRY_SUBSCRIPTIONS * sizeof(TelemetrySubscription),
              "subscription capacity diverged from the spec");

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_TELEMETRY_MANAGER_DATA_HPP
