/**
 * @file Delegate_uTest.cpp
 * @brief Unit tests for concurrency::DelegateU8.
 *
 * Notes:
 *  - Tests verify construction, invocation, and bool conversion.
 *  - DelegateU8 is designed for zero-allocation task dispatch.
 */

#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using apex::concurrency::DelegateU8;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default DelegateU8 has null function and context */
TEST(DelegateU8DefaultTest, DefaultIsNull) {
  const DelegateU8 DEFAULT{};
  EXPECT_EQ(DEFAULT.fn, nullptr);
  EXPECT_EQ(DEFAULT.ctx, nullptr);
}

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

/** @test DelegateU8 stores function pointer correctly */
TEST_F(DelegateU8Test, StoresFunctionPointer) {
  const DelegateU8 DELEGATE{simpleFunction, nullptr};
  EXPECT_EQ(DELEGATE.fn, simpleFunction);
}

/** @test DelegateU8 stores context pointer correctly */
TEST_F(DelegateU8Test, StoresContextPointer) {
  int context = 123;
  const DelegateU8 DELEGATE{simpleFunction, &context};
  EXPECT_EQ(DELEGATE.ctx, &context);
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

/** @test DelegateU8 invocation is const-correct */
TEST_F(DelegateU8Test, InvocationIsConst) {
  const DelegateU8 DELEGATE{simpleFunction, nullptr};
  // Should compile - operator() is const
  const std::uint8_t RESULT = DELEGATE();
  EXPECT_EQ(RESULT, 42);
}

/* ----------------------------- Constexpr Tests ----------------------------- */

/** @test DelegateU8 default construction is constexpr */
TEST(DelegateU8ConstexprTest, DefaultConstructionConstexpr) {
  constexpr DelegateU8 DELEGATE{};
  EXPECT_FALSE(static_cast<bool>(DELEGATE));
}

/** @test DelegateU8 bool conversion is constexpr */
TEST(DelegateU8ConstexprTest, BoolConversionConstexpr) {
  constexpr DelegateU8 NULL_DELEGATE{};
  constexpr bool IS_NULL = static_cast<bool>(NULL_DELEGATE);
  EXPECT_FALSE(IS_NULL);
}

/* ----------------------------- Edge Cases ----------------------------- */

/** @test DelegateU8 with null function but valid context */
TEST_F(DelegateU8Test, NullFunctionWithContext) {
  int context = 42;
  const DelegateU8 DELEGATE{nullptr, &context};
  EXPECT_FALSE(static_cast<bool>(DELEGATE));
  EXPECT_EQ(DELEGATE(), 0); // Should return 0, not crash
}

/** @test DelegateU8 can be copied */
TEST_F(DelegateU8Test, CanBeCopied) {
  const DelegateU8 ORIGINAL{simpleFunction, nullptr};
  const DelegateU8 COPY = ORIGINAL;
  EXPECT_EQ(COPY.fn, ORIGINAL.fn);
  EXPECT_EQ(COPY.ctx, ORIGINAL.ctx);
  EXPECT_EQ(COPY(), 42);
}

/** @test DelegateU8 can be assigned */
TEST_F(DelegateU8Test, CanBeAssigned) {
  DelegateU8 delegate{};
  delegate = DelegateU8{simpleFunction, nullptr};
  EXPECT_TRUE(static_cast<bool>(delegate));
  EXPECT_EQ(delegate(), 42);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test DelegateU8 invocation returns consistent results */
TEST(DelegateU8DeterminismTest, InvocationConsistent) {
  auto fn = [](void*) noexcept -> std::uint8_t { return 100; };
  const DelegateU8 DELEGATE{fn, nullptr};

  const std::uint8_t FIRST = DELEGATE();
  const std::uint8_t SECOND = DELEGATE();
  EXPECT_EQ(FIRST, SECOND);
  EXPECT_EQ(FIRST, 100);
}
