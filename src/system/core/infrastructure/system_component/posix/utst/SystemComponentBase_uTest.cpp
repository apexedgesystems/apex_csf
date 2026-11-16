/**
 * @file SystemComponentBase_uTest.cpp
 * @brief Unit tests for system_core::system_component::SystemComponentBase.
 *
 * Notes:
 *  - Tests are platform-agnostic.
 *  - Uses a minimal DummyComponent to exercise the base class.
 *  - Tests template method pattern: init() → doInit().
 *  - Tests configuration requirement: init() fails without setConfigured(true).
 */

#include "src/system/core/infrastructure/system_component/posix/inc/SystemComponentBase.hpp"
#include "src/system/core/infrastructure/system_component/base/inc/SystemComponentStatus.hpp"
#include "src/system/core/infrastructure/logs/inc/SystemLog.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>

using system_core::system_component::Status;
using system_core::system_component::SystemComponentBase;

/** @brief Minimal derived to exercise init + accessors. */
class DummyComponent : public SystemComponentBase {
public:
  DummyComponent() noexcept = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 999; }
  [[nodiscard]] const char* componentName() const noexcept override { return "DummyComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "DUMMY"; }

  /** @brief Mark as configured for testing (exposes protected method). */
  void configure() noexcept { setConfigured(true); }

  /** @brief Track if doInit was called. */
  bool doInitCalled() const noexcept { return doInitCalled_; }

  /** @brief Track if doReset was called. */
  bool doResetCalled() const noexcept { return doResetCalled_; }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    doInitCalled_ = true;
    return static_cast<std::uint8_t>(Status::SUCCESS);
  }

  void doReset() noexcept override { doResetCalled_ = true; }

private:
  bool doInitCalled_{false};
  bool doResetCalled_{false};
};

/** @brief Component that fails during doInit. */
class FailingComponent : public SystemComponentBase {
public:
  FailingComponent() noexcept = default;

  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 998; }
  [[nodiscard]] const char* componentName() const noexcept override { return "FailingComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "FAILING"; }

  void configure() noexcept { setConfigured(true); }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override {
    setLastError("init failed for test");
    return static_cast<std::uint8_t>(Status::ERROR_PARAM);
  }
};

/** @brief Component that writes to its own log. */
class LoggingComponent : public SystemComponentBase {
public:
  [[nodiscard]] std::uint16_t componentId() const noexcept override { return 997; }
  [[nodiscard]] const char* componentName() const noexcept override { return "LoggingComponent"; }
  [[nodiscard]] const char* label() const noexcept override { return "LOGGING"; }

  [[nodiscard]] bool hasLog() const noexcept { return componentLog() != nullptr; }

  /// One INFO line and a flush; the log's status for the pair.
  [[nodiscard]] logs::Status logLine(const std::string& message) noexcept {
    const logs::Status WRITTEN = componentLog()->info(label(), message);
    const logs::Status FLUSHED = componentLog()->flush();
    return WRITTEN != logs::Status::OK ? WRITTEN : FLUSHED;
  }

protected:
  [[nodiscard]] std::uint8_t doInit() noexcept override { return 0; }
};

/// A fresh directory under the system temp directory, removed with the object.
class TempDir {
public:
  TempDir() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<unsigned long long> dist;
    path_ = std::filesystem::temp_directory_path() / ("syscomp_log_" + std::to_string(dist(gen)));
    std::filesystem::create_directory(path_);
  }
  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

static std::string slurp(const std::filesystem::path& path) {
  std::ifstream ifs(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

/* ----------------------------- Default Construction ----------------------------- */

/** @test Defaults before init() are sane. */
TEST(SystemComponentBaseTest, DefaultsBeforeInit) {
  DummyComponent c{};
  EXPECT_FALSE(c.isInitialized());
  EXPECT_FALSE(c.isConfigured());
  EXPECT_EQ(c.status(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_STREQ(c.label(), "DUMMY");
  EXPECT_EQ(c.lastError(), nullptr);
}

/* ----------------------------- SystemComponentBase Method Tests ----------------------------- */

/** @test init() fails with ERROR_NOT_CONFIGURED when not configured. */
TEST(SystemComponentBaseTest, InitFailsWithoutConfiguration) {
  DummyComponent c{};
  EXPECT_FALSE(c.isConfigured());

  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_NOT_CONFIGURED));
  EXPECT_FALSE(c.isInitialized());
  EXPECT_FALSE(c.doInitCalled());    // doInit should not be called
  EXPECT_NE(c.lastError(), nullptr); // Should have error context
}

/** @test isConfigured() returns true after setConfigured(true). */
TEST(SystemComponentBaseTest, SetConfiguredMarksConfigured) {
  DummyComponent c{};
  EXPECT_FALSE(c.isConfigured());

  c.configure();
  EXPECT_TRUE(c.isConfigured());
}

/** @test init() succeeds when configured and calls doInit(). */
TEST(SystemComponentBaseTest, InitSucceedsWhenConfigured) {
  DummyComponent c{};
  c.configure();
  EXPECT_TRUE(c.isConfigured());

  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
  EXPECT_TRUE(c.doInitCalled());
  EXPECT_EQ(c.lastError(), nullptr); // No error on success
}

/** @test init() is idempotent (second call returns SUCCESS without re-init). */
TEST(SystemComponentBaseTest, InitIsIdempotent) {
  DummyComponent c{};
  c.configure();

  EXPECT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());

  // Second call should return SUCCESS without calling doInit again
  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
}

/** @test init() propagates failure status from doInit(). */
TEST(SystemComponentBaseTest, InitPropagatesDoInitFailure) {
  FailingComponent c{};
  c.configure();

  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::ERROR_PARAM));
  EXPECT_FALSE(c.isInitialized());   // Should not be marked initialized on failure
  EXPECT_NE(c.lastError(), nullptr); // Should have error context
}

/** @test reset() clears initialized flag and calls doReset(). */
TEST(SystemComponentBaseTest, ResetClearsInitialized) {
  DummyComponent c{};
  c.configure();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());

  c.reset();
  EXPECT_FALSE(c.isInitialized());
  EXPECT_TRUE(c.doResetCalled());
  EXPECT_TRUE(c.isConfigured());     // Configuration preserved
  EXPECT_EQ(c.lastError(), nullptr); // Error context cleared
}

/** @test reset() allows re-initialization. */
TEST(SystemComponentBaseTest, ResetAllowsReInit) {
  DummyComponent c{};
  c.configure();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::SUCCESS));

  c.reset();
  EXPECT_FALSE(c.isInitialized());

  // Should be able to init again
  const std::uint8_t CODE = c.init();
  EXPECT_EQ(CODE, static_cast<std::uint8_t>(Status::SUCCESS));
  EXPECT_TRUE(c.isInitialized());
}

/** @test reset() resets status to SUCCESS. */
TEST(SystemComponentBaseTest, ResetResetsStatus) {
  FailingComponent c{};
  c.configure();
  ASSERT_EQ(c.init(), static_cast<std::uint8_t>(Status::ERROR_PARAM));

  c.reset();
  EXPECT_EQ(c.status(), static_cast<std::uint8_t>(Status::SUCCESS));
}

/* ----------------------------- Determinism Tests ----------------------------- */

/** @test noexcept contracts for public API. */
TEST(SystemComponentBaseTest, NoexceptContracts) {
  static_assert(noexcept(std::declval<const DummyComponent&>().status()),
                "status() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().isInitialized()),
                "isInitialized() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().isConfigured()),
                "isConfigured() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().label()),
                "label() should be noexcept");
  static_assert(noexcept(std::declval<const DummyComponent&>().lastError()),
                "lastError() should be noexcept");
  static_assert(noexcept(std::declval<DummyComponent&>().init()), "init() should be noexcept");
  static_assert(noexcept(std::declval<DummyComponent&>().reset()), "reset() should be noexcept");
  SUCCEED();
}

/* ----------------------------- Component Log ----------------------------- */

/** @test initComponentLog() creates the type subdirectory and the log takes lines. */
TEST(SystemComponentBaseTest, InitComponentLogCreatesItsSubdirectory) {
  const TempDir DIR;
  LoggingComponent comp;
  comp.setInstanceIndex(3);

  ASSERT_TRUE(comp.initComponentLog(DIR.path()));
  ASSERT_TRUE(comp.hasLog());
  EXPECT_EQ(comp.logLine("first line"), logs::Status::OK);

  const auto LOG_FILE = DIR.path() / "core" / "LoggingComponent_3.log";
  EXPECT_EQ(comp.componentLogPath(DIR.path()), LOG_FILE);
  ASSERT_TRUE(std::filesystem::exists(LOG_FILE)) << LOG_FILE;
  const std::string CONTENT = slurp(LOG_FILE);
  EXPECT_NE(CONTENT.find(" INFO: LOGGING - first line\n"), std::string::npos) << CONTENT;
}

/** @test initComponentLog() reports a log that could not open, and installs it all the same. */
TEST(SystemComponentBaseTest, InitComponentLogReportsAFailedOpen) {
  const TempDir DIR;
  // A regular file where the log tree's root should be: no subdirectory can be made under it.
  const auto NOT_A_DIR = DIR.path() / "occupied";
  std::ofstream(NOT_A_DIR) << "x";

  LoggingComponent comp;
  comp.setInstanceIndex(0);
  EXPECT_FALSE(comp.initComponentLog(NOT_A_DIR));

  // Callers that write without a null check still have a log to call; it drops the line.
  ASSERT_TRUE(comp.hasLog());
  EXPECT_EQ(comp.logLine("dropped"), logs::Status::ERROR_OPEN);
  EXPECT_EQ(slurp(NOT_A_DIR), "x");
}
