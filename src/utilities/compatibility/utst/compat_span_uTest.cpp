/**
 * @file compat_span_uTest.cpp
 * @brief Unit tests for compat_span.hpp (C++20 std::span shims).
 */

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "src/utilities/compatibility/inc/compat_span.hpp"

using apex::compat::bytes_span;
using apex::compat::mutable_bytes_span;
using apex::compat::rospan;

// =============================================================================
// bytes_span Tests
// =============================================================================

/**
 * @test BytesSpan_DefaultConstruction
 * @brief Verifies default-constructed bytes_span is empty.
 */
TEST(CompatSpan, BytesSpan_DefaultConstruction) {
  const bytes_span span;

  EXPECT_EQ(span.data(), nullptr);
  EXPECT_EQ(span.size(), 0u);
  EXPECT_TRUE(span.empty());
}

/**
 * @test BytesSpan_PointerSizeConstruction
 * @brief Verifies construction from pointer and size.
 */
TEST(CompatSpan, BytesSpan_PointerSizeConstruction) {
  const std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const bytes_span span(data, 4);

  EXPECT_EQ(span.data(), data);
  EXPECT_EQ(span.size(), 4u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test BytesSpan_ArrayConstruction
 * @brief Verifies construction from std::array.
 */
TEST(CompatSpan, BytesSpan_ArrayConstruction) {
  const std::array<std::uint8_t, 3> arr = {0xAA, 0xBB, 0xCC};
  const bytes_span span(arr);

  EXPECT_EQ(span.data(), arr.data());
  EXPECT_EQ(span.size(), 3u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test BytesSpan_VectorConstruction
 * @brief Verifies construction from std::vector.
 */
TEST(CompatSpan, BytesSpan_VectorConstruction) {
  const std::vector<std::uint8_t> vec = {0x10, 0x20, 0x30, 0x40, 0x50};
  const bytes_span span(vec);

  EXPECT_EQ(span.data(), vec.data());
  EXPECT_EQ(span.size(), 5u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test BytesSpan_StringViewConstruction
 * @brief Verifies construction from string_view (C++17 shim only).
 *
 * Note: std::span<const uint8_t> in C++20 does not have a string_view
 * constructor. This test only applies to the C++17 shim.
 */
#if !__cpp_lib_span
TEST(CompatSpan, BytesSpan_StringViewConstruction) {
  const std::string_view sv = "Hello";
  const bytes_span span(sv);

  EXPECT_NE(span.data(), nullptr);
  EXPECT_EQ(span.size(), 5u);
  EXPECT_FALSE(span.empty());

  // First byte should be 'H'
  EXPECT_EQ(span.data()[0], static_cast<std::uint8_t>('H'));
}
#endif

/**
 * @test BytesSpan_EmptyVector
 * @brief Verifies bytes_span from empty vector is empty.
 */
TEST(CompatSpan, BytesSpan_EmptyVector) {
  const std::vector<std::uint8_t> empty;
  const bytes_span span(empty);

  EXPECT_EQ(span.size(), 0u);
  EXPECT_TRUE(span.empty());
}

/**
 * @test BytesSpan_Indexing
 * @brief Verifies bytes_span operator[] access.
 */
TEST(CompatSpan, BytesSpan_Indexing) {
  const std::array<std::uint8_t, 4> arr = {0x10, 0x20, 0x30, 0x40};
  const bytes_span span(arr);

  EXPECT_EQ(span[0], 0x10);
  EXPECT_EQ(span[1], 0x20);
  EXPECT_EQ(span[2], 0x30);
  EXPECT_EQ(span[3], 0x40);
}

/**
 * @test BytesSpan_RangeBasedFor
 * @brief Verifies bytes_span works with range-based for loops.
 */
TEST(CompatSpan, BytesSpan_RangeBasedFor) {
  const std::vector<std::uint8_t> vec = {1, 2, 3, 4, 5};
  const bytes_span span(vec);

  std::uint8_t sum = 0;
  for (std::uint8_t b : span) {
    sum += b;
  }
  EXPECT_EQ(sum, 15);
}

// =============================================================================
// mutable_bytes_span Tests
// =============================================================================

/**
 * @test MutableBytesSpan_DefaultConstruction
 * @brief Verifies default-constructed mutable_bytes_span is empty.
 */
TEST(CompatSpan, MutableBytesSpan_DefaultConstruction) {
  const mutable_bytes_span span;

  EXPECT_EQ(span.data(), nullptr);
  EXPECT_EQ(span.size(), 0u);
  EXPECT_TRUE(span.empty());
}

/**
 * @test MutableBytesSpan_PointerSizeConstruction
 * @brief Verifies construction from pointer and size.
 */
TEST(CompatSpan, MutableBytesSpan_PointerSizeConstruction) {
  std::uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
  const mutable_bytes_span span(data, 4);

  EXPECT_EQ(span.data(), data);
  EXPECT_EQ(span.size(), 4u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test MutableBytesSpan_ArrayConstruction
 * @brief Verifies construction from std::array.
 */
TEST(CompatSpan, MutableBytesSpan_ArrayConstruction) {
  std::array<std::uint8_t, 3> arr = {0xAA, 0xBB, 0xCC};
  const mutable_bytes_span span(arr);

  EXPECT_EQ(span.data(), arr.data());
  EXPECT_EQ(span.size(), 3u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test MutableBytesSpan_VectorConstruction
 * @brief Verifies construction from std::vector.
 */
TEST(CompatSpan, MutableBytesSpan_VectorConstruction) {
  std::vector<std::uint8_t> vec = {0x10, 0x20, 0x30, 0x40, 0x50};
  const mutable_bytes_span span(vec);

  EXPECT_EQ(span.data(), vec.data());
  EXPECT_EQ(span.size(), 5u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test MutableBytesSpan_Modification
 * @brief Verifies mutable_bytes_span allows modification of underlying data.
 */
TEST(CompatSpan, MutableBytesSpan_Modification) {
  std::array<std::uint8_t, 4> arr = {0x00, 0x00, 0x00, 0x00};
  mutable_bytes_span span(arr);

  span[0] = 0xDE;
  span[1] = 0xAD;
  span[2] = 0xBE;
  span[3] = 0xEF;

  EXPECT_EQ(arr[0], 0xDE);
  EXPECT_EQ(arr[1], 0xAD);
  EXPECT_EQ(arr[2], 0xBE);
  EXPECT_EQ(arr[3], 0xEF);
}

/**
 * @test MutableBytesSpan_RangeBasedFor
 * @brief Verifies mutable_bytes_span works with range-based for loops.
 */
TEST(CompatSpan, MutableBytesSpan_RangeBasedFor) {
  std::vector<std::uint8_t> vec = {1, 2, 3, 4, 5};
  mutable_bytes_span span(vec);

  // Modify via range-based for
  for (std::uint8_t& b : span) {
    b *= 2;
  }

  EXPECT_EQ(vec[0], 2);
  EXPECT_EQ(vec[1], 4);
  EXPECT_EQ(vec[2], 6);
  EXPECT_EQ(vec[3], 8);
  EXPECT_EQ(vec[4], 10);
}

/**
 * @test MutableBytesSpan_EmptyVector
 * @brief Verifies mutable_bytes_span from empty vector is empty.
 */
TEST(CompatSpan, MutableBytesSpan_EmptyVector) {
  std::vector<std::uint8_t> empty;
  const mutable_bytes_span span(empty);

  EXPECT_EQ(span.size(), 0u);
  EXPECT_TRUE(span.empty());
}

// =============================================================================
// rospan<T> Tests
// =============================================================================

/**
 * @test Rospan_DefaultConstruction
 * @brief Verifies default-constructed rospan is empty.
 */
TEST(CompatSpan, Rospan_DefaultConstruction) {
  const rospan<int> span;

  EXPECT_EQ(span.data(), nullptr);
  EXPECT_EQ(span.size(), 0u);
  EXPECT_TRUE(span.empty());
}

/**
 * @test Rospan_PointerSizeConstruction
 * @brief Verifies rospan construction from pointer and size.
 */
TEST(CompatSpan, Rospan_PointerSizeConstruction) {
  const double data[] = {1.0, 2.0, 3.0};
  const rospan<double> span(data, 3);

  EXPECT_EQ(span.data(), data);
  EXPECT_EQ(span.size(), 3u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test Rospan_ArrayConstruction
 * @brief Verifies rospan construction from std::array.
 */
TEST(CompatSpan, Rospan_ArrayConstruction) {
  const std::array<int, 4> arr = {10, 20, 30, 40};
  const rospan<int> span(arr);

  EXPECT_EQ(span.data(), arr.data());
  EXPECT_EQ(span.size(), 4u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test Rospan_VectorConstruction
 * @brief Verifies rospan construction from std::vector.
 */
TEST(CompatSpan, Rospan_VectorConstruction) {
  const std::vector<float> vec = {1.5f, 2.5f, 3.5f};
  const rospan<float> span(vec);

  EXPECT_EQ(span.data(), vec.data());
  EXPECT_EQ(span.size(), 3u);
  EXPECT_FALSE(span.empty());
}

/**
 * @test Rospan_EmptyArray
 * @brief Verifies rospan from empty array is empty.
 */
TEST(CompatSpan, Rospan_EmptyArray) {
  const std::array<int, 0> empty{};
  const rospan<int> span(empty);

  EXPECT_EQ(span.size(), 0u);
  EXPECT_TRUE(span.empty());
}

/**
 * @test Rospan_DifferentTypes
 * @brief Verifies rospan works with various element types.
 */
TEST(CompatSpan, Rospan_DifferentTypes) {
  // uint64_t
  const std::vector<std::uint64_t> u64vec = {1, 2, 3};
  const rospan<std::uint64_t> u64span(u64vec);
  EXPECT_EQ(u64span.size(), 3u);

  // Custom struct
  struct Point {
    int x;
    int y;
  };
  const std::array<Point, 2> points = {{{1, 2}, {3, 4}}};
  const rospan<Point> pointSpan(points);
  EXPECT_EQ(pointSpan.size(), 2u);
  EXPECT_EQ(pointSpan.data()[0].x, 1);
  EXPECT_EQ(pointSpan.data()[1].y, 4);
}

/**
 * @test Rospan_Constexpr
 * @brief Verifies rospan accessors are constexpr-compatible.
 */
TEST(CompatSpan, Rospan_Constexpr) {
  constexpr rospan<int> emptySpan;
  constexpr bool isEmpty = emptySpan.empty();
  constexpr std::size_t sz = emptySpan.size();

  EXPECT_TRUE(isEmpty);
  EXPECT_EQ(sz, 0u);
}
