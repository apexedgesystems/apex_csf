/**
 * @file NullFileSystem_uTest.cpp
 * @brief Unit tests for NullFileSystem.
 */

#include "src/system/core/components/filesystem/lite/inc/NullFileSystem.hpp"

#include <gtest/gtest.h>

using system_core::filesystem::lite::NullFileSystem;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Default construction creates filesystem with expected state. */
TEST(NullFileSystem_DefaultConstruction, InitialState) {
  NullFileSystem fs;

  EXPECT_EQ(fs.componentId(), 2);
  EXPECT_STREQ(fs.componentName(), "NullFileSystem");
  EXPECT_EQ(fs.componentType(), system_core::system_component::ComponentType::CORE);
  EXPECT_STREQ(fs.label(), "NULL_FS");
  EXPECT_FALSE(fs.isInitialized());
  EXPECT_FALSE(fs.isRegistered());
  EXPECT_EQ(fs.status(), 0);
}

/* ----------------------------- Lifecycle Tests ----------------------------- */

/** @test init() succeeds and sets initialized flag. */
TEST(NullFileSystem_Lifecycle, InitSucceeds) {
  NullFileSystem fs;

  const std::uint8_t RESULT = fs.init();

  EXPECT_EQ(RESULT, 0);
  EXPECT_TRUE(fs.isInitialized());
}

/** @test reset() clears initialized flag. */
TEST(NullFileSystem_Lifecycle, ResetClearsState) {
  NullFileSystem fs;
  (void)fs.init();

  fs.reset();

  EXPECT_FALSE(fs.isInitialized());
}

/* ----------------------------- Registration Tests ----------------------------- */

/** @test setInstanceIndex() computes fullUid correctly. */
TEST(NullFileSystem_Registration, SetInstanceIndex) {
  NullFileSystem fs;

  fs.setInstanceIndex(1);

  EXPECT_TRUE(fs.isRegistered());
  EXPECT_EQ(fs.instanceIndex(), 1);
  // fullUid = (componentId << 8) | instanceIndex = (2 << 8) | 1 = 513
  EXPECT_EQ(fs.fullUid(), (2U << 8) | 1U);
}

/* ----------------------------- Filesystem Operations Tests ----------------------------- */

/** @test exists() always returns false. */
TEST(NullFileSystem_Operations, ExistsAlwaysFalse) {
  NullFileSystem fs;
  (void)fs.init();

  EXPECT_FALSE(fs.exists("/any/path"));
  EXPECT_FALSE(fs.exists(""));
  EXPECT_FALSE(fs.exists(nullptr));
}

/** @test createDirectory() always returns true. */
TEST(NullFileSystem_Operations, CreateDirectoryAlwaysTrue) {
  NullFileSystem fs;
  (void)fs.init();

  EXPECT_TRUE(fs.createDirectory("/any/path"));
  EXPECT_TRUE(fs.createDirectory(""));
}

/** @test writeFile() always returns true. */
TEST(NullFileSystem_Operations, WriteFileAlwaysTrue) {
  NullFileSystem fs;
  (void)fs.init();

  const char DATA[] = "test data";
  EXPECT_TRUE(fs.writeFile("/any/file", DATA, sizeof(DATA)));
  EXPECT_TRUE(fs.writeFile("/any/file", nullptr, 0));
}

/** @test readFile() always returns 0 bytes. */
TEST(NullFileSystem_Operations, ReadFileReturnsZero) {
  NullFileSystem fs;
  (void)fs.init();

  char buffer[64];
  const std::size_t RESULT = fs.readFile("/any/file", buffer, sizeof(buffer));

  EXPECT_EQ(RESULT, 0);
}

/** @test deleteFile() always returns true. */
TEST(NullFileSystem_Operations, DeleteFileAlwaysTrue) {
  NullFileSystem fs;
  (void)fs.init();

  EXPECT_TRUE(fs.deleteFile("/any/file"));
  EXPECT_TRUE(fs.deleteFile(""));
}

/* ----------------------------- IComponent Interface Tests ----------------------------- */

/** @test NullFileSystem implements IComponent interface. */
TEST(NullFileSystem_Interface, ImplementsIComponent) {
  NullFileSystem fs;
  system_core::system_component::IComponent* iface = &fs;

  EXPECT_EQ(iface->componentId(), 2);
  EXPECT_STREQ(iface->componentName(), "NullFileSystem");
  EXPECT_EQ(iface->componentType(), system_core::system_component::ComponentType::CORE);
  EXPECT_EQ(iface->init(), 0);
  EXPECT_TRUE(iface->isInitialized());
}
