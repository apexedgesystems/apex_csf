#ifndef APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_MOVE_TRACKING_HPP
#define APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_MOVE_TRACKING_HPP
/**
 * @file MoveTracking.hpp
 * @brief An element type that counts its copies and moves, and the check the
 *        three queues share: an rvalue is moved in, an lvalue is copied once,
 *        and a pop moves the element out.
 */

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace apex {
namespace concurrency {
namespace test {

/// An int that counts every copy and every move made of any Tracked, and marks a moved-from
/// source. The counts are per process: a case resets them before the operation it measures.
struct Tracked {
  static constexpr int MOVED_FROM = -1;

  static int& copies() noexcept {
    static int count = 0;
    return count;
  }
  static int& moves() noexcept {
    static int count = 0;
    return count;
  }
  static void resetCounts() noexcept {
    copies() = 0;
    moves() = 0;
  }

  Tracked() = default;
  explicit Tracked(int v) noexcept : value(v) {}
  Tracked(const Tracked& other) noexcept : value(other.value) { ++copies(); }
  Tracked(Tracked&& other) noexcept : value(other.value) {
    other.value = MOVED_FROM;
    ++moves();
  }
  Tracked& operator=(const Tracked& other) noexcept {
    value = other.value;
    ++copies();
    return *this;
  }
  Tracked& operator=(Tracked&& other) noexcept {
    value = other.value;
    other.value = MOVED_FROM;
    ++moves();
    return *this;
  }

  int value{0};
};

/// Check a queue template's handling of value categories, single-threaded:
///  - tryPush of an rvalue copies nothing and leaves the source moved-from;
///  - tryPush of an lvalue copies exactly once and leaves the source intact;
///  - tryPop copies nothing: the element is moved out;
///  - a move-only element type goes in and comes out.
template <template <class> class Queue> void checkQueueValueCategories() {
  {
    Queue<Tracked> queue(4);
    Tracked source(7);
    Tracked::resetCounts();
    ASSERT_TRUE(queue.tryPush(std::move(source)));
    EXPECT_EQ(Tracked::copies(), 0) << "tryPush(T&&) copied its argument";
    EXPECT_GE(Tracked::moves(), 1);
    EXPECT_EQ(source.value, Tracked::MOVED_FROM); // NOLINT(bugprone-use-after-move)

    Tracked out;
    Tracked::resetCounts();
    ASSERT_TRUE(queue.tryPop(out));
    EXPECT_EQ(Tracked::copies(), 0) << "tryPop copied the element out";
    EXPECT_GE(Tracked::moves(), 1);
    EXPECT_EQ(out.value, 7);
  }
  {
    Queue<Tracked> queue(4);
    const Tracked SOURCE(9);
    Tracked::resetCounts();
    ASSERT_TRUE(queue.tryPush(SOURCE));
    EXPECT_EQ(Tracked::copies(), 1) << "tryPush(const T&) made other than one copy";
    EXPECT_EQ(Tracked::moves(), 0);
    EXPECT_EQ(SOURCE.value, 9);

    Tracked out;
    ASSERT_TRUE(queue.tryPop(out));
    EXPECT_EQ(out.value, 9);
  }
  {
    Queue<std::unique_ptr<int>> queue(4);
    ASSERT_TRUE(queue.tryPush(std::make_unique<int>(11)));
    std::unique_ptr<int> out;
    ASSERT_TRUE(queue.tryPop(out));
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(*out, 11);
  }
}

} // namespace test
} // namespace concurrency
} // namespace apex

#endif // APEX_UTILITIES_CONCURRENCY_UTST_SUPPORT_MOVE_TRACKING_HPP
