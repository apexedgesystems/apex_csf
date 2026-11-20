#ifndef APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
/**
 * @file ReloadTarget.hpp
 * @brief A model for the RELOAD_LIBRARY tests: the test links one instance
 *        and plugin libraries provide its replacements, one under the same
 *        name and one under another.
 *
 * An instance writes one line to its component log when it initializes
 * and registers one task. Through a ReloadProbe the test sees which
 * instance runs that task, can hold a run of it inside the task, and
 * learns when an instance is destroyed and whether one of its runs was
 * still inside the task at that moment.
 */

#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"
#include "src/system/core/infrastructure/system_component/posix/inc/SwModelBase.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace executive {
namespace test {

/**
 * @brief What the instances report to the test, and how the test holds a
 *        task run.
 *
 * The test owns the probe. Plugin instances are made by a library's
 * factory, not by the test, so the probe's address travels to every
 * instance of the process in an environment variable.
 */
struct ReloadProbe {
  static constexpr std::size_t MAX_INSTANCES = 8;
  static constexpr const char* ENV_NAME = "APEX_RELOAD_TEST_PROBE";

  /// While set, a task run stays inside the task (HOLD_LIMIT at most).
  std::atomic<bool> hold{false};
  /// Instances constructed so far; an instance's number is its place in that order, from 1.
  std::atomic<std::uint32_t> constructed{0};
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> entered{};  ///< Task runs begun.
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> finished{}; ///< Task runs ended.
  std::array<std::atomic<bool>, MAX_INSTANCES + 1> destroyed{};
  /// Set when an instance was destroyed with one of its runs inside the task.
  std::array<std::atomic<bool>, MAX_INSTANCES + 1> destroyedInTask{};

  /// Make this probe the one instances constructed from here on find.
  void publish() noexcept {
    std::array<char, 32> text{};
    (void)std::snprintf(text.data(), text.size(), "%p", static_cast<void*>(this));
    (void)::setenv(ENV_NAME, text.data(), 1);
  }

  static void withdraw() noexcept { (void)::unsetenv(ENV_NAME); }

  /// The published probe, or nullptr when there is none.
  [[nodiscard]] static ReloadProbe* published() noexcept {
    const char* text = std::getenv(ENV_NAME);
    void* address = nullptr;
    if (text == nullptr || std::sscanf(text, "%p", &address) != 1) {
      return nullptr;
    }
    return static_cast<ReloadProbe*>(address);
  }

  /// True while a run of instance N is inside the task.
  [[nodiscard]] bool inTask(std::size_t n) const noexcept {
    return entered[n].load(std::memory_order_acquire) !=
           finished[n].load(std::memory_order_acquire);
  }
};

class ReloadTarget : public system_core::system_component::SwModelBase {
public:
  static constexpr std::uint16_t COMPONENT_ID = 251;
  static constexpr const char* COMPONENT_NAME = "ReloadTarget";
  static constexpr std::uint8_t STEP_TASK_UID = 1;
  static constexpr std::chrono::seconds HOLD_LIMIT{10};

  /// ORIGIN says where this instance came from; it is part of the start-up
  /// line. NAME is the component name the instance reports.
  explicit ReloadTarget(const char* origin = "loaded from a library",
                        const char* name = COMPONENT_NAME) noexcept
      : origin_(origin), name_(name), probe_(ReloadProbe::published()) {
    if (probe_ != nullptr) {
      const std::size_t NUMBER = probe_->constructed.fetch_add(1, std::memory_order_acq_rel) + 1U;
      number_ = (NUMBER <= ReloadProbe::MAX_INSTANCES) ? NUMBER : 0U;
    }
  }

  ~ReloadTarget() override {
    if (probe_ != nullptr) {
      if (probe_->inTask(number_)) {
        probe_->destroyedInTask[number_].store(true, std::memory_order_release);
      }
      probe_->destroyed[number_].store(true, std::memory_order_release);
    }
  }

  ReloadTarget(const ReloadTarget&) = delete;
  ReloadTarget& operator=(const ReloadTarget&) = delete;
  ReloadTarget(ReloadTarget&&) = delete;
  ReloadTarget& operator=(ReloadTarget&&) = delete;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return COMPONENT_ID; }
  [[nodiscard]] const char* componentName() const noexcept override { return name_; }
  [[nodiscard]] const char* label() const noexcept override { return "RELOAD_TARGET"; }

  /// The line an instance of ORIGIN writes when it initializes.
  [[nodiscard]] static std::string startupLine(const char* origin) {
    return std::string("ReloadTarget ") + origin + ": started";
  }

  /// The scheduled task: counts the run and stays inside while the probe holds.
  [[nodiscard]] std::uint8_t step() noexcept {
    if (probe_ == nullptr) {
      return 0;
    }
    probe_->entered[number_].fetch_add(1, std::memory_order_acq_rel);
    const auto DEADLINE = std::chrono::steady_clock::now() + HOLD_LIMIT;
    while (probe_->hold.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < DEADLINE) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    probe_->finished[number_].fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    if (auto* log = componentLog()) {
      (void)log->info(label(), startupLine(origin_));
    }
    return registerTask<ReloadTarget, &ReloadTarget::step>(STEP_TASK_UID, this, "step") ? 0 : 1;
  }

private:
  const char* origin_;
  const char* name_;
  ReloadProbe* probe_;    ///< nullptr when no test published one.
  std::size_t number_{0}; ///< This instance's number on the probe; 0 when it has none.
};

/// A replacement with ReloadTarget's component id under another name.
class RenamedReloadTarget final : public ReloadTarget {
public:
  static constexpr const char* COMPONENT_NAME = "ReloadRenamed";

  RenamedReloadTarget() noexcept : ReloadTarget("loaded from a library", COMPONENT_NAME) {}
};

} // namespace test
} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
