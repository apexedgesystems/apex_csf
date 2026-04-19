#ifndef APEX_SYSTEM_CORE_ACTION_RESOURCE_CATALOG_HPP
#define APEX_SYSTEM_CORE_ACTION_RESOURCE_CATALOG_HPP
/**
 * @file ResourceCatalog.hpp
 * @brief Catalogs for watchpoints, groups, and notifications.
 *
 * Each catalog stores lightweight definitions that can be activated into
 * the corresponding fixed-size active table at runtime. This decouples
 * the number of defined resources (hundreds) from the number evaluated
 * each tick (bounded by ActionEngineConfig).
 *
 * Pattern (same for all three types):
 *   - Boot: TPRM populates catalog. Entries with activeOnBoot are
 *     copied into the active table.
 *   - Runtime: ACTIVATE/DEACTIVATE commands move entries between
 *     catalog and active table by ID.
 *   - Lookup: O(log N) binary search on sorted ID array.
 *
 * RT-safe: Lookups are RT-safe. Mutation (add, activate, deactivate)
 * is NOT RT-safe (re-sort, table scan).
 */

#include "src/system/core/components/action/apex/inc/ActionEngineConfig.hpp"
#include "src/system/core/components/action/apex/inc/DataWatchpoint.hpp"
#include "src/system/core/components/action/apex/inc/EventNotification.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace system_core {
namespace data {

/* ----------------------------- WatchpointDef ----------------------------- */

/**
 * @struct WatchpointDef
 * @brief Static definition of a watchpoint (catalog entry).
 *
 * Contains all configuration needed to populate a DataWatchpoint in the
 * active table. No runtime state (lastResult, fireCount, etc.).
 */
struct WatchpointDef {
  std::uint16_t watchpointId{0};                          ///< Unique ID.
  DataTarget target{};                                    ///< What to watch.
  WatchPredicate predicate{WatchPredicate::EQ};           ///< When to fire.
  WatchDataType dataType{WatchDataType::RAW};             ///< How to interpret bytes.
  std::uint16_t eventId{0};                               ///< Event ID fired on trigger.
  std::array<std::uint8_t, WATCH_VALUE_SIZE> threshold{}; ///< Comparison value.
  std::uint32_t minFireCount{0};                          ///< Debounce threshold.
  std::uint16_t cadenceTicks{0};                          ///< Evaluate every N ticks.
  bool activeOnBoot{false};                               ///< Auto-activate at startup.
};

/* ----------------------------- GroupDef ----------------------------- */

/**
 * @struct GroupDef
 * @brief Static definition of a watchpoint group (catalog entry).
 */
struct GroupDef {
  std::uint16_t groupId{0};                                    ///< Unique ID.
  std::array<std::uint16_t, WATCHPOINT_GROUP_MAX_REFS> refs{}; ///< Watchpoint IDs.
  std::uint8_t count{0};                                       ///< Reference count.
  GroupLogic logic{GroupLogic::AND};                           ///< Combination logic.
  std::uint16_t eventId{0};                                    ///< Event ID fired.
  bool activeOnBoot{false};                                    ///< Auto-activate.
};

/* ----------------------------- NotificationDef ----------------------------- */

/**
 * @struct NotificationDef
 * @brief Static definition of an event notification (catalog entry).
 */
struct NotificationDef {
  std::uint16_t notificationId{0}; ///< Unique ID.
  std::uint16_t eventId{0};        ///< Event to listen for.
  LogSeverity severity{LogSeverity::INFO};
  char logLabel[16]{};      ///< Log label (empty = no log).
  char logMessage[48]{};    ///< Log message (empty = no log).
  bool activeOnBoot{false}; ///< Auto-activate.
};

/* ----------------------------- ResourceCatalog ----------------------------- */

/**
 * @class ResourceCatalog
 * @brief Generic sorted catalog with O(log N) lookup by ID.
 *
 * @tparam Entry   Definition struct (must have a uint16_t ID as first member).
 * @tparam MaxSize Maximum catalog capacity.
 * @tparam IdOf    Accessor: given an Entry, returns its uint16_t ID.
 *
 * @note RT-safe for lookups. NOT RT-safe for mutation.
 */
template <typename Entry, std::size_t MaxSize, std::uint16_t (*IdOf)(const Entry&)>
class ResourceCatalog {
public:
  /** @brief Add an entry. Returns false if full or duplicate.
   *  @note NOT RT-safe: may re-sort. */
  bool add(const Entry& entry) noexcept {
    if (count_ >= MaxSize) {
      return false;
    }
    if (findById(IdOf(entry)) != nullptr) {
      return false;
    }
    entries_[count_] = entry;
    ++count_;
    sortById();
    return true;
  }

  /** @brief Find by ID (const). O(log N). RT-safe.
   *  @note RT-safe: O(log N) binary search. */
  [[nodiscard]] const Entry* findById(std::uint16_t id) const noexcept {
    if (count_ == 0) {
      return nullptr;
    }
    std::size_t lo = 0;
    std::size_t hi = count_;
    while (lo < hi) {
      const std::size_t MID = lo + (hi - lo) / 2;
      const std::uint16_t MID_ID = IdOf(entries_[MID]);
      if (MID_ID == id) {
        return &entries_[MID];
      }
      if (MID_ID < id) {
        lo = MID + 1;
      } else {
        hi = MID;
      }
    }
    return nullptr;
  }

  /** @brief Find by ID (mutable). O(log N).
   *  @note NOT RT-safe if used for mutation during execution. */
  [[nodiscard]] Entry* findByIdMut(std::uint16_t id) noexcept {
    return const_cast<Entry*>(static_cast<const ResourceCatalog*>(this)->findById(id));
  }

  /** @brief Iterate all entries.
   *  @note RT-safe if fn is RT-safe. */
  template <typename F> void forEach(F&& fn) const noexcept {
    for (std::size_t i = 0; i < count_; ++i) {
      fn(entries_[i]);
    }
  }

  /** @brief Total entries.
   *  @note RT-safe: O(1). */
  [[nodiscard]] std::size_t size() const noexcept { return count_; }

  /** @brief Clear all entries.
   *  @note NOT RT-safe. */
  void clear() noexcept { count_ = 0; }

private:
  void sortById() noexcept {
    std::sort(entries_, entries_ + count_,
              [](const Entry& a, const Entry& b) { return IdOf(a) < IdOf(b); });
  }

  Entry entries_[MaxSize]{};
  std::size_t count_{0};
};

/* ----------------------------- ID Accessors ----------------------------- */

inline std::uint16_t watchpointDefId(const WatchpointDef& e) noexcept { return e.watchpointId; }
inline std::uint16_t groupDefId(const GroupDef& e) noexcept { return e.groupId; }
inline std::uint16_t notificationDefId(const NotificationDef& e) noexcept {
  return e.notificationId;
}

/* ----------------------------- Catalog Types ----------------------------- */

using WatchpointCatalog = ResourceCatalog<WatchpointDef, Config::WATCHPOINT_COUNT, watchpointDefId>;

using GroupCatalog = ResourceCatalog<GroupDef, Config::GROUP_COUNT, groupDefId>;

using NotificationCatalog =
    ResourceCatalog<NotificationDef, Config::NOTIFICATION_COUNT, notificationDefId>;

/* ----------------------------- Activation Helpers ----------------------------- */

/**
 * @brief Activate a watchpoint from catalog into the active table.
 * @param def Catalog definition.
 * @param table Active watchpoint table.
 * @param tableSize Table capacity.
 * @return Slot index where activated, or 0xFF if table full or already active.
 * @note NOT RT-safe: O(tableSize) scan.
 */
inline std::uint8_t activateWatchpoint(const WatchpointDef& def, DataWatchpoint* table,
                                       std::size_t tableSize) noexcept {
  // Check if already active
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (table[i].armed && table[i].watchpointId == def.watchpointId) {
      return 0xFF; // Already active
    }
  }

  // Find a free slot
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (!table[i].armed) {
      table[i] = DataWatchpoint{};
      table[i].watchpointId = def.watchpointId;
      table[i].target = def.target;
      table[i].predicate = def.predicate;
      table[i].dataType = def.dataType;
      table[i].eventId = def.eventId;
      table[i].threshold = def.threshold;
      table[i].minFireCount = def.minFireCount;
      table[i].cadenceTicks = def.cadenceTicks;
      table[i].armed = true;
      return static_cast<std::uint8_t>(i);
    }
  }
  return 0xFF; // Table full
}

/**
 * @brief Deactivate a watchpoint by ID from the active table.
 * @param watchpointId ID to deactivate.
 * @param table Active watchpoint table.
 * @param tableSize Table capacity.
 * @return true if found and deactivated.
 * @note NOT RT-safe: O(tableSize) scan.
 */
inline bool deactivateWatchpoint(std::uint16_t watchpointId, DataWatchpoint* table,
                                 std::size_t tableSize) noexcept {
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (table[i].watchpointId == watchpointId) {
      table[i] = DataWatchpoint{};
      return true;
    }
  }
  return false;
}

/**
 * @brief Activate a group from catalog into the active table.
 * @return Slot index or 0xFF.
 * @note NOT RT-safe: O(tableSize) scan.
 */
inline std::uint8_t activateGroup(const GroupDef& def, WatchpointGroup* table,
                                  std::size_t tableSize) noexcept {
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (table[i].armed && table[i].groupId == def.groupId) {
      return 0xFF;
    }
  }
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (!table[i].armed) {
      table[i] = WatchpointGroup{};
      table[i].groupId = def.groupId;
      table[i].refs = def.refs;
      table[i].count = def.count;
      table[i].logic = def.logic;
      table[i].eventId = def.eventId;
      table[i].armed = true;
      return static_cast<std::uint8_t>(i);
    }
  }
  return 0xFF;
}

/**
 * @brief Deactivate a group by ID from the active table.
 * @note NOT RT-safe: O(tableSize) scan.
 */
inline bool deactivateGroup(std::uint16_t groupId, WatchpointGroup* table,
                            std::size_t tableSize) noexcept {
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (table[i].groupId == groupId) {
      table[i] = WatchpointGroup{};
      return true;
    }
  }
  return false;
}

/**
 * @brief Activate a notification from catalog into the active table.
 * @return Slot index or 0xFF.
 * @note NOT RT-safe: O(tableSize) scan.
 */
inline std::uint8_t activateNotification(const NotificationDef& def, EventNotification* table,
                                         std::size_t tableSize) noexcept {
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (table[i].armed && table[i].notificationId == def.notificationId) {
      return 0xFF;
    }
  }
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (!table[i].armed) {
      table[i] = EventNotification{};
      table[i].notificationId = def.notificationId;
      table[i].eventId = def.eventId;
      table[i].logSeverity = def.severity;
      std::memcpy(table[i].logLabel, def.logLabel, sizeof(table[i].logLabel));
      std::memcpy(table[i].logMessage, def.logMessage, sizeof(table[i].logMessage));
      table[i].armed = true;
      return static_cast<std::uint8_t>(i);
    }
  }
  return 0xFF;
}

/**
 * @brief Deactivate a notification by ID from the active table.
 * @note NOT RT-safe: O(tableSize) scan.
 */
inline bool deactivateNotification(std::uint16_t notificationId, EventNotification* table,
                                   std::size_t tableSize) noexcept {
  for (std::size_t i = 0; i < tableSize; ++i) {
    if (table[i].notificationId == notificationId) {
      table[i] = EventNotification{};
      return true;
    }
  }
  return false;
}

} // namespace data
} // namespace system_core

#endif // APEX_SYSTEM_CORE_ACTION_RESOURCE_CATALOG_HPP
