/**
 * @file Delegate_uTest.cpp
 * @brief Unit tests for concurrency::DelegateU8 and the templated Delegate.
 *
 * Notes:
 *  - Tests verify construction, invocation, and bool conversion.
 *  - Both are designed for zero-allocation dispatch.
 */

#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

using apex::concurrency::DelegateU8;

/* ----------------------------- DelegateU8 Compile-Time Contract ----------------------------- */

namespace {

constexpr std::uint8_t answer(void*) noexcept { return 42; }

} // namespace

// A function pointer and a context pointer, nothing else, copied bit for bit: what lets a
// delegate sit in a queue cell or a task table without a constructor running.
static_assert(sizeof(DelegateU8) == 2 * sizeof(void*));
static_assert(std::is_trivially_copyable_v<DelegateU8>);
static_assert(std::is_aggregate_v<DelegateU8>);

// Usable in constant expressions: a default delegate is null, converts to false and returns
// zero when called, and a delegate with a function calls it.
static_assert(DelegateU8{}.fn == nullptr && DelegateU8{}.ctx == nullptr);
static_assert(!DelegateU8{});
static_assert(DelegateU8{}() == 0);
static_assert(DelegateU8{&answer, nullptr});
static_assert(DelegateU8{&answer, nullptr}() == 42);

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default DelegateU8 converts to false */
TEST(DelegateU8DefaultTest, DefaultIsFalse) {
  const DelegateU8 DEFAULT{};
  EXPECT_FALSE(static_cast<bool>(DEFAULT));
}

/** @test Default DelegateU8 invocation returns 0 */
TEST(DelegateU8DefaultTest, DefaultInvocationReturnsZero) {
  const DelegateU8 DEFAULT{};
  EXPECT_EQ(DEFAULT(), 0);
}

/* ----------------------------- Construction Tests ----------------------------- */

class DelegateU8Test : public ::testing::Test {
protected:
  static std::uint8_t simpleFunction(void*) noexcept { return 42; }

  static std::uint8_t contextFunction(void* ctx) noexcept {
    return *static_cast<std::uint8_t*>(ctx);
  }
};

/** @test DelegateU8 with function converts to true */
TEST_F(DelegateU8Test, WithFunctionIsTrue) {
  const DelegateU8 DELEGATE{simpleFunction, nullptr};
  EXPECT_TRUE(static_cast<bool>(DELEGATE));
}

/* ----------------------------- Invocation Tests ----------------------------- */

/** @test DelegateU8 invokes function and returns result */
TEST_F(DelegateU8Test, InvokeReturnsResult) {
  const DelegateU8 DELEGATE{simpleFunction, nullptr};
  EXPECT_EQ(DELEGATE(), 42);
}

/** @test DelegateU8 passes context to function */
TEST_F(DelegateU8Test, PassesContextToFunction) {
  std::uint8_t value = 99;
  const DelegateU8 DELEGATE{contextFunction, &value};
  EXPECT_EQ(DELEGATE(), 99);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test DelegateU8 with null function but valid context */
TEST_F(DelegateU8Test, NullFunctionWithContext) {
  int context = 42;
  const DelegateU8 DELEGATE{nullptr, &context};
  EXPECT_FALSE(static_cast<bool>(DELEGATE));
  EXPECT_EQ(DELEGATE(), 0); // Should return 0, not crash
}

/* ----------------------------- Delegate<Ret, Args...> Tests ----------------------------- */

namespace {

using apex::concurrency::Delegate;
using apex::concurrency::makeDelegate;

static_assert(std::is_trivially_copyable_v<Delegate<int, int>>);
static_assert(std::is_trivially_copyable_v<Delegate<void, int>>);

/// A callee that records what reached it.
struct Callee {
  int calls{0};
  int lastArgument{0};
};

int addToArgument(void* ctx, int argument) noexcept {
  auto* callee = static_cast<Callee*>(ctx);
  ++callee->calls;
  callee->lastArgument = argument;
  return argument + 100;
}

void recordArgument(void* ctx, int argument) noexcept {
  auto* callee = static_cast<Callee*>(ctx);
  ++callee->calls;
  callee->lastArgument = argument;
}

} // namespace

/** @test A Delegate hands its context and its arguments to the function and returns the
 * function's result. */
TEST(DelegateTest, PassesContextAndArgumentsAndReturnsTheResult) {
  Callee callee;
  const Delegate<int, int> DELEGATE{&addToArgument, &callee};

  EXPECT_TRUE(static_cast<bool>(DELEGATE));
  EXPECT_EQ(DELEGATE(7), 107);
  EXPECT_EQ(callee.calls, 1);
  EXPECT_EQ(callee.lastArgument, 7);
}

/** @test A Delegate with no function converts to false and returns a value-initialized result
 * without calling anything. */
TEST(DelegateTest, NullDelegateReturnsAValueInitializedResult) {
  const Delegate<int, int> EMPTY{};

  EXPECT_FALSE(static_cast<bool>(EMPTY));
  EXPECT_EQ(EMPTY(7), 0);
}

/** @test A void Delegate calls its function with the context and arguments, and a void Delegate
 * with no function does nothing. */
TEST(DelegateTest, VoidDelegateCallsItsFunctionOrDoesNothing) {
  Callee callee;
  const Delegate<void, int> DELEGATE{&recordArgument, &callee};
  DELEGATE(9);
  EXPECT_EQ(callee.calls, 1);
  EXPECT_EQ(callee.lastArgument, 9);

  const Delegate<void, int> EMPTY{};
  EXPECT_FALSE(static_cast<bool>(EMPTY));
  EMPTY(11);
  EXPECT_EQ(callee.calls, 1);
}

/** @test makeDelegate deduces the return and argument types from the function, for both the
 * value-returning and the void form. */
TEST(DelegateTest, MakeDelegateDeducesTheSignature) {
  Callee callee;
  const auto RETURNING = makeDelegate(&addToArgument, &callee);
  const auto VOID = makeDelegate(&recordArgument, &callee);
  static_assert(std::is_same_v<decltype(RETURNING), const Delegate<int, int>>);
  static_assert(std::is_same_v<decltype(VOID), const Delegate<void, int>>);

  EXPECT_EQ(RETURNING(1), 101);
  VOID(2);
  EXPECT_EQ(callee.calls, 2);
  EXPECT_EQ(callee.lastArgument, 2);
}
