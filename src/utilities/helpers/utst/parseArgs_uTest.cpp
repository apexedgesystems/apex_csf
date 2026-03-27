/**
 * @file parseArgs_uTest.cpp
 * @brief Unit tests for apex::helpers::args::parseArgs argument parsing.
 *
 * Coverage:
 *  - Single and multi-value flags
 *  - Required flags (error/no-error paths)
 *  - Insufficient values for a flag
 *  - Multiple required flags with one missing
 *  - No arguments provided
 *  - Order independence
 *  - Unknown flags ignored
 *  - Zero-arity flags (nargs == 0)
 *  - Duplicate flag occurrences (last wins)
 *  - Preexisting ParsedArgs entries are cleared/overwritten
 *  - Empty map with non-empty args (no matches ⇒ success, no output)
 */

#include "src/utilities/helpers/inc/Utilities.hpp"
#include "src/utilities/compatibility/inc/compat_span.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string_view>
#include <vector>

using ::testing::Eq;

namespace { // Helpers ---------------------------------------------------------

/**
 * @brief Convert a vector<string> to apex::compat::rospan<string_view> without copies.
 * @note The returned span references @p views; @p views must outlive the call that uses it.
 */
apex::compat::rospan<std::string_view> makeArgsView(const std::vector<std::string>& src,
                                                    std::vector<std::string_view>& views) {
  views.clear();
  views.reserve(src.size());
  for (const auto& s : src)
    views.emplace_back(s);
  return apex::compat::rospan<std::string_view>(views.data(), views.size());
}

} // namespace

// Fixture ---------------------------------------------------------------------

class ParseArgsTest : public ::testing::Test {
protected:
  apex::helpers::args::ParsedArgs parsed_;
  std::string errorMessage_;
  std::optional<std::reference_wrapper<std::string>> errorOpt_;

  void SetUp() override {
    parsed_.clear();
    errorMessage_.clear();
    errorOpt_ = std::ref(errorMessage_);
  }
};

// -----------------------------------------------------------------------------

/**
 * @test Single argument parsed successfully.
 */
TEST_F(ParseArgsTest, SingleArgumentSuccess) {
  std::vector<std::string> args = {"--foo", "value1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{1, {"--foo", 1, false}}};

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[1].size(), 1u);
  EXPECT_THAT(parsed_[1][0], Eq("value1"));
}

/**
 * @test Multi-value argument (nargs > 1) is parsed into multiple entries.
 */
TEST_F(ParseArgsTest, MultiValueArgumentSuccess) {
  std::vector<std::string> args = {"--foo", "val1", "val2", "val3"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{1, {"--foo", 3, false}}};

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[1].size(), 3u);
  EXPECT_THAT(parsed_[1][0], Eq("val1"));
  EXPECT_THAT(parsed_[1][1], Eq("val2"));
  EXPECT_THAT(parsed_[1][2], Eq("val3"));
}

/**
 * @test Required argument missing produces an error message.
 */
TEST_F(ParseArgsTest, MissingRequiredArgument_WithError) {
  std::vector<std::string> args = {"--bar", "value1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {
      {1, {"--foo", 1, true}}, // required but missing
      {2, {"--bar", 1, false}},
  };

  const bool OK =
      apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_, errorOpt_);

  ASSERT_FALSE(OK);
  EXPECT_THAT(errorMessage_, Eq("Missing required argument: key='1', flag='--foo'"));
}

/**
 * @test Required argument missing returns false even when no error string is provided.
 */
TEST_F(ParseArgsTest, MissingRequiredArgument_NoError) {
  std::vector<std::string> args = {"--bar", "value1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{1, {"--foo", 1, true}}}; // required but missing

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_FALSE(OK);
}

/**
 * @test Argument with insufficient values (nargs exceeded) yields error text.
 */
TEST_F(ParseArgsTest, MultiValueArgumentTooFewValues_WithError) {
  std::vector<std::string> args = {"--foo", "val1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{1, {"--foo", 3, false}}}; // expects 3 values, only 1 provided

  const bool OK =
      apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_, errorOpt_);

  ASSERT_FALSE(OK);
  EXPECT_THAT(errorMessage_, Eq("Argument out of bounds: expected 3 values for flag '--foo'"));
}

/**
 * @test Multiple required arguments with one missing returns false and points at the missing one.
 */
TEST_F(ParseArgsTest, MultipleRequiredArguments_OneMissing) {
  std::vector<std::string> args = {"--bar", "value1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {
      {1, {"--foo", 1, true}}, // missing
      {2, {"--bar", 1, true}}, // present
  };

  const bool OK =
      apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_, errorOpt_);

  ASSERT_FALSE(OK);
  EXPECT_THAT(errorMessage_, Eq("Missing required argument: key='1', flag='--foo'"));
}

/**
 * @test No arguments provided emits a clear error message.
 */
TEST_F(ParseArgsTest, NoArgumentsProvided) {
  std::vector<std::string> args;
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{1, {"--foo", 1, true}}};

  const bool OK =
      apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_, errorOpt_);

  ASSERT_FALSE(OK);
  EXPECT_THAT(errorMessage_, Eq("No arguments provided"));
}

/**
 * @test Success path without providing the optional error parameter.
 */
TEST_F(ParseArgsTest, SuccessWithoutSettingError) {
  std::vector<std::string> args = {"--foo", "value1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{1, {"--foo", 1, false}}};

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[1].size(), 1u);
  EXPECT_THAT(parsed_[1][0], Eq("value1"));
}

/**
 * @test Order of arguments does not affect parsing results.
 */
TEST_F(ParseArgsTest, ArgumentOrderDoesNotMatter) {
  std::vector<std::string> args = {"--bar", "valB", "--foo", "valA"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {
      {1, {"--foo", 1, false}},
      {2, {"--bar", 1, false}},
  };

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[1].size(), 1u);
  EXPECT_THAT(parsed_[1][0], Eq("valA"));
  ASSERT_EQ(parsed_[2].size(), 1u);
  EXPECT_THAT(parsed_[2][0], Eq("valB"));
}

/**
 * @test Unknown flags are ignored and do not affect the result.
 */
TEST_F(ParseArgsTest, UnknownFlagsAreIgnored) {
  std::vector<std::string> args = {"--unknown", "x", "--foo", "ok"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{7, {"--foo", 1, false}}};

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_.count(7), 1u);
  ASSERT_EQ(parsed_[7].size(), 1u);
  EXPECT_THAT(parsed_[7][0], Eq("ok"));
}

/**
 * @test Zero-arity flag (nargs == 0) consumes no following tokens.
 */
TEST_F(ParseArgsTest, ZeroArityFlag) {
  std::vector<std::string> args = {"--enable", "--foo", "v"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {
      {1, {"--enable", 0, false}},
      {2, {"--foo", 1, false}},
  };

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[1].size(), 0u); // no values captured
  ASSERT_EQ(parsed_[2].size(), 1u);
  EXPECT_THAT(parsed_[2][0], Eq("v"));
}

/**
 * @test Duplicate flag occurrences: later occurrence overwrites earlier values.
 *       Note: parser is fixed-arity; each occurrence must provide nargs values.
 */
TEST_F(ParseArgsTest, DuplicateFlagOverwrites) {
  // Arrange: first --foo has two values; second --foo also has two values.
  std::vector<std::string> args = {"--foo", "a", "x", "--foo", "b", "c"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{3, {"--foo", 2, false}}};

  // Act
  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  // Assert: success; last occurrence wins
  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[3].size(), 2u);
  EXPECT_THAT(parsed_[3][0], Eq("b"));
  EXPECT_THAT(parsed_[3][1], Eq("c"));
}

/**
 * @test Preexisting ParsedArgs entries are cleared/overwritten when the same key is parsed.
 */
TEST_F(ParseArgsTest, PreexistingParsedArgsAreCleared) {
  // Seed with old content for key 5
  parsed_[5] = {"oldA", "oldB"};

  std::vector<std::string> args = {"--bar", "new1"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {{5, {"--bar", 1, false}}};

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  ASSERT_EQ(parsed_[5].size(), 1u);
  EXPECT_THAT(parsed_[5][0], Eq("new1"));
}

/**
 * @test Empty map with non-empty args: nothing matches, but this is not an error.
 */
TEST_F(ParseArgsTest, EmptyMapWithArgsSucceeds_NoOutput) {
  std::vector<std::string> args = {"--foo", "x", "--bar", "y"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map; // empty

  const bool OK = apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_);

  ASSERT_TRUE(OK);
  EXPECT_TRUE(parsed_.empty());
}

/**
 * @test Fixed-arity semantics: a token that looks like a flag DOES NOT delimit values.
 *       The parser consumes the next N tokens literally as values.
 */
TEST_F(ParseArgsTest, FlagTokenInsideValuesIsTreatedAsValue) {
  std::vector<std::string> args = {"--foo", "a", "--bar", "x"};
  std::vector<std::string_view> views;
  apex::helpers::args::ArgMap map = {
      {1, {"--foo", 2, false}}, // consumes "a" and "--bar" as values
      {2, {"--bar", 1, false}}, // will not be matched (used as value)
  };

  const bool OK =
      apex::helpers::args::parseArgs(makeArgsView(args, views), map, parsed_, errorOpt_);
  ASSERT_TRUE(OK);

  // --foo captured two values, second one is literally "--bar"
  ASSERT_EQ(parsed_.count(1), 1u);
  ASSERT_EQ(parsed_[1].size(), 2u);
  EXPECT_THAT(parsed_[1][0], Eq("a"));
  EXPECT_THAT(parsed_[1][1], Eq("--bar"));

  // "--bar" was not parsed as a flag (it was consumed as a value)
  EXPECT_EQ(parsed_.count(2), 0u);
  EXPECT_TRUE(errorMessage_.empty());
}
