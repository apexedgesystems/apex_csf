/**
 * @file MonteCarloExport_uTest.cpp
 * @brief Unit tests for monte_carlo::MonteCarloExport utilities.
 *
 * Notes:
 *  - Tests verify CSV output format, summary printing, and yield computation.
 *  - File-based tests use temporary paths and clean up after themselves.
 */

#include "src/system/core/monte_carlo/inc/MonteCarloExport.hpp"

#include <cstdint>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using apex::monte_carlo::ColumnDef;
using apex::monte_carlo::computeYield;
using apex::monte_carlo::exportCsv;
using apex::monte_carlo::exportSummaryCsv;
using apex::monte_carlo::MonteCarloResults;
using apex::monte_carlo::printSummary;

/* ----------------------------- Test Helpers ----------------------------- */

struct SimpleResult {
  double voltage;
  double current;
};

static MonteCarloResults<SimpleResult> makeTestResults() {
  MonteCarloResults<SimpleResult> results;
  results.runs = {{1.0, 0.1}, {2.0, 0.2}, {3.0, 0.3}, {4.0, 0.4}, {5.0, 0.5}};
  results.totalRuns = 5;
  results.completedRuns = 5;
  results.failedRuns = 0;
  results.wallTimeSeconds = 0.5;
  results.threadCount = 2;
  return results;
}

static std::vector<ColumnDef<SimpleResult>> makeTestColumns() {
  return {{"voltage", [](const SimpleResult& r) { return r.voltage; }},
          {"current", [](const SimpleResult& r) { return r.current; }}};
}

class ExportTest : public ::testing::Test {
protected:
  std::filesystem::path tempDir_;

  void SetUp() override {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    tempDir_ =
        std::filesystem::temp_directory_path() / ("mc_export_test_" + std::to_string(dist(gen)));
    std::filesystem::create_directories(tempDir_);
  }

  void TearDown() override { std::filesystem::remove_all(tempDir_); }

  [[nodiscard]] std::string readFile(const std::filesystem::path& path) const {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
  }

  [[nodiscard]] std::vector<std::string> readLines(const std::filesystem::path& path) const {
    std::ifstream in(path);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty()) {
        lines.push_back(line);
      }
    }
    return lines;
  }
};

/* ----------------------------- exportCsv Tests ----------------------------- */

/** @test exportCsv writes header and data rows */
TEST_F(ExportTest, CsvHeaderAndRows) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();
  const auto PATH = tempDir_ / "results.csv";

  ASSERT_TRUE(exportCsv(results, PATH, columns));

  auto lines = readLines(PATH);
  ASSERT_EQ(lines.size(), 6U); // 1 header + 5 data

  EXPECT_EQ(lines[0], "run_index,voltage,current");
  EXPECT_TRUE(lines[1].find("0,") == 0);
  EXPECT_TRUE(lines[5].find("4,") == 0);
}

/** @test exportCsv with empty results writes header only */
TEST_F(ExportTest, CsvEmptyResults) {
  MonteCarloResults<SimpleResult> results;
  auto columns = makeTestColumns();
  const auto PATH = tempDir_ / "empty.csv";

  ASSERT_TRUE(exportCsv(results, PATH, columns));

  auto lines = readLines(PATH);
  ASSERT_EQ(lines.size(), 1U);
  EXPECT_EQ(lines[0], "run_index,voltage,current");
}

/** @test exportCsv returns false for invalid path */
TEST_F(ExportTest, CsvInvalidPath) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();

  EXPECT_FALSE(exportCsv(results, "/nonexistent/dir/file.csv", columns));
}

/** @test exportCsv data values are correct */
TEST_F(ExportTest, CsvDataValues) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();
  const auto PATH = tempDir_ / "values.csv";

  ASSERT_TRUE(exportCsv(results, PATH, columns));

  auto lines = readLines(PATH);
  // Row 0: run_index=0, voltage=1.0, current=0.1
  EXPECT_TRUE(lines[1].find("0,1,0.1") == 0);
}

/* ----------------------------- exportSummaryCsv Tests ----------------------------- */

/** @test exportSummaryCsv writes field statistics */
TEST_F(ExportTest, SummaryCsvFieldStats) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();
  const auto PATH = tempDir_ / "summary.csv";

  ASSERT_TRUE(exportSummaryCsv(results, PATH, columns));

  auto lines = readLines(PATH);
  ASSERT_EQ(lines.size(), 3U); // 1 header + 2 fields

  EXPECT_EQ(lines[0], "field,count,mean,stddev,min,p05,p25,median,p75,p95,max");
  EXPECT_TRUE(lines[1].find("voltage,") == 0);
  EXPECT_TRUE(lines[2].find("current,") == 0);
}

/* ----------------------------- printSummary Tests ----------------------------- */

/** @test printSummary includes execution metadata */
TEST_F(ExportTest, SummaryContainsMetadata) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();

  std::ostringstream oss;
  printSummary(oss, results, columns);
  const auto OUTPUT = oss.str();

  EXPECT_TRUE(OUTPUT.find("5 runs") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("5 completed") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("0 failed") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("2 threads") != std::string::npos);
}

/** @test printSummary includes field names */
TEST_F(ExportTest, SummaryContainsFieldNames) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();

  std::ostringstream oss;
  printSummary(oss, results, columns);
  const auto OUTPUT = oss.str();

  EXPECT_TRUE(OUTPUT.find("voltage") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("current") != std::string::npos);
}

/** @test printSummary includes column headers */
TEST_F(ExportTest, SummaryContainsColumnHeaders) {
  auto results = makeTestResults();
  auto columns = makeTestColumns();

  std::ostringstream oss;
  printSummary(oss, results, columns);
  const auto OUTPUT = oss.str();

  EXPECT_TRUE(OUTPUT.find("Mean") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("Stddev") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("Median") != std::string::npos);
}

/** @test printSummary shows failure rate when failures exist */
TEST_F(ExportTest, SummaryShowsFailureRate) {
  auto results = makeTestResults();
  results.failedRuns = 2;
  results.completedRuns = 3;
  auto columns = makeTestColumns();

  std::ostringstream oss;
  printSummary(oss, results, columns);
  const auto OUTPUT = oss.str();

  EXPECT_TRUE(OUTPUT.find("Failure rate") != std::string::npos);
  EXPECT_TRUE(OUTPUT.find("2/5") != std::string::npos);
}

/* ----------------------------- computeYield Tests ----------------------------- */

/** @test computeYield with upper limit */
TEST(YieldTest, UpperLimit) {
  MonteCarloResults<SimpleResult> results;
  results.runs = {{1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}, {4.0, 0.0}, {5.0, 0.0}};

  // voltage <= 3.0 passes: 3 out of 5
  const double YIELD = computeYield<SimpleResult>(
      results, [](const SimpleResult& r) { return r.voltage; }, 3.0, true);

  EXPECT_DOUBLE_EQ(YIELD, 0.6);
}

/** @test computeYield with lower limit */
TEST(YieldTest, LowerLimit) {
  MonteCarloResults<SimpleResult> results;
  results.runs = {{1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}, {4.0, 0.0}, {5.0, 0.0}};

  // voltage >= 3.0 passes: 3 out of 5
  const double YIELD = computeYield<SimpleResult>(
      results, [](const SimpleResult& r) { return r.voltage; }, 3.0, false);

  EXPECT_DOUBLE_EQ(YIELD, 0.6);
}

/** @test computeYield with all pass */
TEST(YieldTest, AllPass) {
  MonteCarloResults<SimpleResult> results;
  results.runs = {{1.0, 0.0}, {2.0, 0.0}, {3.0, 0.0}};

  const double YIELD = computeYield<SimpleResult>(
      results, [](const SimpleResult& r) { return r.voltage; }, 10.0, true);

  EXPECT_DOUBLE_EQ(YIELD, 1.0);
}

/** @test computeYield with empty results */
TEST(YieldTest, EmptyResults) {
  MonteCarloResults<SimpleResult> results;

  const double YIELD = computeYield<SimpleResult>(
      results, [](const SimpleResult& r) { return r.voltage; }, 5.0, true);

  EXPECT_DOUBLE_EQ(YIELD, 0.0);
}
