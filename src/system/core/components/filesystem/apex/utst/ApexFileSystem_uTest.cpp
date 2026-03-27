/**
 * @file ApexFileSystem_uTest.cpp
 * @brief Unit tests for ApexFileSystem (default layout with A/B bank support).
 */

#include "src/system/core/components/filesystem/apex/inc/ApexFileSystem.hpp"
#include "src/system/core/components/filesystem/apex/inc/FileSystemStatus.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <thread>

#include <gtest/gtest.h>

using system_core::filesystem::ApexFileSystem;
using system_core::filesystem::Bank;
using system_core::filesystem::Status;
namespace fs = std::filesystem;

/* ----------------------------- Test Helpers ----------------------------- */

namespace {

fs::path uniqTempDir(const std::string& stem) {
  const fs::path DIR = fs::temp_directory_path();
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<unsigned long long> dist;
  return DIR / (stem + "_" + std::to_string(dist(gen)));
}

class ApexFSDefaultShim : public ApexFileSystem {
public:
  using ApexFileSystem::ApexFileSystem;
  using ApexFileSystem::archivePath; // expose for testing
};

} // namespace

/* ----------------------------- Fixture ----------------------------- */

/**
 * @brief Fixture sets up a unique temp root and a logger per test.
 */
class ApexFileSystemTest : public ::testing::Test {
protected:
  std::shared_ptr<logs::SystemLog> sysLog_{};
  fs::path root_{};

  void SetUp() override {
    root_ = uniqTempDir("apexfs_root");
    std::error_code ec;
    fs::create_directories(root_, ec);
    const fs::path LOG_PATH = root_ / "apexfs_test.log";
    sysLog_ = std::make_shared<logs::SystemLog>(LOG_PATH.string());
  }

  void TearDown() override {
    std::error_code ec;
    fs::remove_all(root_, ec);
  }
};

/* ----------------------------- Default Construction ----------------------------- */

/** @test init creates default directories including bank A and B. */
TEST_F(ApexFileSystemTest, InitCreatesDefaultDirs) {
  ApexFileSystem afs(root_);

  const std::uint8_t ST = afs.init();
  ASSERT_EQ(ST, static_cast<std::uint8_t>(Status::SUCCESS));

  // Non-banked directories.
  EXPECT_TRUE(fs::exists(afs.tlmDir()));
  EXPECT_TRUE(fs::exists(afs.logDir()));
  EXPECT_TRUE(fs::exists(afs.dbDir()));

  // Bank A directories (active by default).
  EXPECT_TRUE(fs::exists(afs.libDir()));
  EXPECT_TRUE(fs::exists(afs.tprmDir()));
  EXPECT_TRUE(fs::exists(afs.binDir()));
  EXPECT_TRUE(fs::exists(afs.rtsDir()));
  EXPECT_TRUE(fs::exists(afs.atsDir()));

  // Bank B directories (inactive).
  EXPECT_TRUE(fs::exists(afs.inactiveLibDir()));
  EXPECT_TRUE(fs::exists(afs.inactiveTprmDir()));
  EXPECT_TRUE(fs::exists(afs.inactiveBinDir()));
  EXPECT_TRUE(fs::exists(afs.inactiveRtsDir()));
  EXPECT_TRUE(fs::exists(afs.inactiveAtsDir()));
}

/** @test Default active bank is A. */
TEST_F(ApexFileSystemTest, DefaultActiveBankIsA) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  EXPECT_EQ(afs.activeBank(), Bank::A);
}

/* ----------------------------- Bank Path Tests ----------------------------- */

/** @test Active bank paths resolve to bank_a by default. */
TEST_F(ApexFileSystemTest, ActiveBankPathsResolveToA) {
  using system_core::filesystem::ATS_DIR;
  using system_core::filesystem::BANK_A_DIR;
  using system_core::filesystem::BIN_DIR;
  using system_core::filesystem::LIB_DIR;
  using system_core::filesystem::RTS_DIR;
  using system_core::filesystem::TPRM_DIR;

  ApexFileSystem afs(root_);

  EXPECT_EQ(afs.libDir(), root_ / BANK_A_DIR / LIB_DIR);
  EXPECT_EQ(afs.tprmDir(), root_ / BANK_A_DIR / TPRM_DIR);
  EXPECT_EQ(afs.binDir(), root_ / BANK_A_DIR / BIN_DIR);
  EXPECT_EQ(afs.rtsDir(), root_ / BANK_A_DIR / RTS_DIR);
  EXPECT_EQ(afs.atsDir(), root_ / BANK_A_DIR / ATS_DIR);
}

/** @test Inactive bank paths resolve to bank_b by default. */
TEST_F(ApexFileSystemTest, InactiveBankPathsResolveToB) {
  using system_core::filesystem::ATS_DIR;
  using system_core::filesystem::BANK_B_DIR;
  using system_core::filesystem::BIN_DIR;
  using system_core::filesystem::LIB_DIR;
  using system_core::filesystem::RTS_DIR;
  using system_core::filesystem::TPRM_DIR;

  ApexFileSystem afs(root_);

  EXPECT_EQ(afs.inactiveLibDir(), root_ / BANK_B_DIR / LIB_DIR);
  EXPECT_EQ(afs.inactiveTprmDir(), root_ / BANK_B_DIR / TPRM_DIR);
  EXPECT_EQ(afs.inactiveBinDir(), root_ / BANK_B_DIR / BIN_DIR);
  EXPECT_EQ(afs.inactiveRtsDir(), root_ / BANK_B_DIR / RTS_DIR);
  EXPECT_EQ(afs.inactiveAtsDir(), root_ / BANK_B_DIR / ATS_DIR);
}

/** @test Non-banked accessors return correct paths. */
TEST_F(ApexFileSystemTest, NonBankedAccessors) {
  using system_core::filesystem::LOG_DIR;
  using system_core::filesystem::TLM_DIR;

  ApexFileSystem afs(root_);

  EXPECT_EQ(afs.tlmDir(), root_ / TLM_DIR);
  EXPECT_EQ(afs.logDir(), root_ / LOG_DIR);
}

/* ----------------------------- Bank Swap Tests ----------------------------- */

/** @test flipActiveBank toggles active bank from A to B. */
TEST_F(ApexFileSystemTest, FlipActiveBankToggle) {
  using system_core::filesystem::BANK_A_DIR;
  using system_core::filesystem::BANK_B_DIR;
  using system_core::filesystem::LIB_DIR;

  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  ASSERT_EQ(afs.activeBank(), Bank::A);
  ASSERT_EQ(afs.libDir(), root_ / BANK_A_DIR / LIB_DIR);

  EXPECT_TRUE(afs.flipActiveBank());
  EXPECT_EQ(afs.activeBank(), Bank::B);
  EXPECT_EQ(afs.libDir(), root_ / BANK_B_DIR / LIB_DIR);
  EXPECT_EQ(afs.inactiveLibDir(), root_ / BANK_A_DIR / LIB_DIR);

  EXPECT_TRUE(afs.flipActiveBank());
  EXPECT_EQ(afs.activeBank(), Bank::A);
}

/** @test Active bank persists across instances via marker file. */
TEST_F(ApexFileSystemTest, ActiveBankPersistsAcrossInstances) {
  {
    ApexFileSystem afs(root_);
    ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));
    EXPECT_TRUE(afs.flipActiveBank());
    EXPECT_EQ(afs.activeBank(), Bank::B);
  }

  // New instance reads the persisted marker.
  ApexFileSystem afs2(root_);
  ASSERT_EQ(afs2.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(afs2.activeBank(), Bank::B);
}

/** @test swapBankFile moves file from inactive to active (first deploy). */
TEST_F(ApexFileSystemTest, SwapBankFileFirstDeploy) {
  using system_core::filesystem::LIB_DIR;

  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  // Place a file in the inactive bank only.
  const fs::path INACTIVE_FILE = afs.inactiveLibDir() / "plugin.so";
  {
    std::ofstream(INACTIVE_FILE) << "v2_binary";
  }
  ASSERT_TRUE(fs::exists(INACTIVE_FILE));

  EXPECT_TRUE(afs.swapBankFile(LIB_DIR, "plugin.so"));

  // File should now be in active bank.
  EXPECT_TRUE(fs::exists(afs.libDir() / "plugin.so"));
  EXPECT_FALSE(fs::exists(afs.inactiveLibDir() / "plugin.so"));
}

/** @test swapBankFile swaps files when both banks have the file. */
TEST_F(ApexFileSystemTest, SwapBankFileBothExist) {
  using system_core::filesystem::TPRM_DIR;

  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  // Place different content in each bank.
  const fs::path ACTIVE_FILE = afs.tprmDir() / "config.tprm";
  const fs::path INACTIVE_FILE = afs.inactiveTprmDir() / "config.tprm";
  {
    std::ofstream(ACTIVE_FILE) << "old_config";
  }
  {
    std::ofstream(INACTIVE_FILE) << "new_config";
  }

  EXPECT_TRUE(afs.swapBankFile(TPRM_DIR, "config.tprm"));

  // Contents should be swapped.
  std::string activeContent, inactiveContent;
  {
    std::ifstream ifs(ACTIVE_FILE);
    std::getline(ifs, activeContent);
  }
  {
    std::ifstream ifs(INACTIVE_FILE);
    std::getline(ifs, inactiveContent);
  }

  EXPECT_EQ(activeContent, "new_config");
  EXPECT_EQ(inactiveContent, "old_config");
}

/** @test swapBankFile returns false when neither bank has the file. */
TEST_F(ApexFileSystemTest, SwapBankFileNeitherExists) {
  using system_core::filesystem::LIB_DIR;

  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  EXPECT_FALSE(afs.swapBankFile(LIB_DIR, "nonexistent.so"));
}

/** @test swapBankFile returns false when only active bank has file. */
TEST_F(ApexFileSystemTest, SwapBankFileOnlyActiveExists) {
  using system_core::filesystem::LIB_DIR;

  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  {
    std::ofstream(afs.libDir() / "active_only.so") << "data";
  }

  EXPECT_FALSE(afs.swapBankFile(LIB_DIR, "active_only.so"));
}

/* ----------------------------- Idempotency ----------------------------- */

/** @test init is idempotent. */
TEST_F(ApexFileSystemTest, InitIdempotent) {
  ApexFileSystem afs(root_);

  EXPECT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  EXPECT_TRUE(fs::exists(afs.libDir()));
  EXPECT_TRUE(fs::exists(afs.tlmDir()));
  EXPECT_TRUE(fs::exists(afs.logDir()));
  EXPECT_TRUE(fs::exists(afs.tprmDir()));
  EXPECT_TRUE(fs::exists(afs.rtsDir()));
  EXPECT_TRUE(fs::exists(afs.atsDir()));
}

/** @test TPRM directory is writable (bank-aware). */
TEST_F(ApexFileSystemTest, TprmDirectoryWritable) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  const fs::path TMP_FILE = afs.tprmDir() / "temp_file.txt";
  {
    std::ofstream(TMP_FILE) << "x";
  }

  EXPECT_TRUE(fs::exists(TMP_FILE));
}

/* ----------------------------- Swap Traceability Tests ----------------------------- */

/** @test init creates swap_history directory. */
TEST_F(ApexFileSystemTest, InitCreatesSwapHistoryDir) {
  using system_core::filesystem::SWAP_HISTORY_DIR;

  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  EXPECT_TRUE(fs::exists(afs.swapHistoryDir()));
  EXPECT_EQ(afs.swapHistoryDir(), root_ / SWAP_HISTORY_DIR);
}

/** @test init creates dedicated swap log. */
TEST_F(ApexFileSystemTest, SwapLogCreated) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  EXPECT_TRUE(fs::exists(afs.swapLogPath()));
  EXPECT_NE(afs.swapLog(), nullptr);
}

/** @test archiveComponentLog copies log and returns archived path. */
TEST_F(ApexFileSystemTest, ArchiveComponentLogBasic) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  // Create a fake component log.
  const fs::path LOG_FILE = afs.coreLogDir() / "TestComp_0.log";
  {
    std::ofstream(LOG_FILE) << "line1\nline2\nline3\n";
  }

  const fs::path RESULT = afs.archiveComponentLog(LOG_FILE, "TestComp", 0);
  EXPECT_FALSE(RESULT.empty());
  EXPECT_TRUE(fs::exists(RESULT));

  // Archived file should contain the same content.
  std::string content;
  {
    std::ifstream ifs(RESULT);
    content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
  }
  EXPECT_NE(content.find("line1"), std::string::npos);

  // Archive should be under swap_history/.
  EXPECT_TRUE(RESULT.string().find("swap_history") != std::string::npos);
}

/** @test archiveComponentLog returns empty path for nonexistent log. */
TEST_F(ApexFileSystemTest, ArchiveComponentLogNonexistent) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  const fs::path RESULT = afs.archiveComponentLog(root_ / "nonexistent.log", "Fake", 0);
  EXPECT_TRUE(RESULT.empty());
}

/** @test archiveComponentLog returns empty path for empty log file. */
TEST_F(ApexFileSystemTest, ArchiveComponentLogEmptyFile) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  const fs::path LOG_FILE = afs.coreLogDir() / "Empty_0.log";
  {
    std::ofstream ofs(LOG_FILE);
  } // Create empty file.

  const fs::path RESULT = afs.archiveComponentLog(LOG_FILE, "Empty", 0);
  EXPECT_TRUE(RESULT.empty());
}

/** @test archiveComponentLog prunes oldest archives beyond max (FIFO). */
TEST_F(ApexFileSystemTest, ArchiveComponentLogFifoPrune) {
  ApexFileSystem afs(root_);
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  const fs::path LOG_FILE = afs.coreLogDir() / "Pruner_0.log";

  // Create 4 archives (max=3 for fast test).
  for (int i = 0; i < 4; ++i) {
    {
      std::ofstream(LOG_FILE) << "iteration " << i;
    }
    const fs::path R = afs.archiveComponentLog(LOG_FILE, "Pruner", 0, 3);
    EXPECT_FALSE(R.empty()) << "Failed on iteration " << i;

    // Small delay to ensure unique timestamps.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
  }

  // Count remaining archives for this component.
  std::size_t count = 0;
  std::error_code ec;
  for (fs::directory_iterator it(afs.swapHistoryDir(), ec), end; !ec && it != end;
       it.increment(ec)) {
    const std::string NAME = it->path().filename().string();
    if (NAME.find("_Pruner_0") != std::string::npos) {
      ++count;
    }
  }

  EXPECT_EQ(count, 3u);
}

/* ----------------------------- Archive Tests ----------------------------- */

/** @test archivePath lives under logs directory with timestamped name. */
TEST_F(ApexFileSystemTest, ArchiveUnderLogs) {
  ApexFSDefaultShim afs(root_, "myfs");
  ASSERT_EQ(afs.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  const fs::path ARCHIVE = afs.archivePath();
  const std::string FILENAME = ARCHIVE.filename().string();

  EXPECT_EQ(ARCHIVE.parent_path(), afs.logDir());
  EXPECT_TRUE(FILENAME.starts_with("myfs_")) << "Got: " << FILENAME;
  EXPECT_TRUE(FILENAME.ends_with(".tar")) << "Got: " << FILENAME;
  EXPECT_EQ(FILENAME.size(), 24u); // "myfs_" (5) + "YYYYMMDD-HHMMSS" (15) + ".tar" (4)
}
