#ifndef APEX_DEMOS_EDGE_PIPELINE_MODEL_HPP
#define APEX_DEMOS_EDGE_PIPELINE_MODEL_HPP
/**
 * @file PipelineModel.hpp
 * @brief Three-phase sequenced pipeline: pre -> transform -> post.
 *
 * Exercises SequenceGroup ordering across scheduler pool threads inside a
 * live executive. The three tasks share one data buffer with no locks --
 * the phase ordering IS the synchronization. Each stage checksums its
 * input against what the prior stage claims to have written, so any
 * ordering failure is counted, not just felt:
 *   - PRE (phase 1): fill the buffer from a frame seed, publish checksum.
 *   - TRANSFORM (phase 2): verify PRE's checksum, transform in place,
 *     publish the transformed checksum.
 *   - POST (phase 3): verify the transformed checksum, count the frame.
 *
 * State telemetry exposes framesCompleted / orderViolations so a checkout
 * can assert ordering held under load (violations must stay 0).
 *
 * @note RT-safe: all three stages are bounded compute over a fixed buffer;
 *       no allocation, no I/O, no blocking beyond the phase wait itself.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/ModelData.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"

#include <array>
#include <cstdint>

#include <fmt/format.h>

namespace apex_edge_demo {

using system_core::data::State;
using system_core::system_component::SwModelBase;

/* ----------------------------- Telemetry ----------------------------- */

/**
 * @struct PipelineState
 * @brief Observable pipeline health: frames through vs ordering failures.
 */
struct PipelineState {
  std::uint32_t framesCompleted{0}; ///< Full pre->transform->post cycles.
  std::uint32_t orderViolations{0}; ///< Checksum mismatches (must stay 0).
  std::uint32_t lastChecksum{0};    ///< Post-stage checksum of the last frame.
};

/* ----------------------------- PipelineModel ----------------------------- */

/**
 * @class PipelineModel
 * @brief Sequenced three-stage compute chain over one shared buffer.
 */
class PipelineModel final : public SwModelBase {
public:
  /* ----------------------------- Component Identity ----------------------------- */

  static constexpr std::uint16_t COMPONENT_ID = 134;
  static constexpr const char* COMPONENT_NAME = "PipelineModel";

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return COMPONENT_NAME; }

  /* ----------------------------- Task UIDs ----------------------------- */

  enum class TaskUid : std::uint8_t {
    PRE = 1,       ///< Phase 1: fill buffer from frame seed.
    TRANSFORM = 2, ///< Phase 2: verify + transform in place.
    POST = 3       ///< Phase 3: verify + count the frame.
  };

  PipelineModel() noexcept = default;
  ~PipelineModel() override = default;

protected:
  /* ----------------------------- Lifecycle ----------------------------- */

  [[nodiscard]] std::uint8_t doInit() noexcept override {
    using system_core::data::DataCategory;
    using system_core::system_component::Status;

    if (!createSequenceGroup(GROUP, MAX_PHASE)) {
      return static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED);
    }

    bool ok = registerSequencedTask<PipelineModel, &PipelineModel::pre>(
        static_cast<std::uint8_t>(TaskUid::PRE), this, "pipePre", GROUP, 1);
    ok = ok && registerSequencedTask<PipelineModel, &PipelineModel::transform>(
                   static_cast<std::uint8_t>(TaskUid::TRANSFORM), this, "pipeTransform", GROUP, 2);
    ok = ok && registerSequencedTask<PipelineModel, &PipelineModel::post>(
                   static_cast<std::uint8_t>(TaskUid::POST), this, "pipePost", GROUP, 3);
    if (!ok) {
      return static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED);
    }

    registerData(DataCategory::STATE, "state", &state_.get(), sizeof(PipelineState));
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

public:
  /* ----------------------------- Pipeline Stages ----------------------------- */

  /** @brief Phase 1: fill the buffer from this frame's seed. @note RT-safe. */
  std::uint8_t pre() noexcept {
    std::uint32_t v = ++seed_;
    for (auto& word : buffer_) {
      v = v * LCG_A + LCG_C;
      word = v;
    }
    preChecksum_ = checksum();
    return 0U;
  }

  /** @brief Phase 2: verify PRE's write, transform in place. @note RT-safe. */
  std::uint8_t transform() noexcept {
    if (checksum() != preChecksum_) {
      ++state_.get().orderViolations;
    }
    for (auto& word : buffer_) {
      word = (word ^ XOR_MASK) + ROT_SALT;
    }
    transformChecksum_ = checksum();
    return 0U;
  }

  /** @brief Phase 3: verify TRANSFORM's write, count the frame. @note RT-safe. */
  std::uint8_t post() noexcept {
    auto& s = state_.get();
    if (checksum() != transformChecksum_) {
      ++s.orderViolations;
    }
    s.lastChecksum = transformChecksum_;
    ++s.framesCompleted;
    if ((s.framesCompleted % LOG_EVERY) == 0U) {
      if (auto* log = componentLog()) {
        log->info(label(), fmt::format("pipeline frames={} violations={}", s.framesCompleted,
                                       s.orderViolations));
      }
    }
    return 0U;
  }

  /** @brief Telemetry accessor. @note RT-safe. */
  [[nodiscard]] const PipelineState& state() const noexcept { return state_.get(); }

private:
  [[nodiscard]] std::uint32_t checksum() const noexcept {
    std::uint32_t sum = 0U;
    for (const auto& word : buffer_) {
      sum = (sum << 1) ^ word;
    }
    return sum;
  }

  static constexpr std::uint8_t GROUP = 0;
  static constexpr std::uint32_t LOG_EVERY = 25;
  static constexpr int MAX_PHASE = 3;
  static constexpr std::size_t WORDS = 1024;
  static constexpr std::uint32_t LCG_A = 1664525U;
  static constexpr std::uint32_t LCG_C = 1013904223U;
  static constexpr std::uint32_t XOR_MASK = 0xA5A5A5A5U;
  static constexpr std::uint32_t ROT_SALT = 0x3C6EF35FU;

  std::array<std::uint32_t, WORDS> buffer_{};
  std::uint32_t seed_{0};
  std::uint32_t preChecksum_{0};
  std::uint32_t transformChecksum_{0};

  State<PipelineState> state_{};
};

} // namespace apex_edge_demo

#endif // APEX_DEMOS_EDGE_PIPELINE_MODEL_HPP
