#ifndef APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
#define APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
/**
 * @file ReloadTarget.hpp
 * @brief A model for the RELOAD_LIBRARY tests: the test links one instance
 *        and plugin libraries provide its replacements, one under the same
 *        name and one under another.
 *
 * An instance writes one line to its component log when it initializes,
 * registers one output block that holds its own number, and registers one
 * task in a sequence group. Through a ReloadProbe the test sees which
 * instance runs that task and on which thread each instance was
 * initialized, can hold a run inside the task, and learns when an
 * instance is destroyed and whether one of its runs was still inside the
 * task at that moment, and whether each instance was attached to the
 * internal bus and told so before its task first ran, and on which
 * thread it loaded parameters from a file and whether one of its runs
 * was inside the task then, and can hold an instance inside the call
 * that tells it the bus is ready and learns whether a command reached the
 * instance meanwhile. Variants differ from the linked instance in
 * the size of the output block or in how the task is sequenced, for the swaps that must be refused.
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
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <vector>

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
  /// Hash of the id of the thread that initialized the instance; 0 until then.
  std::array<std::atomic<std::size_t>, MAX_INSTANCES + 1> initThread{};
  /// Parameter loads the instance took from a file.
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> tprmLoads{};
  /// Set when a parameter load ran while a run of the instance was inside the task.
  std::array<std::atomic<bool>, MAX_INSTANCES + 1> tprmLoadedInTask{};
  /// Hash of the id of the thread that ran the last parameter load; 0 until then.
  std::array<std::atomic<std::size_t>, MAX_INSTANCES + 1> tprmLoadThread{};
  /// Set once a task run of the instance found the internal bus attached.
  std::array<std::atomic<bool>, MAX_INSTANCES + 1> busInTask{};
  /// Times the instance was told the bus is ready.
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> busReadyCalls{};
  /// Set when the bus was attached at the moment the instance was told so.
  std::array<std::atomic<bool>, MAX_INSTANCES + 1> busAtReady{};
  /// Task runs the instance had begun when it was told the bus is ready.
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> runsBeforeBusReady{};
  /// While set, an instance told the bus is ready stays inside that call (HOLD_LIMIT at most).
  std::atomic<bool> holdBusReady{false};
  /// Set while the instance is inside the call that tells it the bus is ready.
  std::array<std::atomic<bool>, MAX_INSTANCES + 1> inBusReady{};
  /// POKE commands the instance has handled.
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> pokes{};
  /// Of those, the ones that arrived while it was being told the bus is ready.
  std::array<std::atomic<std::uint32_t>, MAX_INSTANCES + 1> pokesInBusReady{};

  /// The value initThread holds for the calling thread.
  [[nodiscard]] static std::size_t thisThread() noexcept {
    return std::hash<std::thread::id>{}(std::this_thread::get_id()) | 1U;
  }

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
  static constexpr std::uint8_t STEP_GROUP = 0;
  static constexpr std::chrono::seconds HOLD_LIMIT{10};
  /// A command an instance only counts.
  static constexpr std::uint16_t POKE_OPCODE = 0x0100;

  /// The registered output block: the number of the instance that owns it.
  struct Output {
    std::uint32_t instance{0};
    std::uint32_t spare{0};
  };

  /// What an instance registers.
  enum class Shape : std::uint8_t {
    STANDARD,       ///< The whole output block; the task in STEP_GROUP at phase 1.
    SHORTER_OUTPUT, ///< Half the output block under the same name.
    UNSEQUENCED     ///< The task outside any sequence group.
  };

  /// ORIGIN says where this instance came from; it is part of the start-up
  /// line. NAME is the component name the instance reports.
  explicit ReloadTarget(const char* origin = "loaded from a library",
                        const char* name = COMPONENT_NAME, Shape shape = Shape::STANDARD) noexcept
      : origin_(origin), name_(name), shape_(shape), probe_(ReloadProbe::published()) {
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
    if (internalBus() != nullptr) {
      probe_->busInTask[number_].store(true, std::memory_order_release);
    }
    const auto DEADLINE = std::chrono::steady_clock::now() + HOLD_LIMIT;
    while (probe_->hold.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < DEADLINE) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    probe_->finished[number_].fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }

  /// Parameters: the file's presence is all the instance takes from it.
  system_core::system_component::TprmIngest
  loadTprm(const std::filesystem::path& tprmDir) noexcept override {
    using system_core::system_component::TprmIngest;
    setConfigured(true);
    std::error_code ec;
    if (!std::filesystem::exists(tprmDir / tprmFilename(fullUid()), ec)) {
      return TprmIngest::NONE;
    }
    if (probe_ != nullptr) {
      if (probe_->inTask(number_)) {
        probe_->tprmLoadedInTask[number_].store(true, std::memory_order_release);
      }
      probe_->tprmLoadThread[number_].store(ReloadProbe::thisThread(), std::memory_order_release);
      probe_->tprmLoads[number_].fetch_add(1, std::memory_order_acq_rel);
    }
    return TprmIngest::LOADED;
  }

  void onBusReady() noexcept override {
    if (probe_ == nullptr) {
      return;
    }
    probe_->runsBeforeBusReady[number_].store(probe_->entered[number_].load(),
                                              std::memory_order_release);
    probe_->busAtReady[number_].store(internalBus() != nullptr, std::memory_order_release);
    probe_->busReadyCalls[number_].fetch_add(1, std::memory_order_acq_rel);

    probe_->inBusReady[number_].store(true, std::memory_order_release);
    const auto DEADLINE = std::chrono::steady_clock::now() + HOLD_LIMIT;
    while (probe_->holdBusReady.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < DEADLINE) {
      std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    probe_->inBusReady[number_].store(false, std::memory_order_release);
  }

  /// POKE is counted, with whether it found the instance inside onBusReady().
  [[nodiscard]] std::uint8_t handleCommand(std::uint16_t opcode,
                                           apex::compat::rospan<std::uint8_t> payload,
                                           std::vector<std::uint8_t>& response) noexcept override {
    if (opcode != POKE_OPCODE || probe_ == nullptr) {
      return SwModelBase::handleCommand(opcode, payload, response);
    }
    if (probe_->inBusReady[number_].load(std::memory_order_acquire)) {
      probe_->pokesInBusReady[number_].fetch_add(1, std::memory_order_acq_rel);
    }
    probe_->pokes[number_].fetch_add(1, std::memory_order_acq_rel);
    return 0;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    if (auto* log = componentLog()) {
      (void)log->info(label(), startupLine(origin_));
    }
    if (probe_ != nullptr) {
      probe_->initThread[number_].store(ReloadProbe::thisThread(), std::memory_order_release);
    }

    output_.instance = static_cast<std::uint32_t>(number_);
    const std::size_t OUTPUT_BYTES =
        (shape_ == Shape::SHORTER_OUTPUT) ? sizeof(output_.instance) : sizeof(output_);
    bool ok =
        registerData(system_core::data::DataCategory::OUTPUT, "output", &output_, OUTPUT_BYTES);
    if (shape_ == Shape::UNSEQUENCED) {
      ok = ok && registerTask<ReloadTarget, &ReloadTarget::step>(STEP_TASK_UID, this, "step");
    } else {
      ok = ok && createSequenceGroup(STEP_GROUP, 1) &&
           registerSequencedTask<ReloadTarget, &ReloadTarget::step>(STEP_TASK_UID, this, "step",
                                                                    STEP_GROUP, 1);
    }
    return ok ? 0 : 1;
  }

private:
  const char* origin_;
  const char* name_;
  Shape shape_;
  ReloadProbe* probe_;    ///< nullptr when no test published one.
  std::size_t number_{0}; ///< This instance's number on the probe; 0 when it has none.
  Output output_{};
};

/// A replacement with ReloadTarget's component id under another name.
class RenamedReloadTarget final : public ReloadTarget {
public:
  static constexpr const char* COMPONENT_NAME = "ReloadRenamed";

  RenamedReloadTarget() noexcept : ReloadTarget("loaded from a library", COMPONENT_NAME) {}
};

/// A replacement whose output block is shorter than the registered one.
class ShorterOutputReloadTarget final : public ReloadTarget {
public:
  ShorterOutputReloadTarget() noexcept
      : ReloadTarget("loaded from a library", ReloadTarget::COMPONENT_NAME, Shape::SHORTER_OUTPUT) {
  }
};

/// A replacement that does not sequence the task the schedule sequences.
class UnsequencedReloadTarget final : public ReloadTarget {
public:
  UnsequencedReloadTarget() noexcept
      : ReloadTarget("loaded from a library", ReloadTarget::COMPONENT_NAME, Shape::UNSEQUENCED) {}
};

} // namespace test
} // namespace executive

#endif // APEX_SYSTEM_CORE_EXECUTIVE_UTST_RELOAD_TARGET_HPP
