/**
 * @file FileSystemBase_uTest.cpp
 * @brief Unit tests for FileSystemBase (creation, maintenance, teardown).
 */

#include "src/system/core/components/filesystem/apex/inc/FileSystemBase.hpp"
#include "src/system/core/components/filesystem/apex/inc/FileSystemStatus.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using system_core::filesystem::FileSystemBase;
using system_core::filesystem::Status;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

std::filesystem::path uniqTempDir(const std::string& stem) {
  const auto DIR = std::filesystem::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  return DIR / (stem + "_" + std::to_string(dist(gen)));
}

std::string slurp(const std::filesystem::path& p) {
  std::ifstream ifs(p, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

/**
 * @brief Minimal mock derivative of FileSystemBase for tests.
 */
class MockFileSystem : public FileSystemBase {
public:
  explicit MockFileSystem(std::filesystem::path fs, std::string name = "fs") noexcept
      : FileSystemBase(std::move(fs), std::move(name)) {}

  using FileSystemBase::archivePath;

protected:
  std::uint8_t doInit() noexcept override {
    return static_cast<std::uint8_t>(system_core::system_component::Status::SUCCESS);
  }
};

} // namespace

/* ----------------------------- Fixture ----------------------------- */

/**
 * @brief Fixture sets up a unique temp root and a logger per test.
 */
class FileSystemBaseTest : public ::testing::Test {
protected:
  std::shared_ptr<logs::SystemLog> sysLog_{};
  std::filesystem::path root_{};

  void SetUp() override {
    root_ = uniqTempDir("fsbase_root");
    std::error_code ec;
    std::filesystem::create_directories(root_, ec);
    const auto LOG_PATH = root_ / "fsbase_test.log";
    sysLog_ = std::make_shared<logs::SystemLog>(LOG_PATH.string());
  }

  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }
};

/* ----------------------------- API Tests ----------------------------- */

/** @test ensureFsExists creates the root when missing. */
TEST_F(FileSystemBaseTest, EnsureFsExistsCreatesRoot) {
  const auto ALT_ROOT = uniqTempDir("fsbase_root_missing");
  std::error_code ec;
  std::filesystem::remove_all(ALT_ROOT, ec);

  MockFileSystem fs(ALT_ROOT);
  EXPECT_FALSE(std::filesystem::exists(ALT_ROOT));
  EXPECT_EQ(fs.ensureFsExists(), Status::SUCCESS);
  EXPECT_TRUE(std::filesystem::exists(ALT_ROOT));

  std::filesystem::remove_all(ALT_ROOT, ec);
}

/** @test createDirectories creates all requested directories. */
TEST_F(FileSystemBaseTest, CreateDirectories) {
  MockFileSystem fs(root_);

  const std::vector<std::filesystem::path> DIRS{
      root_ / "dir1",
      root_ / "dir2",
      root_ / "a/b/c",
  };

  EXPECT_EQ(fs.createDirectories(DIRS), Status::SUCCESS);
  for (const auto& d : DIRS) {
    EXPECT_TRUE(std::filesystem::exists(d)) << "Missing: " << d;
  }
}

/** @test exists returns true for a present directory root. */
TEST_F(FileSystemBaseTest, ExistsProbe) {
  MockFileSystem fs(root_);
  EXPECT_TRUE(fs.exists());
}

/** @test isUnderRoot validates paths inside vs outside the root. */
TEST_F(FileSystemBaseTest, IsUnderRootChecks) {
  MockFileSystem fs(root_);
  EXPECT_TRUE(fs.isUnderRoot(root_ / "x"));
  EXPECT_TRUE(fs.isUnderRoot(std::filesystem::path("x/y"))); // relative -> under root
  // Construct a path outside the root (parent of temp dir).
  const auto PARENT = root_.parent_path();
  EXPECT_FALSE(fs.isUnderRoot(PARENT / "not_under"));
}

/** @test writeFileAtomic writes bytes and renames into place. */
TEST_F(FileSystemBaseTest, WriteFileAtomic) {
  MockFileSystem fs(root_);
  const auto TARGET = root_ / "data.bin";
  const std::vector<std::uint8_t> BYTES{1, 2, 3, 4, 5};

  EXPECT_EQ(fs.writeFileAtomic(TARGET, BYTES, /*doFsync*/ false), Status::SUCCESS);
  ASSERT_TRUE(std::filesystem::exists(TARGET));

  const auto CONTENT = slurp(TARGET);
  ASSERT_EQ(CONTENT.size(), BYTES.size());
  for (size_t i = 0; i < BYTES.size(); ++i) {
    EXPECT_EQ(static_cast<unsigned char>(CONTENT[i]), BYTES[i]);
  }
}

/** @test clearContentsExcept preserves keep-list entries. */
TEST_F(FileSystemBaseTest, ClearContentsExcept) {
  MockFileSystem fs(root_);

  const auto KEEP_A = root_ / "keep_a.txt";
  const auto KEEP_B = root_ / "dir_keep/keep_b.txt";
  const auto DROP_A = root_ / "drop_a.txt";
  const auto DROP_DIR = root_ / "drop_dir";

  std::error_code ec;
  std::filesystem::create_directories(KEEP_B.parent_path(), ec);
  std::filesystem::create_directories(DROP_DIR, ec);

  {
    std::ofstream(KEEP_A) << "KA";
  }
  {
    std::ofstream(KEEP_B) << "KB";
  }
  {
    std::ofstream(DROP_A) << "DA";
  }
  {
    std::ofstream(DROP_DIR / "x") << "X";
  }

  const std::vector<std::filesystem::path> KEEP_LIST{
      KEEP_A,
      std::filesystem::relative(KEEP_B, root_), // mix absolute + relative
  };

  EXPECT_EQ(fs.clearContentsExcept(KEEP_LIST), Status::SUCCESS);

  EXPECT_TRUE(std::filesystem::exists(KEEP_A));
  EXPECT_TRUE(std::filesystem::exists(KEEP_B));
  EXPECT_FALSE(std::filesystem::exists(DROP_A));
  EXPECT_FALSE(std::filesystem::exists(DROP_DIR));
}

/** @test pruneByAge removes files older than the threshold. */
TEST_F(FileSystemBaseTest, PruneByAge) {
  using clock = std::filesystem::file_time_type::clock;

  MockFileSystem fs(root_);
  const auto YOUNG = root_ / "young.dat";
  const auto OLD = root_ / "old.dat";

  {
    std::ofstream(YOUNG) << "y";
  }
  {
    std::ofstream(OLD) << "o";
  }

  // Mark OLD as older than threshold.
  std::error_code ec;
  const auto NOW = clock::now();
  std::filesystem::last_write_time(OLD, NOW - std::chrono::seconds(3600), ec); // 1h old
  ASSERT_FALSE(ec);

  EXPECT_EQ(fs.pruneByAge(root_, /*maxAgeSec*/ 60), Status::SUCCESS); // keep <= 60s

  EXPECT_TRUE(std::filesystem::exists(YOUNG));
  EXPECT_FALSE(std::filesystem::exists(OLD));
}

/** @test pruneBySize retains newest files while keeping total under cap. */
TEST_F(FileSystemBaseTest, PruneBySizeKeepsNewest) {
  using clock = std::filesystem::file_time_type::clock;

  MockFileSystem fs(root_);
  const auto A = root_ / "a.bin";
  const auto B = root_ / "b.bin";
  const auto C = root_ / "c.bin";

  {
    std::ofstream(A, std::ios::binary).write(std::string(100, 'A').data(), 100);
    std::ofstream(B, std::ios::binary).write(std::string(100, 'B').data(), 100);
    std::ofstream(C, std::ios::binary).write(std::string(100, 'C').data(), 100);
  }

  // Make timestamps increasing: A oldest, then B, then C newest.
  std::error_code ec;
  const auto T0 = clock::now() - std::chrono::seconds(300);
  std::filesystem::last_write_time(A, T0, ec);
  ASSERT_FALSE(ec);
  std::filesystem::last_write_time(B, T0 + std::chrono::seconds(100), ec);
  ASSERT_FALSE(ec);
  std::filesystem::last_write_time(C, T0 + std::chrono::seconds(200), ec);
  ASSERT_FALSE(ec);

  // Cap total to 200 bytes; expect A (oldest) removed, B and C kept.
  EXPECT_EQ(fs.pruneBySize(root_, /*maxBytes*/ 200), Status::SUCCESS);

  EXPECT_FALSE(std::filesystem::exists(A));
  EXPECT_TRUE(std::filesystem::exists(B));
  EXPECT_TRUE(std::filesystem::exists(C));
}

/** @test checkSpace returns available and enforces minimum threshold (best-effort). */
TEST_F(FileSystemBaseTest, CheckSpace) {
  MockFileSystem fs(root_);
  std::uintmax_t avail = 0;
  EXPECT_EQ(fs.checkSpace(0, &avail), Status::SUCCESS);
  // Enforce an unreasonably large minimum to trigger failure deterministically.
  EXPECT_EQ(fs.checkSpace(static_cast<std::uintmax_t>(-1), nullptr),
            Status::ERROR_FS_CREATION_FAIL);
}

/** @test archivePath default is [root]/[name].tar. */
TEST_F(FileSystemBaseTest, ArchivePathDefault) {
  MockFileSystem fs(root_, "myfs");
  const auto EXPECTED = root_ / "myfs.tar";
  EXPECT_EQ(fs.archivePath(), EXPECTED);
}

/** @test cleanup archives and clears contents except the archive (best-effort). */
TEST_F(FileSystemBaseTest, CleanupArchivesAndClears) {
  // This test assumes a working 'tar' tool. If unavailable, it will fail with TAR_CREATE_FAIL.
  MockFileSystem fs(root_, "fs");
  const auto KEEP_FILE = root_ / "keep.txt";
  const auto SUBDIR = root_ / "dir";
  std::error_code ec;
  std::filesystem::create_directories(SUBDIR, ec);
  {
    std::ofstream(KEEP_FILE) << "x";
  }
  {
    std::ofstream(SUBDIR / "y") << "y";
  }

  const auto ST = fs.cleanup();
  if (ST == Status::SUCCESS) {
    EXPECT_FALSE(std::filesystem::exists(KEEP_FILE));
    EXPECT_FALSE(std::filesystem::exists(SUBDIR));
    EXPECT_TRUE(std::filesystem::exists(root_ / "fs.tar"));
  } else {
    // Acceptable in environments without tar; verify status code maps to tar failure.
    EXPECT_EQ(ST, Status::ERROR_FS_TAR_CREATE_FAIL);
  }
}
