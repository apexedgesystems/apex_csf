#ifndef APEX_SYSTEM_CORE_SCHEDULER_TASKCTXPOOL_HPP
#define APEX_SYSTEM_CORE_SCHEDULER_TASKCTXPOOL_HPP
/**
 * @file TaskCtxPool.hpp
 * @brief Lock-free single-list pool for per-task execution contexts.
 */

#include <cstddef>
#include <cstdio>

#include <atomic>
#include <new>

namespace system_core {

namespace schedulable {
class SchedulableTask;
} // namespace schedulable

namespace scheduler {

class SchedulerMultiThread;
struct TaskEntry;

// Import task types from schedulable namespace
using schedulable::SchedulableTask;

/**
 * @brief Execution context passed to worker threads.
 */
struct TaskCtx {
  SchedulerMultiThread* self{nullptr};
  SchedulableTask* task{nullptr};
  TaskEntry* entry{nullptr}; ///< TaskEntry for sequencing info.
  TaskCtx* next{nullptr};
  std::uint16_t tick{0};
  std::uint8_t poolId{0}; ///< Pool ID for multi-pool context release.
};

/**
 * @class TaskCtxPool
 * @brief Intrusive LIFO pool for TaskCtx (no per-tick heap churn).
 *
 * Thread-safe push/pop via atomic head. Fallback allocation is optional.
 */
class TaskCtxPool {
public:
  TaskCtxPool() noexcept
      : head_(nullptr), freeCount_(0), allocatedCount_(0), allowFallbackAlloc_(true) {}

  explicit TaskCtxPool(std::size_t reserve) noexcept
      : head_(nullptr), freeCount_(0), allocatedCount_(0), allowFallbackAlloc_(true) {
    preallocate(reserve);
  }

  TaskCtxPool(const TaskCtxPool&) = delete;
  TaskCtxPool& operator=(const TaskCtxPool&) = delete;
  TaskCtxPool(TaskCtxPool&&) = delete;
  TaskCtxPool& operator=(TaskCtxPool&&) = delete;

  ~TaskCtxPool() noexcept {
#ifndef NDEBUG
    const std::size_t ALLOC = allocatedCount_.load(std::memory_order_relaxed);
    const std::size_t FREE = freeCount_.load(std::memory_order_relaxed);
    if (ALLOC != FREE) {
      std::fprintf(stderr,
                   "[TaskCtxPool] DEBUG: destroy with outstanding contexts "
                   "(allocated=%zu free=%zu outstanding=%zu)\n",
                   ALLOC, FREE, (ALLOC > FREE ? ALLOC - FREE : 0u));
    }
#endif
    TaskCtx* node = head_.load(std::memory_order_relaxed);
    while (node) {
      TaskCtx* next = node->next;
      delete node;
      node = next;
    }
  }

  void preallocate(std::size_t n) noexcept {
    for (std::size_t i = 0; i < n; ++i) {
      TaskCtx* node = new (std::nothrow) TaskCtx{};
      if (!node)
        break;
      TaskCtx* oldHead = head_.load(std::memory_order_relaxed);
      do {
        node->next = oldHead;
      } while (!head_.compare_exchange_weak(oldHead, node, std::memory_order_release,
                                            std::memory_order_relaxed));
#ifndef NDEBUG
      freeCount_.fetch_add(1, std::memory_order_relaxed);
      allocatedCount_.fetch_add(1, std::memory_order_relaxed);
#endif
    }
  }

  void setFallbackAlloc(bool enable) noexcept { allowFallbackAlloc_ = enable; }

  [[nodiscard]] TaskCtx* acquire(SchedulerMultiThread* self, SchedulableTask* task) noexcept {
    TaskCtx* ctx = pop();
    if (!ctx && allowFallbackAlloc_) {
      ctx = new (std::nothrow) TaskCtx{};
#ifndef NDEBUG
      if (ctx)
        allocatedCount_.fetch_add(1, std::memory_order_relaxed);
#endif
    }
    if (ctx) {
      ctx->self = self;
      ctx->task = task;
      ctx->next = nullptr;
    }
    return ctx;
  }

  [[nodiscard]] TaskCtx* acquireNoAlloc(SchedulerMultiThread* self,
                                        SchedulableTask* task) noexcept {
    TaskCtx* ctx = pop();
    if (ctx) {
      ctx->self = self;
      ctx->task = task;
      ctx->next = nullptr;
    }
    return ctx;
  }

  void release(TaskCtx* ctx) noexcept {
    if (!ctx)
      return;
    ctx->self = nullptr;
    ctx->task = nullptr;

    TaskCtx* oldHead = head_.load(std::memory_order_relaxed);
    do {
      ctx->next = oldHead;
    } while (!head_.compare_exchange_weak(oldHead, ctx, std::memory_order_release,
                                          std::memory_order_relaxed));
#ifndef NDEBUG
    freeCount_.fetch_add(1, std::memory_order_relaxed);
#endif
  }

  [[nodiscard]] std::size_t freeCount() const noexcept {
    return freeCount_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::size_t allocatedCount() const noexcept {
    return allocatedCount_.load(std::memory_order_relaxed);
  }

private:
  [[nodiscard]] TaskCtx* pop() noexcept {
    TaskCtx* node = head_.load(std::memory_order_acquire);
    while (node && !head_.compare_exchange_weak(node, node->next, std::memory_order_acq_rel,
                                                std::memory_order_relaxed)) {
    }
    if (node) {
#ifndef NDEBUG
      freeCount_.fetch_sub(1, std::memory_order_relaxed);
#endif
      node->next = nullptr;
    }
    return node;
  }

private:
  std::atomic<TaskCtx*> head_;
  std::atomic<std::size_t> freeCount_;
  std::atomic<std::size_t> allocatedCount_;
  bool allowFallbackAlloc_;
};

} // namespace scheduler
} // namespace system_core

#endif // APEX_SYSTEM_CORE_SCHEDULER_TASKCTXPOOL_HPP
