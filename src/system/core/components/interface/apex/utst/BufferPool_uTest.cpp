/**
 * @file BufferPool_uTest.cpp
 * @brief Unit tests for BufferPool RT-safe buffer management.
 */

#include "src/system/core/components/interface/apex/inc/BufferPool.hpp"
#include "src/system/core/components/interface/apex/inc/MessageBuffer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using system_core::interface::BufferPool;
using system_core::interface::MessageBuffer;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates pool with default parameters. */
TEST(BufferPool, DefaultConstruction) {
  BufferPool pool;

  EXPECT_EQ(pool.size(), system_core::interface::DEFAULT_BUFFER_POOL_SIZE);
  EXPECT_EQ(pool.available(), system_core::interface::DEFAULT_BUFFER_POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);
}

/** @test Custom construction creates pool with specified parameters. */
TEST(BufferPool, CustomConstruction) {
  constexpr std::size_t POOL_SIZE = 64;
  constexpr std::size_t BUFFER_CAPACITY = 2048;

  BufferPool pool(POOL_SIZE, BUFFER_CAPACITY);

  EXPECT_EQ(pool.size(), POOL_SIZE);
  EXPECT_EQ(pool.available(), POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);
}

/* ----------------------------- Acquire Tests ----------------------------- */

/** @test Acquire returns valid buffer for valid size. */
TEST(BufferPool, AcquireValidSize) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(512);

  ASSERT_NE(buf, nullptr);
  EXPECT_TRUE(buf->isValid());
  EXPECT_GE(buf->capacity, 512);
  EXPECT_EQ(buf->length, 0);

  pool.release(buf);
}

/** @test Acquire returns nullptr if requested size exceeds buffer capacity. */
TEST(BufferPool, AcquireOversizeReturnsNull) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(2048);

  EXPECT_EQ(buf, nullptr);
}

/** @test Acquire clears metadata from previous use. */
TEST(BufferPool, AcquireClearsMetadata) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  buf->length = 50;
  buf->fullUid = 0x12345678;
  buf->opcode = 0xABCD;
  buf->sequence = 0x1234;
  buf->internalOrigin = true;

  pool.release(buf);

  MessageBuffer* buf2 = pool.acquire(100);
  ASSERT_NE(buf2, nullptr);

  EXPECT_EQ(buf2->length, 0);
  EXPECT_EQ(buf2->fullUid, 0);
  EXPECT_EQ(buf2->opcode, 0);
  EXPECT_EQ(buf2->sequence, 0);
  EXPECT_FALSE(buf2->internalOrigin);

  pool.release(buf2);
}

/** @test Acquire exhausts pool and returns nullptr when all buffers acquired. */
TEST(BufferPool, AcquireExhaustion) {
  constexpr std::size_t POOL_SIZE = 8;
  BufferPool pool(POOL_SIZE, 1024);

  std::vector<MessageBuffer*> buffers;

  for (std::size_t i = 0; i < POOL_SIZE; ++i) {
    MessageBuffer* buf = pool.acquire(100);
    ASSERT_NE(buf, nullptr) << "Failed to acquire buffer " << i;
    buffers.push_back(buf);
  }

  EXPECT_EQ(pool.available(), 0);
  EXPECT_EQ(pool.acquired(), POOL_SIZE);

  MessageBuffer* exhausted = pool.acquire(100);
  EXPECT_EQ(exhausted, nullptr);

  for (MessageBuffer* buf : buffers) {
    pool.release(buf);
  }
}

/* ----------------------------- Release Tests ----------------------------- */

/** @test Release returns buffer to pool. */
TEST(BufferPool, ReleaseReturnsToPool) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);
  EXPECT_EQ(pool.available(), 15);

  pool.release(buf);
  EXPECT_EQ(pool.available(), 16);
  EXPECT_EQ(pool.acquired(), 0);
}

/** @test Release nullptr is safe (defensive). */
TEST(BufferPool, ReleaseNullIsSafe) {
  BufferPool pool(16, 1024);

  EXPECT_NO_THROW(pool.release(nullptr));
}

/** @test Released buffer can be reacquired. */
TEST(BufferPool, ReleaseAndReacquireCycle) {
  BufferPool pool(1, 1024);

  MessageBuffer* buf1 = pool.acquire(100);
  ASSERT_NE(buf1, nullptr);

  const std::uint8_t* originalDataPtr = buf1->data;

  pool.release(buf1);

  MessageBuffer* buf2 = pool.acquire(100);
  ASSERT_NE(buf2, nullptr);

  EXPECT_EQ(buf2->data, originalDataPtr);

  pool.release(buf2);
}

/* ----------------------------- Buffer Validity Tests ----------------------------- */

/** @test Acquired buffer has valid data pointer and capacity. */
TEST(BufferPool, AcquiredBufferIsValid) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(512);
  ASSERT_NE(buf, nullptr);

  EXPECT_NE(buf->data, nullptr);
  EXPECT_GE(buf->capacity, 512);
  EXPECT_TRUE(buf->isValid());
  EXPECT_TRUE(buf->canFit(512));
  EXPECT_TRUE(buf->canFit(256));
  EXPECT_FALSE(buf->canFit(2048));

  pool.release(buf);
}

/** @test Buffer can hold actual data. */
TEST(BufferPool, BufferHoldsData) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  const char testData[] = "Hello, BufferPool!";
  const std::size_t dataLen = std::strlen(testData);

  std::memcpy(buf->data, testData, dataLen);
  buf->length = dataLen;

  EXPECT_EQ(buf->length, dataLen);
  EXPECT_EQ(std::memcmp(buf->data, testData, dataLen), 0);

  pool.release(buf);
}

/* ----------------------------- Statistics Tests ----------------------------- */

/** @test Statistics track acquire/release correctly. */
TEST(BufferPool, StatisticsTracking) {
  constexpr std::size_t POOL_SIZE = 10;
  BufferPool pool(POOL_SIZE, 1024);

  EXPECT_EQ(pool.size(), POOL_SIZE);
  EXPECT_EQ(pool.available(), POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);

  MessageBuffer* buf1 = pool.acquire(100);
  MessageBuffer* buf2 = pool.acquire(200);
  MessageBuffer* buf3 = pool.acquire(300);

  EXPECT_EQ(pool.available(), 7);
  EXPECT_EQ(pool.acquired(), 3);

  pool.release(buf2);

  EXPECT_EQ(pool.available(), 8);
  EXPECT_EQ(pool.acquired(), 2);

  pool.release(buf1);
  pool.release(buf3);

  EXPECT_EQ(pool.available(), POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);
}

/* ----------------------------- Metadata Tests ----------------------------- */

/** @test MessageBuffer metadata fields work correctly. */
TEST(BufferPool, MetadataFields) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  buf->fullUid = 0x12345678;
  buf->opcode = 0xABCD;
  buf->sequence = 0x5678;
  buf->internalOrigin = true;
  buf->length = 42;

  EXPECT_EQ(buf->fullUid, 0x12345678);
  EXPECT_EQ(buf->opcode, 0xABCD);
  EXPECT_EQ(buf->sequence, 0x5678);
  EXPECT_TRUE(buf->internalOrigin);
  EXPECT_EQ(buf->length, 42);

  buf->clear();

  EXPECT_EQ(buf->length, 0);
  EXPECT_EQ(buf->fullUid, 0);
  EXPECT_EQ(buf->opcode, 0);
  EXPECT_EQ(buf->sequence, 0);
  EXPECT_FALSE(buf->internalOrigin);

  pool.release(buf);
}

/* ----------------------------- Thread Safety Tests ----------------------------- */

/** @test Acquire from one thread, release from another (ownership transfer). */
TEST(BufferPool, CrossThreadOwnershipTransfer) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = nullptr;

  std::thread producer([&pool, &buf]() {
    buf = pool.acquire(100);
    ASSERT_NE(buf, nullptr);
    buf->fullUid = 0xDEADBEEF;
  });

  producer.join();

  ASSERT_NE(buf, nullptr);
  EXPECT_EQ(buf->fullUid, 0xDEADBEEF);

  std::thread consumer([&pool, buf]() {
    EXPECT_EQ(buf->fullUid, 0xDEADBEEF);
    pool.release(buf);
  });

  consumer.join();

  EXPECT_EQ(pool.available(), 16);
}

/** @test Sequential acquire/release from single thread (stress test). */
TEST(BufferPool, SequentialStressTest) {
  constexpr std::size_t POOL_SIZE = 64;
  constexpr std::size_t NUM_ITERATIONS = 10000;

  BufferPool pool(POOL_SIZE, 1024);

  std::size_t successfulAcquires = 0;

  for (std::size_t i = 0; i < NUM_ITERATIONS; ++i) {
    MessageBuffer* buf = pool.acquire(100);
    if (buf != nullptr) {
      buf->fullUid = static_cast<std::uint32_t>(i);
      ++successfulAcquires;
      pool.release(buf);
    }
  }

  EXPECT_EQ(pool.available(), POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);
  EXPECT_EQ(successfulAcquires, NUM_ITERATIONS);
}

/* ----------------------------- Leak Detection Tests ----------------------------- */

/** @test Pool returns to baseline after many acquire/release cycles. */
TEST(BufferPool, NoLeaksAfterManyCycles) {
  constexpr std::size_t POOL_SIZE = 32;
  constexpr std::size_t NUM_CYCLES = 10000;

  BufferPool pool(POOL_SIZE, 1024);

  for (std::size_t cycle = 0; cycle < NUM_CYCLES; ++cycle) {
    MessageBuffer* buf = pool.acquire(100);
    ASSERT_NE(buf, nullptr);

    buf->fullUid = static_cast<std::uint32_t>(cycle);

    pool.release(buf);
  }

  EXPECT_EQ(pool.available(), POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);
}

/** @test Pool handles exhaustion and recovery correctly. */
TEST(BufferPool, ExhaustionAndRecovery) {
  constexpr std::size_t POOL_SIZE = 16;
  BufferPool pool(POOL_SIZE, 1024);

  std::vector<MessageBuffer*> buffers;

  for (std::size_t i = 0; i < POOL_SIZE; ++i) {
    MessageBuffer* buf = pool.acquire(100);
    ASSERT_NE(buf, nullptr);
    buffers.push_back(buf);
  }

  EXPECT_EQ(pool.available(), 0);
  EXPECT_EQ(pool.acquired(), POOL_SIZE);

  MessageBuffer* exhausted = pool.acquire(100);
  EXPECT_EQ(exhausted, nullptr);

  for (std::size_t i = 0; i < buffers.size(); i += 2) {
    pool.release(buffers[i]);
  }

  EXPECT_EQ(pool.available(), POOL_SIZE / 2);
  EXPECT_EQ(pool.acquired(), POOL_SIZE / 2);

  MessageBuffer* reacquired = pool.acquire(100);
  EXPECT_NE(reacquired, nullptr);
  pool.release(reacquired);

  for (std::size_t i = 1; i < buffers.size(); i += 2) {
    pool.release(buffers[i]);
  }

  EXPECT_EQ(pool.available(), POOL_SIZE);
  EXPECT_EQ(pool.acquired(), 0);
}

/* ----------------------------- Edge Case Tests ----------------------------- */

/** @test Acquire with zero size returns buffer with minimum capacity. */
TEST(BufferPool, AcquireZeroSize) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(0);

  EXPECT_NE(buf, nullptr);
  EXPECT_TRUE(buf->isValid());

  pool.release(buf);
}

/** @test Small pool (single buffer) works correctly. */
TEST(BufferPool, SingleBufferPool) {
  BufferPool pool(1, 512);

  EXPECT_EQ(pool.size(), 1);
  EXPECT_EQ(pool.available(), 1);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  EXPECT_EQ(pool.available(), 0);

  MessageBuffer* exhausted = pool.acquire(100);
  EXPECT_EQ(exhausted, nullptr);

  pool.release(buf);

  EXPECT_EQ(pool.available(), 1);
}

/** @test Large pool (many buffers) allocates correctly. */
TEST(BufferPool, LargePool) {
  constexpr std::size_t LARGE_POOL_SIZE = 512;
  BufferPool pool(LARGE_POOL_SIZE, 1024);

  EXPECT_EQ(pool.size(), LARGE_POOL_SIZE);
  EXPECT_EQ(pool.available(), LARGE_POOL_SIZE);

  std::vector<MessageBuffer*> buffers;
  for (std::size_t i = 0; i < LARGE_POOL_SIZE; ++i) {
    MessageBuffer* buf = pool.acquire(100);
    ASSERT_NE(buf, nullptr);
    buffers.push_back(buf);
  }

  EXPECT_EQ(pool.available(), 0);

  for (MessageBuffer* buf : buffers) {
    pool.release(buf);
  }

  EXPECT_EQ(pool.available(), LARGE_POOL_SIZE);
}

/* ----------------------------- Refcount Tests ----------------------------- */

/** @test Acquired buffer has refcount of 1. */
TEST(BufferPool, AcquiredBufferRefcountIsOne) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  EXPECT_EQ(buf->getRefCount(), 1);

  pool.release(buf);
}

/** @test Single release with refcount=1 returns buffer to pool. */
TEST(BufferPool, SingleReleaseReturnsToPool) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);
  EXPECT_EQ(pool.available(), 15);

  // refcount=1, so release should return to pool
  pool.release(buf);
  EXPECT_EQ(pool.available(), 16);
}

/** @test addRef increments refcount. */
TEST(BufferPool, AddRefIncrementsRefcount) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  EXPECT_EQ(buf->getRefCount(), 1);

  buf->addRef();
  EXPECT_EQ(buf->getRefCount(), 2);

  buf->addRef();
  EXPECT_EQ(buf->getRefCount(), 3);

  // Release three times to return to pool
  pool.release(buf);
  pool.release(buf);
  pool.release(buf);
}

/** @test Multiple releases with refcount>1 only returns on last release. */
TEST(BufferPool, MultipleReleasesWithRefcount) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);
  EXPECT_EQ(pool.available(), 15);

  // Simulate multicast to 3 recipients
  buf->setRefCount(3);
  EXPECT_EQ(buf->getRefCount(), 3);

  // First release: refcount 3->2, buffer NOT returned
  pool.release(buf);
  EXPECT_EQ(pool.available(), 15); // Still 15

  // Second release: refcount 2->1, buffer NOT returned
  pool.release(buf);
  EXPECT_EQ(pool.available(), 15); // Still 15

  // Third release: refcount 1->0, buffer returned
  pool.release(buf);
  EXPECT_EQ(pool.available(), 16); // Now 16
}

/** @test setRefCount sets exact refcount value. */
TEST(BufferPool, SetRefCountSetsValue) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  buf->setRefCount(5);
  EXPECT_EQ(buf->getRefCount(), 5);

  buf->setRefCount(1);
  EXPECT_EQ(buf->getRefCount(), 1);

  pool.release(buf);
}

/** @test clear() resets refcount to 1. */
TEST(BufferPool, ClearResetsRefcount) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  buf->setRefCount(10);
  EXPECT_EQ(buf->getRefCount(), 10);

  buf->clear();
  EXPECT_EQ(buf->getRefCount(), 1);

  pool.release(buf);
}

/** @test Reacquired buffer has refcount reset to 1. */
TEST(BufferPool, ReacquiredBufferRefcountReset) {
  BufferPool pool(1, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  buf->setRefCount(5);
  EXPECT_EQ(buf->getRefCount(), 5);

  // Simulate all 5 releases
  for (int i = 0; i < 5; ++i) {
    pool.release(buf);
  }

  // Reacquire same buffer
  MessageBuffer* buf2 = pool.acquire(100);
  ASSERT_NE(buf2, nullptr);

  // Should be reset to 1
  EXPECT_EQ(buf2->getRefCount(), 1);

  pool.release(buf2);
}

/** @test decRef returns true only when refcount reaches zero. */
TEST(BufferPool, DecRefReturnsTrueOnZero) {
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  buf->setRefCount(3);

  // decRef from 3->2, returns false
  EXPECT_FALSE(buf->decRef());
  EXPECT_EQ(buf->getRefCount(), 2);

  // decRef from 2->1, returns false
  EXPECT_FALSE(buf->decRef());
  EXPECT_EQ(buf->getRefCount(), 1);

  // decRef from 1->0, returns true
  EXPECT_TRUE(buf->decRef());
  EXPECT_EQ(buf->getRefCount(), 0);

  // Reset and release
  buf->setRefCount(1);
  pool.release(buf);
}

/** @test Multicast simulation: same buffer, multiple recipients. */
TEST(BufferPool, MulticastSimulation) {
  constexpr std::size_t NUM_RECIPIENTS = 5;
  BufferPool pool(16, 1024);

  MessageBuffer* buf = pool.acquire(100);
  ASSERT_NE(buf, nullptr);

  // Write test data
  const char testData[] = "Multicast payload";
  std::memcpy(buf->data, testData, sizeof(testData));
  buf->length = sizeof(testData);
  buf->fullUid = 0xABCD1234;
  buf->opcode = 0x0100;

  // Set refcount for multicast
  buf->setRefCount(NUM_RECIPIENTS);
  EXPECT_EQ(pool.available(), 15);

  // Simulate each recipient reading and releasing
  for (std::size_t i = 0; i < NUM_RECIPIENTS; ++i) {
    // Each recipient can read the same data
    EXPECT_EQ(std::memcmp(buf->data, testData, sizeof(testData)), 0);
    EXPECT_EQ(buf->length, sizeof(testData));
    EXPECT_EQ(buf->fullUid, 0xABCD1234);
    EXPECT_EQ(buf->opcode, 0x0100);

    // Release (only last one returns to pool)
    pool.release(buf);
  }

  // Buffer should now be back in pool
  EXPECT_EQ(pool.available(), 16);
}
