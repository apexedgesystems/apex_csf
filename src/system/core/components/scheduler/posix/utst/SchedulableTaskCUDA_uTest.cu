/**
 * @file SchedulableTaskCUDA_uTest.cu
 * @brief Unit tests for SchedulableTaskCUDA functionality.
 *
 * Coverage:
 *  - Construction and initialization
 *  - Execute delegation to base class
 *  - CUDA stream binding and retrieval
 *  - Integration with SchedulableTask features
 */

#include "src/system/core/infrastructure/schedulable/inc/SchedulableTaskCUDA.cuh"
#include "src/system/core/components/scheduler/posix/inc/TaskBuilder.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <cuda_runtime.h>
#include <gtest/gtest.h>

using system_core::schedulable::bindFreeFunction;
using system_core::schedulable::SchedulableTaskCUDA;

namespace {

/**
 * @brief Returns a success code.
 */
std::uint8_t successTask() { return 0; }

/**
 * @brief Returns a specific test value.
 */
std::uint8_t testValueTask() { return 42; }

} // namespace

/**
 * @brief Test fixture for SchedulableTaskCUDA unit tests.
 */
class SchedulableTaskCUDATest : public ::testing::Test {
protected:
  const std::string testLabel_ = "CUDATestTask";
  cudaStream_t mockStream_{nullptr};
  std::shared_ptr<std::atomic<int>> counter_;

  void SetUp() override {
    cudaStreamCreate(&mockStream_);
    counter_ = std::make_shared<std::atomic<int>>(0);
  }

  void TearDown() override {
    if (mockStream_) {
      cudaStreamDestroy(mockStream_);
    }
  }

  void incrementCounterUntil(int targetCount) {
    while (counter_->load() < targetCount) {
      counter_->fetch_add(1);
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
  }
};

/**
 * @test Verifies construction initializes CUDA task correctly.
 */
TEST_F(SchedulableTaskCUDATest, ConstructorInitializesTask) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  EXPECT_EQ(task.getLabel(), testLabel_);
}

/**
 * @test Verifies execute delegates to base class correctly.
 */
TEST_F(SchedulableTaskCUDATest, ExecuteRunsSuccessfully) {
  auto delegate = bindFreeFunction(&testValueTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  uint8_t result = task.execute();

  EXPECT_EQ(result, 42);
}

/**
 * @test Verifies CUDA stream can be set and retrieved.
 */
TEST_F(SchedulableTaskCUDATest, SetCudaStreamWorks) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  task.setCudaStream(mockStream_);
  cudaStream_t retrievedStream = task.getCudaStream();

  EXPECT_EQ(retrievedStream, mockStream_);
}

/**
 * @test Verifies CUDA stream starts as nullptr.
 */
TEST_F(SchedulableTaskCUDATest, DefaultStreamIsNull) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  EXPECT_EQ(task.getCudaStream(), nullptr);
}

/**
 * @test Verifies CUDA stream can be unbound.
 */
TEST_F(SchedulableTaskCUDATest, StreamCanBeUnbound) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  task.setCudaStream(mockStream_);
  EXPECT_EQ(task.getCudaStream(), mockStream_);

  task.setCudaStream(nullptr);
  EXPECT_EQ(task.getCudaStream(), nullptr);
}

/**
 * @test Verifies inherited features work correctly.
 */
TEST_F(SchedulableTaskCUDATest, InheritedFeaturesWork) {
  auto delegate = bindFreeFunction(&successTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  task.setFrequency(3, 2);
  EXPECT_FLOAT_EQ(task.frequency(), 1.5f);

  task.adjustPriority(64);
  EXPECT_EQ(task.priority(), 64);
}

/**
 * @test Verifies frequency decimation works correctly with CUDA tasks.
 */
TEST_F(SchedulableTaskCUDATest, FrequencyDecimationWorks) {
  auto delegate = bindFreeFunction(&testValueTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  task.setFrequency(1, 4);

  int executeCount = 0;
  const int TOTAL_CALLS = 20;

  for (int i = 0; i < TOTAL_CALLS; ++i) {
    std::uint8_t result = task.execute();
    if (result == 42) {
      executeCount++;
    }
  }

  EXPECT_EQ(executeCount, 5);
}

/**
 * @test Verifies sequencing logic works with CUDA tasks.
 */
TEST_F(SchedulableTaskCUDATest, SequencingWorks) {
  auto delegate = bindFreeFunction(&testValueTask);
  SchedulableTaskCUDA task(delegate, testLabel_);

  // Start counter at 4 to minimize wait time
  counter_->store(4);
  task.setSequencing(counter_, 5, 10);

  std::thread incrementThread([this]() { incrementCounterUntil(6); });

  // Give thread time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::uint8_t result = task.execute();

  incrementThread.join();

  EXPECT_EQ(result, 42);
  EXPECT_GE(counter_->load(), 5);
}