#ifndef APEX_SYSTEM_CORE_DATA_EVENT_NOTIFICATION_HPP
#define APEX_SYSTEM_CORE_DATA_EVENT_NOTIFICATION_HPP
/**
 * @file EventNotification.hpp
 * @brief Lightweight event callbacks for watchpoint/group triggers.
 *
 * EventNotification is the observation side of the event system. When a
 * watchpoint or group fires an eventId, registered notifications are
 * invoked. Unlike sequences (which execute actions over time), notifications
 * are immediate, single-cycle responses for logging, mode changes, or
 * component-level reactions.
 *
 * Two notification modes:
 *   1. Callback delegate: External function called with (eventId, fireCount).
 *   2. Built-in log: logLabel + logMessage + logSeverity written to system
 *      log automatically. No callback needed. TPRM-configurable.
 *
 * Both modes can coexist on the same notification. If only log fields are
 * set (no callback), the ActionComponent logs directly to the system log.
 *
 * RT-safe: No allocation. Fixed-size table. Delegate invocation is O(1).
 *
 * Usage (callback):
 * @code
 *   EventNotification note{};
 *   note.eventId = 1;
 *   note.callback = {logFn, &logCtx};
 *   note.armed = true;
 * @endcode
 *
 * Usage (built-in log, TPRM-driven):
 * @code
 *   EventNotification note{};
 *   note.eventId = 1;
 *   note.armed = true;
 *   std::strncpy(note.logLabel, "ACTION", sizeof(note.logLabel) - 1);
 *   std::strncpy(note.logMessage, "ALTITUDE LIMIT >150m", sizeof(note.logMessage) - 1);
 *   note.logSeverity = 1; // WARNING
 * @endcode
 */

#include "src/system/core/components/action/apex/inc/ActionEngineConfig.hpp"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <array>
#include <cstdint>

namespace system_core {
namespace data {

/* ----------------------------- Constants ----------------------------- */

/// Maximum concurrent event notifications.
constexpr std::size_t EVENT_NOTIFICATION_TABLE_SIZE = Config::NOTIFICATION_COUNT;

/// Log severity levels for built-in log notifications.
enum class LogSeverity : std::uint8_t {
  INFO = 0,    ///< Informational message.
  WARNING = 1, ///< Warning (logged with warning severity).
  ERR = 2      ///< Error (logged with error severity).
};

/* ----------------------------- EventNotifyDelegate ----------------------------- */

/// Delegate signature for event notifications.
/// Parameters: (eventId, fireCount). No return value.
using EventNotifyDelegate = apex::concurrency::Delegate<void, std::uint16_t, std::uint32_t>;

/* ----------------------------- EventNotification ----------------------------- */

/**
 * @struct EventNotification
 * @brief Immediate response triggered by a watchpoint event.
 *
 * Registered in the notification table. When eventId fires, the callback
 * delegate is invoked (if set), and/or the built-in log message is written
 * to the system log (if logMessage is non-empty).
 *
 * Unlike sequences (ordered, delayed), notifications are immediate and
 * non-blocking. Use for logging, counters, mode transitions, or signaling
 * other components.
 *
 * @note RT-safe: No allocation. Direct delegate call or log write.
 */
struct EventNotification {
  std::uint16_t notificationId{0}; ///< Unique ID (0 = unassigned).
  std::uint16_t eventId{0};        ///< Event to listen for.
  EventNotifyDelegate callback{};  ///< Called when event fires (optional).
  bool armed{false};               ///< Active flag.
  std::uint32_t invokeCount{0};    ///< Times this notification was invoked.

  /* ----------------------------- Built-in Log ----------------------------- */

  char logLabel[16]{};                        ///< Log label (e.g., "ACTION"). Empty = no log.
  char logMessage[48]{};                      ///< Log message (e.g., "ALTITUDE LIMIT >150m").
  LogSeverity logSeverity{LogSeverity::INFO}; ///< Severity for built-in log.

  /** @brief Check if built-in log is configured. */
  [[nodiscard]] bool hasLogMessage() const noexcept { return logMessage[0] != '\0'; }
};

/* ----------------------------- API ----------------------------- */

/**
 * @brief Check if a notification should fire for the given event.
 * @param note Notification to check.
 * @param eventId Event ID that fired.
 * @return True if notification is armed, bound to this event, and has
 *         either a callback or a log message configured.
 * @note RT-safe: O(1).
 */
[[nodiscard]] inline bool shouldNotify(const EventNotification& note,
                                       std::uint16_t eventId) noexcept {
  return note.armed && note.eventId == eventId && (note.callback || note.hasLogMessage());
}

/**
 * @brief Invoke a notification callback (delegate mode).
 * @param note Notification to invoke (modified: invokeCount).
 * @param eventId Event ID that triggered this notification.
 * @param fireCount Fire count from the source watchpoint or group.
 * @note RT-safe: O(1). Direct delegate call.
 *
 * Calls the callback delegate if set. Does NOT handle built-in log
 * (the ActionComponent handles log dispatch separately).
 */
inline void invokeNotification(EventNotification& note, std::uint16_t eventId,
                               std::uint32_t fireCount) noexcept {
  if (note.callback) {
    note.callback(eventId, fireCount);
  }
  ++note.invokeCount;
}

/**
 * @brief Dispatch an event to a notification table.
 * @param table Pointer to the notification table.
 * @param tableSize Number of entries in the table.
 * @param eventId Event ID that fired.
 * @param fireCount Fire count from the source watchpoint or group.
 * @return Number of notifications invoked.
 * @note RT-safe: O(tableSize).
 *
 * Scans the table and invokes all matching armed notifications.
 * Built-in log dispatch is handled by the ActionComponent, not here.
 */
inline std::uint8_t dispatchEvent(EventNotification* table, std::size_t tableSize,
                                  std::uint16_t eventId, std::uint32_t fireCount) noexcept {
  if (table == nullptr) {
    return 0;
  }

  std::uint8_t count = 0;
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (shouldNotify(table[i], eventId)) {
      invokeNotification(table[i], eventId, fireCount);
      ++count;
    }
  }
  return count;
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_DATA_EVENT_NOTIFICATION_HPP
