/**
 * @file PluginLoader_uTest.cpp
 * @brief Unit tests for PluginLoader's refusals and the error text it keeps.
 *
 * The libraries under plugins/ each pass the ABI gate and lack one factory
 * symbol; APEX_TEST_PLUGIN_DIR is where the build puts them.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/PluginLoader.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

using system_core::system_component::PluginLoader;

namespace {

constexpr std::uint8_t DLOPEN_FAILED = 17;
constexpr std::uint8_t FACTORY_NOT_FOUND = 18;
constexpr std::uint8_t ABI_MISMATCH = 20;

std::filesystem::path pluginPath(const char* name) {
  return std::filesystem::path(APEX_TEST_PLUGIN_DIR) / name;
}

/// Load a library that lacks SYMBOL and return the error text the loader kept.
std::string errorAfterMissingFactory(const char* library, const char* symbol) {
  const auto PATH = pluginPath(library);
  EXPECT_TRUE(std::filesystem::exists(PATH)) << PATH;

  PluginLoader loader;
  EXPECT_EQ(loader.load(PATH), FACTORY_NOT_FOUND);
  EXPECT_FALSE(loader.isLoaded());
  EXPECT_EQ(loader.component(), nullptr);
  if (loader.lastError() == nullptr) {
    ADD_FAILURE() << "no error text after a refused load";
    return {};
  }
  // The loader has closed the library by now; the text must be its own.
  const std::string TEXT = loader.lastError();
  EXPECT_NE(TEXT.find(symbol), std::string::npos) << TEXT;
  return TEXT;
}

} // namespace

/** @test A library without the create factory is refused, and the reason stays readable. */
TEST(PluginLoaderTest, MissingCreateFactoryLeavesAReadableError) {
  const std::string TEXT =
      errorAfterMissingFactory("SystemComponentTestPlugin_NoCreate.so", "apex_create_component");
  EXPECT_FALSE(TEXT.empty());
}

/** @test A library without the destroy factory is refused, and the reason stays readable. */
TEST(PluginLoaderTest, MissingDestroyFactoryLeavesAReadableError) {
  const std::string TEXT =
      errorAfterMissingFactory("SystemComponentTestPlugin_NoDestroy.so", "apex_destroy_component");
  EXPECT_FALSE(TEXT.empty());
}

/**
 * @test A library built for another plugin ABI version is refused before
 *       either factory is called, and the reason says so.
 */
TEST(PluginLoaderTest, AnotherAbiVersionIsRefusedBeforeAnyFactoryCall) {
  const auto PATH = pluginPath("SystemComponentTestPlugin_OtherAbi.so");
  ASSERT_TRUE(std::filesystem::exists(PATH)) << PATH;

  PluginLoader loader;
  EXPECT_EQ(loader.load(PATH), ABI_MISMATCH);
  EXPECT_FALSE(loader.isLoaded());
  EXPECT_EQ(loader.component(), nullptr);
  ASSERT_NE(loader.lastError(), nullptr);
  EXPECT_NE(std::string(loader.lastError()).find("ABI version mismatch"), std::string::npos)
      << loader.lastError();
}

/** @test A path that cannot be opened is refused with the linker's reason. */
TEST(PluginLoaderTest, UnopenablePathLeavesAReadableError) {
  PluginLoader loader;
  EXPECT_EQ(loader.load(pluginPath("no_such_plugin.so")), DLOPEN_FAILED);
  EXPECT_FALSE(loader.isLoaded());
  ASSERT_NE(loader.lastError(), nullptr);
  EXPECT_NE(std::string(loader.lastError()).find("no_such_plugin.so"), std::string::npos);
}

/** @test The error text moves with the loader. */
TEST(PluginLoaderTest, ErrorTextMovesWithTheLoader) {
  PluginLoader first;
  ASSERT_EQ(first.load(pluginPath("SystemComponentTestPlugin_NoCreate.so")), FACTORY_NOT_FOUND);
  ASSERT_NE(first.lastError(), nullptr);
  const std::string TEXT = first.lastError();

  PluginLoader constructed(std::move(first));
  ASSERT_NE(constructed.lastError(), nullptr);
  EXPECT_EQ(std::string(constructed.lastError()), TEXT);

  PluginLoader assigned;
  assigned = std::move(constructed);
  ASSERT_NE(assigned.lastError(), nullptr);
  EXPECT_EQ(std::string(assigned.lastError()), TEXT);
}

/** @test A fresh loader has nothing loaded and no error. */
TEST(PluginLoaderTest, FreshLoaderHasNoError) {
  const PluginLoader LOADER;
  EXPECT_FALSE(LOADER.isLoaded());
  EXPECT_EQ(LOADER.component(), nullptr);
  EXPECT_EQ(LOADER.lastError(), nullptr);
}
