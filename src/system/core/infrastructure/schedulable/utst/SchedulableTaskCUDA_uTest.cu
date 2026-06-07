/**
 * @file SchedulableTaskCUDA_uTest.cu
 * @brief Unit tests for SchedulableTaskCUDA - CUDA-enabled task with stream/event support.
 *
 * Coverage:
 *  - Construction with delegate and label
 *  - CUDA stream binding (setCudaStream/getCudaStream)
 *  - Event-based completion tracking (recordCompletion/isComplete)
 *  - Non-blocking completion check
 *  - Blocking wait (waitComplete)
 *  - Move semantics
 *  - Integration with execute()
 *
 * RT-Safety Notes:
 *  - execute(): RT-safe (direct delegate call)
 *  - recordCompletion(): RT-safe (~100ns)
 *  - isComplete(): RT-safe (~100ns)
 *  - waitComplete(): NOT RT-safe (blocking)
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskCUDA.cuh"
#include "src/utilities/concurrency/inc/Delegate.hpp"

#include <cuda_runtime.h>
#include <gtest/gtest.h>
#include <cstdint>

using apex::concurrency::DelegateU8;
using system_core::schedulable::SchedulableTaskCUDA;

namespace {

std::uint8_t successTask(void*) noexcept { return 0; }
std::uint8_t returnValue(void* ctx) noexcept { return *static_cast<std::uint8_t*>(ctx); }

// Simple CUDA kernel for testing completion
__global__ void simpleKernel(int* out, int value) { *out = value; }

} // namespace

/* ----------------------------- Default Construction ----------------------------- */

/** @test Verifies construction with delegate and label. */
TEST(SchedulableTaskCUDATest, ConstructorSetsLabel) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "cuda_task");

  EXPECT_EQ(task.getLabel(), "cuda_task");
}

/** @test Verifies default stream is nullptr. */
TEST(SchedulableTaskCUDATest, DefaultStreamIsNull) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "test");

  EXPECT_EQ(task.getCudaStream(), nullptr);
}

/** @test Verifies default completion state. */
TEST(SchedulableTaskCUDATest, DefaultCompletionState) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "test");

  // Without recording, isComplete should return true (no GPU work)
  EXPECT_TRUE(task.isComplete());
}

/* ----------------------------- SchedulableTaskCUDA Method Tests ----------------------------- */

/** @test Verifies stream can be set and retrieved. */
TEST(SchedulableTaskCUDATest, SetAndGetStream) {
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "stream_test");

  task.setCudaStream(stream);
  EXPECT_EQ(task.getCudaStream(), stream);

  cudaStreamDestroy(stream);
}

/** @test Verifies stream can be changed. */
TEST(SchedulableTaskCUDATest, ChangeStream) {
  cudaStream_t stream1, stream2;
  ASSERT_EQ(cudaStreamCreate(&stream1), cudaSuccess);
  ASSERT_EQ(cudaStreamCreate(&stream2), cudaSuccess);

  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "change_stream");

  task.setCudaStream(stream1);
  EXPECT_EQ(task.getCudaStream(), stream1);

  task.setCudaStream(stream2);
  EXPECT_EQ(task.getCudaStream(), stream2);

  cudaStreamDestroy(stream1);
  cudaStreamDestroy(stream2);
}

/* ----------------------------- API Tests ----------------------------- */

/** @test Verifies recordCompletion and isComplete work together. */
TEST(SchedulableTaskCUDATest, RecordAndCheckCompletion) {
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "completion_test");
  task.setCudaStream(stream);

  // Record completion (no GPU work, so should complete immediately)
  task.recordCompletion();

  // Synchronize stream to ensure event is complete
  cudaStreamSynchronize(stream);

  EXPECT_TRUE(task.isComplete());

  cudaStreamDestroy(stream);
}

/** @test Verifies resetCompletion clears state. */
TEST(SchedulableTaskCUDATest, ResetCompletion) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "reset_test");

  // Before any recording, should be complete
  EXPECT_TRUE(task.isComplete());

  // Record and reset
  task.recordCompletion();
  task.resetCompletion();

  // After reset, should be complete (no pending event)
  EXPECT_TRUE(task.isComplete());
}

/** @test Verifies waitComplete works with actual GPU work. */
TEST(SchedulableTaskCUDATest, WaitCompleteWithGPUWork) {
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

  int* d_out;
  ASSERT_EQ(cudaMalloc(&d_out, sizeof(int)), cudaSuccess);

  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "gpu_work_test");
  task.setCudaStream(stream);

  // Launch a simple kernel
  simpleKernel<<<1, 1, 0, stream>>>(d_out, 42);

  // Record completion
  task.recordCompletion();

  // Wait for completion
  task.waitComplete();

  // After wait, should be complete
  EXPECT_TRUE(task.isComplete());

  cudaFree(d_out);
  cudaStreamDestroy(stream);
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test Verifies execute returns delegate result. */
TEST(SchedulableTaskCUDATest, ExecuteReturnsResult) {
  std::uint8_t val = 77;
  DelegateU8 del{&returnValue, &val};
  SchedulableTaskCUDA task(del, "execute_test");

  EXPECT_EQ(task.execute(), 77);
}

/** @test Verifies execute resets completion state. */
TEST(SchedulableTaskCUDATest, ExecuteResetsCompletionState) {
  DelegateU8 del{&successTask, nullptr};
  SchedulableTaskCUDA task(del, "reset_on_execute");

  // Record completion
  task.recordCompletion();

  // Execute should reset the recorded state
  (void)task.execute();

  // After execute, isComplete should return true (no pending event)
  EXPECT_TRUE(task.isComplete());
}

/* ----------------------------- Move Semantics Tests ----------------------------- */

/** @test Verifies move constructor transfers ownership. */
TEST(SchedulableTaskCUDATest, MoveConstructor) {
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

  std::uint8_t val = 55;
  DelegateU8 del{&returnValue, &val};
  SchedulableTaskCUDA task1(del, "move_test");
  task1.setCudaStream(stream);

  // Move construct
  SchedulableTaskCUDA task2(std::move(task1));

  // Moved-to task should have the stream
  EXPECT_EQ(task2.getCudaStream(), stream);
  EXPECT_EQ(task2.getLabel(), "move_test");
  EXPECT_EQ(task2.execute(), 55);

  // Moved-from task should have null stream
  EXPECT_EQ(task1.getCudaStream(), nullptr);

  cudaStreamDestroy(stream);
}

/** @test Verifies move assignment transfers ownership. */
TEST(SchedulableTaskCUDATest, MoveAssignment) {
  cudaStream_t stream;
  ASSERT_EQ(cudaStreamCreate(&stream), cudaSuccess);

  std::uint8_t val1 = 11, val2 = 22;
  DelegateU8 del1{&returnValue, &val1};
  DelegateU8 del2{&returnValue, &val2};

  SchedulableTaskCUDA task1(del1, "source");
  task1.setCudaStream(stream);

  SchedulableTaskCUDA task2(del2, "dest");

  // Move assign
  task2 = std::move(task1);

  // task2 should now have task1's properties
  EXPECT_EQ(task2.getCudaStream(), stream);
  EXPECT_EQ(task2.getLabel(), "source");

  // Moved-from should be null
  EXPECT_EQ(task1.getCudaStream(), nullptr);

  cudaStreamDestroy(stream);
}

/* ----------------------------- Constants Tests ----------------------------- */

/** @test Verifies CUDA task works through base pointer. */
TEST(SchedulableTaskCUDATest, BasePointerCompatibility) {
  std::uint8_t val = 88;
  DelegateU8 del{&returnValue, &val};
  SchedulableTaskCUDA task(del, "base_compat");

  system_core::schedulable::SchedulableTaskBase* basePtr = &task;
  EXPECT_EQ(basePtr->execute(), 88);
  EXPECT_EQ(basePtr->getLabel(), "base_compat");
}
