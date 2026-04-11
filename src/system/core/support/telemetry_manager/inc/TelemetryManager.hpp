#ifndef APEX_SUPPORT_TELEMETRY_MANAGER_HPP
#define APEX_SUPPORT_TELEMETRY_MANAGER_HPP
/**
 * @file TelemetryManager.hpp
 * @brief Push telemetry component for automated data streaming to C2.
 *
 * TelemetryManager reads registered data blocks from the registry and
 * pushes them via postInternalTelemetry() at configured rates. This
 * enables ground systems (Zenith) to receive telemetry without polling.
 *
 * Configuration:
 *   - TPRM: Pre-defined subscription table (up to 32 channels)
 *   - Runtime: ADD/REMOVE/LIST/CLEAR subscriptions via handleCommand()
 *
 * Each subscription specifies:
 *   - fullUid + category: Which data block to read from registry
 *   - offset + length: Byte range within the block (0,0 = entire block)
 *   - opcode: APROTO opcode for the outbound telemetry packet
 *   - rateDiv: Push every N collect ticks (rate decimation)
 *
 * The collect task runs at a configurable base rate (default 10 Hz).
 * Each subscription's effective rate = collectRateHz / rateDiv.
 *
 * Tasks:
 *   - collect (configurable Hz): Iterate subscriptions, read data, push
 *   - telemetry (1 Hz): Log status counters
 *
 * @note collect task is RT-safe: registry reads are O(1), bus push is lock-free.
 * @note telemetry task is NOT RT-safe (fmt::format).
 */

#include "src/system/core/support/telemetry_manager/inc/TelemetryManagerData.hpp"

#include "src/system/core/components/registry/apex/inc/ApexRegistry.hpp"
#include "src/system/core/infrastructure/data/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/apex/inc/SupportComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <fmt/format.h>

#include <cstdint>
#include <cstring>
#include <filesystem>

namespace system_core {
namespace support {

using system_core::data::State;
using system_core::data::TunableParam;
using system_core::system_component::CommandResult;
using Status = system_core::system_component::Status;

/* ----------------------------- Constants ----------------------------- */

/// Telemetry opcodes for TelemetryManager.
enum class TelemetryManagerOpcode : std::uint16_t {
  GET_STATS = 0x0100,           ///< Return TelemetryManagerHealthTlm.
  ADD_SUBSCRIPTION = 0x0200,    ///< Add subscription (16-byte payload).
  REMOVE_SUBSCRIPTION = 0x0201, ///< Remove subscription (1-byte slot index).
  LIST_SUBSCRIPTIONS = 0x0202,  ///< Return active subscription table.
  CLEAR_ALL = 0x0203            ///< Remove all subscriptions.
};

/* ----------------------------- TelemetryManager ----------------------------- */

class TelemetryManager final : public system_component::SupportComponentBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 201;
  static constexpr const char* COMPONENT_NAME = "TelemetryManager";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }
  [[nodiscard]] const char* label() const noexcept override { return "TLM_MGR"; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    COLLECT = 1,  ///< Data collection + push (configurable Hz).
    TELEMETRY = 2 ///< Status logging (1 Hz).
  };

  /* ----------------------------- Construction ----------------------------- */

  TelemetryManager() noexcept = default;
  ~TelemetryManager() override = default;

  /* ----------------------------- Wiring ----------------------------- */

  /**
   * @brief Set registry pointer for data block access.
   * @param reg Pointer to frozen registry (must remain valid during operation).
   * @note Call before init(), typically in executive's registerComponents().
   */
  void setRegistry(registry::ApexRegistry* reg) noexcept { registry_ = reg; }

  /* ----------------------------- Task Methods ----------------------------- */

  /**
   * @brief Collect and push telemetry for active subscriptions.
   *
   * Iterates the subscription table. For each active subscription whose
   * rate divisor aligns with the current collect tick, reads bytes from
   * the registry and pushes via postInternalTelemetry().
   *
   * @return 0 on success.
   * @note RT-safe: registry reads O(1), bus push lock-free, no allocation.
   */
  std::uint8_t collect() noexcept {
    auto* bus = internalBus();
    if (bus == nullptr || registry_ == nullptr) {
      return 0;
    }

    auto& s = state_.get();
    ++s.collectCount;

    const auto& tprm = tunableParams_.get();
    for (std::size_t i = 0; i < MAX_TELEMETRY_SUBSCRIPTIONS; ++i) {
      const auto& sub = tprm.subscriptions[i];
      if (sub.active == 0 || sub.fullUid == 0) {
        continue;
      }

      // Rate decimation: push every rateDiv ticks
      if (sub.rateDiv > 1 && (s.collectCount % sub.rateDiv) != 0) {
        continue;
      }

      // Look up data in registry
      auto* entry = registry_->getData(sub.fullUid, static_cast<data::DataCategory>(sub.category));
      if (entry == nullptr || !entry->isValid()) {
        ++s.sendFailures;
        continue;
      }

      // Get byte range
      apex::compat::span<const std::uint8_t> bytes;
      if (sub.offset == 0 && sub.length == 0) {
        bytes = entry->getBytes();
      } else {
        const std::size_t LEN = (sub.length == 0) ? (entry->size - sub.offset) : sub.length;
        bytes = entry->getBytes(sub.offset, LEN);
      }

      if (bytes.empty()) {
        ++s.sendFailures;
        continue;
      }

      // Push to external interface
      const bool OK = bus->postInternalTelemetry(sub.fullUid, sub.opcode, bytes);
      if (OK) {
        ++s.packetsSent;
      } else {
        ++s.sendFailures;
      }
    }

    return 0;
  }

  /**
   * @brief Log telemetry status (1 Hz).
   * @return 0 on success.
   * @note NOT RT-safe: uses fmt::format.
   */
  std::uint8_t telemetry() noexcept {
    const auto& s = state_.get();
    auto* log = componentLog();
    if (log != nullptr) {
      log->info(label(), fmt::format("active={} sent={} fail={} collect={}", s.activeCount,
                                     s.packetsSent, s.sendFailures, s.collectCount));
    }
    return 0;
  }

  /* ----------------------------- Command Handling ----------------------------- */

  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {

    switch (opcode) {
    case static_cast<std::uint16_t>(TelemetryManagerOpcode::GET_STATS): {
      TelemetryManagerHealthTlm tlm{};
      const auto& s = state_.get();
      tlm.collectCount = s.collectCount;
      tlm.packetsSent = s.packetsSent;
      tlm.sendFailures = s.sendFailures;
      tlm.activeCount = s.activeCount;
      response.resize(sizeof(tlm));
      std::memcpy(response.data(), &tlm, sizeof(tlm));
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case static_cast<std::uint16_t>(TelemetryManagerOpcode::ADD_SUBSCRIPTION): {
      if (payload.size() < sizeof(TelemetrySubscription)) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      TelemetrySubscription sub{};
      std::memcpy(&sub, payload.data(), sizeof(sub));
      sub.active = 1;
      if (sub.rateDiv == 0) {
        sub.rateDiv = 1;
      }

      // Find first inactive slot
      auto& tprm = tunableParams_.get();
      for (std::size_t i = 0; i < MAX_TELEMETRY_SUBSCRIPTIONS; ++i) {
        if (tprm.subscriptions[i].active == 0) {
          tprm.subscriptions[i] = sub;
          recomputeActiveCount();
          return static_cast<std::uint8_t>(CommandResult::SUCCESS);
        }
      }
      return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT); // Table full
    }

    case static_cast<std::uint16_t>(TelemetryManagerOpcode::REMOVE_SUBSCRIPTION): {
      if (payload.empty()) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_PAYLOAD);
      }
      const std::uint8_t SLOT = payload[0];
      if (SLOT >= MAX_TELEMETRY_SUBSCRIPTIONS) {
        return static_cast<std::uint8_t>(CommandResult::INVALID_ARGUMENT);
      }
      tunableParams_.get().subscriptions[SLOT] = TelemetrySubscription{};
      recomputeActiveCount();
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case static_cast<std::uint16_t>(TelemetryManagerOpcode::LIST_SUBSCRIPTIONS): {
      // Return the full subscription table
      const auto& tprm = tunableParams_.get();
      response.resize(sizeof(tprm.subscriptions));
      std::memcpy(response.data(), &tprm.subscriptions, sizeof(tprm.subscriptions));
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    case static_cast<std::uint16_t>(TelemetryManagerOpcode::CLEAR_ALL): {
      auto& tprm = tunableParams_.get();
      for (auto& sub : tprm.subscriptions) {
        sub = TelemetrySubscription{};
      }
      state_.get().activeCount = 0;
      return static_cast<std::uint8_t>(CommandResult::SUCCESS);
    }

    default:
      return SupportComponentBase::handleCommand(opcode, payload, response);
    }
  }

  /* ----------------------------- TPRM ----------------------------- */

  bool loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    if (!isRegistered()) {
      return false;
    }

    char filename[32];
    std::snprintf(filename, sizeof(filename), "%06x.tprm", fullUid());
    const std::filesystem::path TPRM_PATH = tprmDir / filename;

    if (!std::filesystem::exists(TPRM_PATH)) {
      setConfigured(true);
      return false;
    }

    std::string error;
    TelemetryManagerTprm loaded{};
    if (apex::helpers::files::hex2cpp(TPRM_PATH.string(), loaded, error)) {
      tunableParams_.get() = loaded;
      setConfigured(true);
      recomputeActiveCount();

      auto* log = componentLog();
      if (log != nullptr) {
        log->info(label(), fmt::format("TPRM loaded: collectRate={} Hz, {} active subscriptions",
                                       loaded.collectRateHz, state_.get().activeCount));
      }
      return true;
    }

    setConfigured(true);
    return false;
  }

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;

    // Register tasks
    registerTask<TelemetryManager, &TelemetryManager::collect>(
        static_cast<std::uint8_t>(TaskUid::COLLECT), this, "collect");
    registerTask<TelemetryManager, &TelemetryManager::telemetry>(
        static_cast<std::uint8_t>(TaskUid::TELEMETRY), this, "telemetry");

    // Register data for registry
    registerData(DataCategory::TUNABLE_PARAM, "tunableParams", &tunableParams_.get(),
                 sizeof(TelemetryManagerTprm));
    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(TelemetryManagerState));

    recomputeActiveCount();

    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

private:
  /* ----------------------------- Helpers ----------------------------- */

  void recomputeActiveCount() noexcept {
    std::uint16_t count = 0;
    const auto& tprm = tunableParams_.get();
    for (const auto& sub : tprm.subscriptions) {
      if (sub.active != 0 && sub.fullUid != 0) {
        ++count;
      }
    }
    state_.get().activeCount = count;
  }

  /* ----------------------------- Data Members ----------------------------- */

  TunableParam<TelemetryManagerTprm> tunableParams_{};
  State<TelemetryManagerState> state_{};
  registry::ApexRegistry* registry_{nullptr};
};

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_TELEMETRY_MANAGER_HPP
