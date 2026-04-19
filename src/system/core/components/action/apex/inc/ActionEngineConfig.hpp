#ifndef APEX_SYSTEM_CORE_ACTION_ENGINE_CONFIG_HPP
#define APEX_SYSTEM_CORE_ACTION_ENGINE_CONFIG_HPP
/**
 * @file ActionEngineConfig.hpp
 * @brief Compile-time configuration for action engine table sizes.
 *
 * Default values are sized for POSIX targets and match or exceed
 * cFS LC/SC capacity (176 watchpoints, 64 RTS). MCU deployments
 * override with smaller values via a platform-specific config header.
 *
 * To customize, define APEX_ACTION_ENGINE_CUSTOM_CONFIG before including
 * any action engine header, and provide a header that defines the
 * ActionEngineConfig struct with your values.
 */

#include <cstddef>

namespace system_core {
namespace data {

/* ----------------------------- ActionEngineConfig ----------------------------- */

/**
 * @struct ActionEngineConfig
 * @brief Compile-time table sizes for the action engine.
 *
 * POSIX defaults are sized for real mission workloads. All defined
 * watchpoints, groups, and notifications evaluate every tick -- no
 * artificial "active set" limit. Override for MCU deployments.
 */
struct ActionEngineConfig {
  /// Maximum watchpoints evaluated each tick.
  static constexpr std::size_t WATCHPOINT_COUNT = 128;

  /// Maximum watchpoint groups (actionpoints) evaluated each tick.
  static constexpr std::size_t GROUP_COUNT = 32;

  /// Maximum event notifications.
  static constexpr std::size_t NOTIFICATION_COUNT = 128;

  /// Maximum pending actions in the queue.
  static constexpr std::size_t ACTION_QUEUE_SIZE = 64;

  /// Maximum concurrent RTS execution slots.
  static constexpr std::size_t RTS_SLOT_COUNT = 32;

  /// Maximum concurrent ATS execution slots.
  static constexpr std::size_t ATS_SLOT_COUNT = 16;

  /// Maximum steps per sequence.
  static constexpr std::size_t SEQUENCE_MAX_STEPS = 16;

  /// Maximum bytes for watchpoint threshold comparison.
  static constexpr std::size_t WATCH_VALUE_SIZE = 8;

  /// Maximum watchpoint refs per group.
  static constexpr std::size_t GROUP_MAX_REFS = 16;

  /// Maximum sequence catalog entries (RTS + ATS definitions on disk).
  static constexpr std::size_t SEQUENCE_CATALOG_SIZE = 256;

  /// Maximum watchpoint ID for the ID-to-index lookup table.
  static constexpr std::size_t WATCHPOINT_ID_LIMIT = 256;

  /// Maximum event ID for the event dispatch lookup table.
  static constexpr std::size_t EVENT_ID_LIMIT = 512;
};

/// Active configuration. Override by defining APEX_ACTION_ENGINE_CUSTOM_CONFIG.
#ifdef APEX_ACTION_ENGINE_CUSTOM_CONFIG
#include APEX_ACTION_ENGINE_CUSTOM_CONFIG
#else
using Config = ActionEngineConfig;
#endif

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_ENGINE_CONFIG_HPP
