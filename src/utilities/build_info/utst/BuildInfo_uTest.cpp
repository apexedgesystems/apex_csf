/**
 * @file BuildInfo_uTest.cpp
 * @brief Unit tests for the build identity strings.
 *
 * The values under test are the ones generated for this very build, so the
 * tests check shape and mutual consistency rather than particular values.
 */

#include "src/utilities/build_info/inc/BuildInfo.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

using apex::build_info::branch;
using apex::build_info::buildType;
using apex::build_info::commit;
using apex::build_info::compiler;
using apex::build_info::describe;
using apex::build_info::dirty;
using apex::build_info::project;
using apex::build_info::summary;
using apex::build_info::targetSystem;
using apex::build_info::treeState;
using apex::build_info::version;

namespace {

/// Twelve lowercase hex digits, the shape of commit() and a dirty treeState().
bool isHex12(std::string_view text) {
  if (text.size() != 12) {
    return false;
  }
  for (const char C : text) {
    const auto U = static_cast<unsigned char>(C);
    if (std::isxdigit(U) == 0 || std::isupper(U) != 0) {
      return false;
    }
  }
  return true;
}

bool endsWith(std::string_view text, std::string_view suffix) {
  return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

} // namespace

/** @test Every field is non-empty and the summary line carries the ones it is built from:
 *  project, description, branch, tree state, build type, compiler, target (version and commit
 *  are not in it; the description already names the commit). */
TEST(BuildInfo, SummaryCarriesItsFields) {
  const std::string SUMMARY = summary();
  ASSERT_FALSE(SUMMARY.empty());
  for (const char* field :
       {project(), describe(), branch(), treeState(), buildType(), compiler(), targetSystem()}) {
    ASSERT_STRNE(field, "");
    EXPECT_NE(SUMMARY.find(field), std::string::npos) << field;
  }
  EXPECT_NE(SUMMARY.find("tree="), std::string::npos);
}

/** @test The commit is twelve lowercase hex digits, or "unknown" without repository metadata. */
TEST(BuildInfo, CommitIsTwelveHexDigitsOrUnknown) {
  const std::string_view COMMIT = commit();
  EXPECT_TRUE(COMMIT == "unknown" || isHex12(COMMIT)) << COMMIT;
}

/** @test Tree state is "clean", a twelve-digit diff hash, or "unknown"; dirty() mirrors it. */
TEST(BuildInfo, TreeStateAndDirtyAgree) {
  const std::string_view STATE = treeState();
  EXPECT_TRUE(STATE == "clean" || STATE == "unknown" || isHex12(STATE)) << STATE;
  EXPECT_EQ(dirty(), isHex12(STATE));
}

/** @test describe() takes one of its documented forms, each ending in -dirty exactly when the
 *  tree is dirty: an exact tag, <tag>-<n>-g<hex> with the hex a prefix of commit(), or a bare hex
 *  prefix of commit() where no tag is reachable. The generator pins twelve digits, but any
 *  abbreviation of at least four is a valid form. */
TEST(BuildInfo, DescribeTakesADocumentedForm) {
  if (std::strcmp(commit(), "unknown") == 0) {
    GTEST_SKIP() << "no repository metadata in this build";
  }
  std::string_view described = describe();
  const bool DESCRIBED_DIRTY = endsWith(described, "-dirty");
  EXPECT_EQ(DESCRIBED_DIRTY, dirty()) << described << " vs tree=" << treeState();
  if (DESCRIBED_DIRTY) {
    described.remove_suffix(std::string_view("-dirty").size());
  }
  ASSERT_FALSE(described.empty());

  const std::string_view COMMIT = commit();
  const auto IS_HEX = [](std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](char c) {
      return std::isxdigit(static_cast<unsigned char>(c)) != 0;
    });
  };
  const auto IS_PREFIX_OF_COMMIT = [&](std::string_view hex) {
    return hex.size() >= 4 && hex.size() <= COMMIT.size() && IS_HEX(hex) &&
           COMMIT.substr(0, hex.size()) == hex;
  };
  if (IS_HEX(described)) {
    // No tag reachable: the description is a bare abbreviation of the commit.
    EXPECT_TRUE(IS_PREFIX_OF_COMMIT(described)) << described << " vs commit " << COMMIT;
  } else if (const auto G = described.rfind("-g"); G != std::string_view::npos) {
    EXPECT_TRUE(IS_PREFIX_OF_COMMIT(described.substr(G + 2)))
        << described << " vs commit " << COMMIT;
  }
  // Otherwise an exact tag: there is no commit abbreviation to compare.
}

/** @test The branch is a name, never empty; "HEAD" stands for a detached checkout. */
TEST(BuildInfo, BranchIsNamed) {
  const std::string_view BRANCH = branch();
  EXPECT_FALSE(BRANCH.empty());
  EXPECT_EQ(BRANCH.find(' '), std::string_view::npos) << BRANCH;
}

/** @test The version is dotted digits, the project version CMake was given. */
TEST(BuildInfo, VersionIsDottedDigits) {
  const std::string_view VERSION = version();
  ASSERT_FALSE(VERSION.empty());
  for (const char C : VERSION) {
    EXPECT_TRUE(std::isdigit(static_cast<unsigned char>(C)) != 0 || C == '.') << VERSION;
  }
}

/** @test The compiler reads "<id>-<version>" and the target system "<name>/<processor>". */
TEST(BuildInfo, CompilerAndTargetShapes) {
  EXPECT_NE(std::string_view(compiler()).find('-'), std::string_view::npos) << compiler();
  EXPECT_NE(std::string_view(targetSystem()).find('/'), std::string_view::npos) << targetSystem();
  EXPECT_STRNE(buildType(), "");
}

/** @test Accessors return static storage: the same pointer on every call. */
TEST(BuildInfo, StableStorage) {
  EXPECT_EQ(summary(), summary());
  EXPECT_EQ(describe(), describe());
  EXPECT_EQ(treeState(), treeState());
}
