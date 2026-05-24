/**
 * @file FileSystem_pTest.cpp
 * @brief Performance tests for the POSIX ApexFileSystem component.
 *
 * Measures:
 *  - Path validation (isUnderRoot) for valid and escape paths
 *  - Cached directory path accessors
 *  - Existence checks
 *  - Atomic file write throughput (small + medium payload)
 *  - Status-to-string conversion overhead
 *
 * Usage:
 *   ./FileSystem_PTEST --csv results.csv
 *   ./FileSystem_PTEST --quick
 *   ./FileSystem_PTEST --profile perf
 */

#include "src/bench/inc/Perf.hpp"
#include "src/system/core/components/filesystem/posix/inc/ApexFileSystem.hpp"
#include "src/system/core/components/filesystem/posix/inc/FileSystemBase.hpp"
#include "src/system/core/components/filesystem/posix/inc/FileSystemStatus.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ub = vernier::bench;
using namespace system_core::filesystem;
namespace fs = std::filesystem;

/* ----------------------------- Path Validation Benchmarks ----------------------------- */

/**
 * @brief Benchmark isUnderRoot path validation.
 *
 * This is called frequently during file operations to ensure paths
 * stay within the filesystem root. Uses cached canonical root.
 */
PERF_TEST(FileSystemPerf, IsUnderRootValid) {
  UB_PERF_GUARD(perf);

  fs::path tempRoot = fs::temp_directory_path() / "fs_ptest_underroot";
  fs::create_directories(tempRoot);
  ApexFileSystem afs(tempRoot, "test");
  (void)afs.init();

  // Valid path under root
  fs::path validPath = tempRoot / "logs" / "test.log";

  auto fn = [&]() {
    volatile bool result = afs.isUnderRoot(validPath);
    (void)result;
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "isUnderRoot_valid");

  fs::remove_all(tempRoot);
}

/**
 * @brief Benchmark isUnderRoot with invalid (escape) path.
 */
PERF_TEST(FileSystemPerf, IsUnderRootInvalid) {
  UB_PERF_GUARD(perf);

  fs::path tempRoot = fs::temp_directory_path() / "fs_ptest_underroot_inv";
  fs::create_directories(tempRoot);
  ApexFileSystem afs(tempRoot, "test");
  (void)afs.init();

  // Path attempting to escape root
  fs::path escapePath = tempRoot / ".." / "escape" / "file.txt";

  auto fn = [&]() {
    volatile bool result = afs.isUnderRoot(escapePath);
    (void)result;
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "isUnderRoot_escape");

  fs::remove_all(tempRoot);
}

/* ----------------------------- Directory Accessor Benchmarks ----------------------------- */

/**
 * @brief Benchmark directory path accessor calls.
 *
 * These are O(1) returns of cached paths. Measures accessor overhead.
 */
PERF_TEST(FileSystemPerf, DirectoryAccessors) {
  UB_PERF_GUARD(perf);

  fs::path tempRoot = fs::temp_directory_path() / "fs_ptest_accessors";
  fs::create_directories(tempRoot);
  ApexFileSystem afs(tempRoot, "test");
  (void)afs.init();

  auto fn = [&]() {
    volatile auto p1 = afs.logDir();
    volatile auto p2 = afs.libDir();
    volatile auto p3 = afs.tlmDir();
    volatile auto p4 = afs.tprmDir();
    (void)p1;
    (void)p2;
    (void)p3;
    (void)p4;
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "DirectoryAccessors_4x");

  fs::remove_all(tempRoot);
}

/* ----------------------------- Existence Check Benchmarks ----------------------------- */

/**
 * @brief Benchmark filesystem existence check.
 */
PERF_TEST(FileSystemPerf, ExistsCheck) {
  UB_PERF_GUARD(perf);

  fs::path tempRoot = fs::temp_directory_path() / "fs_ptest_exists";
  fs::create_directories(tempRoot);
  ApexFileSystem afs(tempRoot, "test");
  (void)afs.init();

  auto fn = [&]() {
    volatile bool result = afs.exists();
    (void)result;
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "exists_check");

  fs::remove_all(tempRoot);
}

/* ----------------------------- Atomic Write Benchmarks ----------------------------- */

/**
 * @brief Benchmark atomic file write (small payload).
 *
 * Uses temp+rename pattern for atomicity. This is I/O bound but
 * we measure the overhead of the atomic write mechanism.
 */
PERF_TEST(FileSystemPerf, AtomicWriteSmall) {
  UB_PERF_GUARD(perf);

  fs::path tempRoot = fs::temp_directory_path() / "fs_ptest_atomic";
  fs::create_directories(tempRoot);
  ApexFileSystem afs(tempRoot, "test");
  (void)afs.init();

  // Small 64-byte payload
  std::vector<std::uint8_t> payload(64, 0xAB);
  fs::path target = tempRoot / "logs" / "atomic_test.bin";
  std::size_t counter = 0;

  auto fn = [&]() {
    // Vary filename to avoid OS caching effects
    fs::path varTarget = tempRoot / "logs" / ("atomic_" + std::to_string(counter++ % 10) + ".bin");
    (void)afs.writeFileAtomic(varTarget, payload, false);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "AtomicWrite_64B");

  fs::remove_all(tempRoot);
}

/**
 * @brief Benchmark atomic file write (medium payload).
 */
PERF_TEST(FileSystemPerf, AtomicWriteMedium) {
  UB_PERF_GUARD(perf);

  fs::path tempRoot = fs::temp_directory_path() / "fs_ptest_atomic_med";
  fs::create_directories(tempRoot);
  ApexFileSystem afs(tempRoot, "test");
  (void)afs.init();

  // 4KB payload
  std::vector<std::uint8_t> payload(4096, 0xCD);
  std::size_t counter = 0;

  auto fn = [&]() {
    fs::path varTarget = tempRoot / "logs" / ("atomic_" + std::to_string(counter++ % 10) + ".bin");
    (void)afs.writeFileAtomic(varTarget, payload, false);
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "AtomicWrite_4KB");

  fs::remove_all(tempRoot);
}

/* ----------------------------- Status Conversion Benchmarks ----------------------------- */

/**
 * @brief Benchmark status to string conversion.
 */
PERF_TEST(FileSystemPerf, StatusToString) {
  UB_PERF_GUARD(perf);

  std::array<Status, 4> statuses = {Status::SUCCESS, Status::ERROR_FS_CREATION_FAIL,
                                    Status::ERROR_FS_TAR_CREATE_FAIL, Status::ERROR_INVALID_FS};
  std::size_t idx = 0;

  auto fn = [&]() {
    const char* str = toString(statuses[idx++ % statuses.size()]);
    (void)str;
  };

  perf.warmup(fn);
  perf.throughputLoop(fn, "StatusToString");
}

PERF_MAIN()
