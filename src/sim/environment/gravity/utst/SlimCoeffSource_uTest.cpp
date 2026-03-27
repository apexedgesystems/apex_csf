/**
 * @file SlimCoeffSource_uTest.cpp
 * @brief Unit tests for SlimCoeffSource compact coefficient readers.
 *
 * Tests cover:
 *  - Type instantiation and basic API
 *  - File open failures (non-existent files)
 *  - Integration with CoeffSource interface
 */

#include "src/sim/environment/gravity/inc/SlimCoeffSource.hpp"
#include "src/sim/environment/gravity/inc/SlimCoeffTable.hpp"

#include <cstdio>
#include <cstring>

#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using sim::environment::gravity::GravityNSRecordD;
using sim::environment::gravity::GravityNSRecordF;
using sim::environment::gravity::SlimCoeffSourceD;
using sim::environment::gravity::SlimCoeffSourceF;
using sim::environment::gravity::SlimCoeffTableD;
using sim::environment::gravity::SlimCoeffTableF;

/* ----------------------------- Type Instantiation ----------------------------- */

/** @test Verify SlimCoeffTableD type can be instantiated. */
TEST(SlimCoeffTableD, TypeInstantiation) {
  SlimCoeffTableD table;
  EXPECT_FALSE(table.isOpen());
  EXPECT_EQ(table.minDegree(), 0);
  EXPECT_EQ(table.maxDegree(), -1);
  EXPECT_EQ(table.recordCount(), 0u);
}

/** @test Verify SlimCoeffTableF type can be instantiated. */
TEST(SlimCoeffTableF, TypeInstantiation) {
  SlimCoeffTableF table;
  EXPECT_FALSE(table.isOpen());
  EXPECT_EQ(table.minDegree(), 0);
  EXPECT_EQ(table.maxDegree(), -1);
  EXPECT_EQ(table.recordCount(), 0u);
}

/** @test Verify SlimCoeffSourceD type can be instantiated. */
TEST(SlimCoeffSourceD, TypeInstantiation) {
  SlimCoeffSourceD src;
  EXPECT_EQ(src.minDegree(), 0);
  EXPECT_EQ(src.maxDegree(), -1);

  double c = 0.0, s = 0.0;
  EXPECT_FALSE(src.get(0, 0, c, s));
}

/** @test Verify SlimCoeffSourceF type can be instantiated. */
TEST(SlimCoeffSourceF, TypeInstantiation) {
  SlimCoeffSourceF src;
  EXPECT_EQ(src.minDegree(), 0);
  EXPECT_EQ(src.maxDegree(), -1);

  double c = 0.0, s = 0.0;
  EXPECT_FALSE(src.get(0, 0, c, s));
}

/* ----------------------------- File Open Failures ----------------------------- */

/** @test SlimCoeffSourceD open fails for non-existent file. */
TEST(SlimCoeffSourceD, OpenNonExistent) {
  SlimCoeffSourceD src;
  EXPECT_FALSE(src.open("/nonexistent/path/to/file.bin"));
}

/** @test SlimCoeffSourceF open fails for non-existent file. */
TEST(SlimCoeffSourceF, OpenNonExistent) {
  SlimCoeffSourceF src;
  EXPECT_FALSE(src.open("/nonexistent/path/to/file.bin"));
}

/* ----------------------------- Record Size Verification ----------------------------- */

/** @test Verify GravityNSRecordD is exactly 20 bytes. */
TEST(GravityNSRecordD, RecordSize) {
  EXPECT_EQ(sizeof(GravityNSRecordD), 20u);
  EXPECT_EQ(SlimCoeffTableD::RECORD_SIZE, 20u);
}

/** @test Verify GravityNSRecordF is exactly 12 bytes. */
TEST(GravityNSRecordF, RecordSize) {
  EXPECT_EQ(sizeof(GravityNSRecordF), 12u);
  EXPECT_EQ(SlimCoeffTableF::RECORD_SIZE, 12u);
}

/* ----------------------------- File Read Tests ----------------------------- */

namespace {

/**
 * @brief Helper to create a temporary file with slim records.
 */
template <typename RecordT> std::string createTempFile(const std::vector<RecordT>& records) {
  std::string path = "/tmp/slim_coeff_test_XXXXXX";
  int fd = mkstemp(&path[0]);
  if (fd < 0)
    return "";
  close(fd);

  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) {
    std::remove(path.c_str());
    return "";
  }

  for (const auto& rec : records) {
    out.write(reinterpret_cast<const char*>(&rec), sizeof(RecordT));
  }
  out.close();
  return path;
}

} // namespace

/** @test SlimCoeffSourceD reads triangular file correctly. */
TEST(SlimCoeffSourceD, ReadTriangular) {
  // Create a triangular file with degrees 0-2:
  // (0,0), (1,0), (1,1), (2,0), (2,1), (2,2) = 6 records
  std::vector<GravityNSRecordD> records;
  for (int16_t n = 0; n <= 2; ++n) {
    for (int16_t m = 0; m <= n; ++m) {
      GravityNSRecordD rec{};
      rec.n = n;
      rec.m = m;
      rec.Cbar = 1.0 + 0.1 * n + 0.01 * m;
      rec.Sbar = 0.5 + 0.05 * n + 0.005 * m;
      records.push_back(rec);
    }
  }

  std::string path = createTempFile(records);
  ASSERT_FALSE(path.empty()) << "Failed to create temp file";

  SlimCoeffSourceD src;
  ASSERT_TRUE(src.open(path));
  EXPECT_EQ(src.minDegree(), 0);
  EXPECT_EQ(src.maxDegree(), 2);

  // Read and verify all coefficients
  for (int16_t n = 0; n <= 2; ++n) {
    for (int16_t m = 0; m <= n; ++m) {
      double c = 0.0, s = 0.0;
      EXPECT_TRUE(src.get(n, m, c, s)) << "Failed to get(" << n << "," << m << ")";
      EXPECT_DOUBLE_EQ(c, 1.0 + 0.1 * n + 0.01 * m);
      EXPECT_DOUBLE_EQ(s, 0.5 + 0.05 * n + 0.005 * m);
    }
  }

  // Out-of-range reads should fail
  double c = 0.0, s = 0.0;
  EXPECT_FALSE(src.get(3, 0, c, s));
  EXPECT_FALSE(src.get(-1, 0, c, s));
  EXPECT_FALSE(src.get(2, 3, c, s));

  std::remove(path.c_str());
}

/** @test SlimCoeffSourceF reads triangular file correctly with float precision. */
TEST(SlimCoeffSourceF, ReadTriangular) {
  // Create a triangular file with degrees 0-2
  std::vector<GravityNSRecordF> records;
  for (int16_t n = 0; n <= 2; ++n) {
    for (int16_t m = 0; m <= n; ++m) {
      GravityNSRecordF rec{};
      rec.n = n;
      rec.m = m;
      rec.cbar = 1.0f + 0.1f * n + 0.01f * m;
      rec.sbar = 0.5f + 0.05f * n + 0.005f * m;
      records.push_back(rec);
    }
  }

  std::string path = createTempFile(records);
  ASSERT_FALSE(path.empty()) << "Failed to create temp file";

  SlimCoeffSourceF src;
  ASSERT_TRUE(src.open(path));
  EXPECT_EQ(src.minDegree(), 0);
  EXPECT_EQ(src.maxDegree(), 2);

  // Read and verify all coefficients (with float tolerance)
  for (int16_t n = 0; n <= 2; ++n) {
    for (int16_t m = 0; m <= n; ++m) {
      double c = 0.0, s = 0.0;
      EXPECT_TRUE(src.get(n, m, c, s)) << "Failed to get(" << n << "," << m << ")";
      EXPECT_FLOAT_EQ(static_cast<float>(c), 1.0f + 0.1f * n + 0.01f * m);
      EXPECT_FLOAT_EQ(static_cast<float>(s), 0.5f + 0.05f * n + 0.005f * m);
    }
  }

  std::remove(path.c_str());
}

/** @test SlimCoeffSourceD handles file size not multiple of record size. */
TEST(SlimCoeffSourceD, InvalidFileSize) {
  // Create a file with invalid size (not multiple of 20)
  std::string path = "/tmp/slim_coeff_invalid_XXXXXX";
  int fd = mkstemp(&path[0]);
  ASSERT_GE(fd, 0);
  close(fd);

  std::ofstream out(path, std::ios::binary);
  ASSERT_TRUE(out.is_open());
  char data[25] = {}; // 25 bytes, not a multiple of 20
  out.write(data, sizeof(data));
  out.close();

  SlimCoeffSourceD src;
  EXPECT_FALSE(src.open(path));

  std::remove(path.c_str());
}
