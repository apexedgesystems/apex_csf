/**
 * @file PackedTprm_uTest.cpp
 * @brief Unit tests for PackedTprm structs and constants.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/PackedTprm.hpp"

#include <gtest/gtest.h>

using system_core::system_component::PackedTprmHeader;
using system_core::system_component::PackedTprmIndexEntry;
using system_core::system_component::PackedTprmReader;
using system_core::system_component::TPRM_HEADER_SIZE;
using system_core::system_component::TPRM_INDEX_ENTRY_SIZE;
using system_core::system_component::TPRM_MAGIC;
using system_core::system_component::TPRM_MAX_ENTRIES;
using system_core::system_component::TPRM_VERSION;

/* ----------------------------- Constants Tests ----------------------------- */

/** @test TPRM_MAGIC constant. */
TEST(PackedTprmConstants, MagicBytes) {
  EXPECT_EQ(TPRM_MAGIC[0], 'T');
  EXPECT_EQ(TPRM_MAGIC[1], 'P');
  EXPECT_EQ(TPRM_MAGIC[2], 'R');
  EXPECT_EQ(TPRM_MAGIC[3], 'M');
}

/** @test TPRM_VERSION constant (version 2 = fullUid support). */
TEST(PackedTprmConstants, Version) { EXPECT_EQ(TPRM_VERSION, 2); }

/** @test TPRM_MAX_ENTRIES constant. */
TEST(PackedTprmConstants, MaxEntries) { EXPECT_EQ(TPRM_MAX_ENTRIES, 256U); }

/** @test TPRM_HEADER_SIZE constant. */
TEST(PackedTprmConstants, HeaderSize) { EXPECT_EQ(TPRM_HEADER_SIZE, 8U); }

/** @test TPRM_INDEX_ENTRY_SIZE constant (12 bytes: fullUid[4] + offset[4] + size[4]). */
TEST(PackedTprmConstants, IndexEntrySize) { EXPECT_EQ(TPRM_INDEX_ENTRY_SIZE, 12U); }

/* ----------------------------- PackedTprmHeader Method Tests ----------------------------- */

/** @test PackedTprmHeader has correct size. */
TEST(PackedTprmHeader, SizeMatchesConstant) {
  EXPECT_EQ(sizeof(PackedTprmHeader), TPRM_HEADER_SIZE);
}

/** @test PackedTprmHeader default construction. */
TEST(PackedTprmHeader, DefaultConstruction) {
  const PackedTprmHeader header{};

  EXPECT_EQ(header.magic[0], '\0');
  EXPECT_EQ(header.version, 0);
  EXPECT_EQ(header.count, 0);
}

/** @test PackedTprmHeader::isValid returns true for valid header. */
TEST(PackedTprmHeader, IsValidReturnsTrueForValidHeader) {
  PackedTprmHeader header{};
  header.magic = TPRM_MAGIC;
  header.version = TPRM_VERSION;
  header.count = 5;

  EXPECT_TRUE(header.isValid());
}

/** @test PackedTprmHeader::isValid returns false for invalid magic. */
TEST(PackedTprmHeader, IsValidReturnsFalseForInvalidMagic) {
  PackedTprmHeader header{};
  header.magic = {'B', 'A', 'D', '!'};
  header.version = TPRM_VERSION;
  header.count = 5;

  EXPECT_FALSE(header.isValid());
}

/** @test PackedTprmHeader::isValid returns false for invalid version. */
TEST(PackedTprmHeader, IsValidReturnsFalseForInvalidVersion) {
  PackedTprmHeader header{};
  header.magic = TPRM_MAGIC;
  header.version = 99;
  header.count = 5;

  EXPECT_FALSE(header.isValid());
}

/* ----------------------------- PackedTprmIndexEntry Method Tests ----------------------------- */

/** @test PackedTprmIndexEntry has correct size. */
TEST(PackedTprmIndexEntry, SizeMatchesConstant) {
  EXPECT_EQ(sizeof(PackedTprmIndexEntry), TPRM_INDEX_ENTRY_SIZE);
}

/** @test PackedTprmIndexEntry default construction. */
TEST(PackedTprmIndexEntry, DefaultConstruction) {
  const PackedTprmIndexEntry entry{};

  EXPECT_EQ(entry.fullUid, 0U);
  EXPECT_EQ(entry.offset, 0U);
  EXPECT_EQ(entry.size, 0U);
}

/** @test PackedTprmIndexEntry can store values. */
TEST(PackedTprmIndexEntry, CanStoreValues) {
  PackedTprmIndexEntry entry{};
  entry.fullUid = 0x006500; // componentId=101, instance=0
  entry.offset = 1024;
  entry.size = 512;

  EXPECT_EQ(entry.fullUid, 0x006500U);
  EXPECT_EQ(entry.offset, 1024U);
  EXPECT_EQ(entry.size, 512U);
}

/* ----------------------------- PackedTprmReader Method Tests ----------------------------- */

/** @test PackedTprmReader default construction. */
TEST(PackedTprmReader, DefaultConstruction) {
  const PackedTprmReader reader{};

  EXPECT_EQ(reader.count(), 0U);
  EXPECT_TRUE(reader.entries().empty());
}

/** @test PackedTprmReader::findEntry returns nullptr when empty. */
TEST(PackedTprmReader, FindEntryReturnsNullptrWhenEmpty) {
  const PackedTprmReader reader{};

  EXPECT_EQ(reader.findEntry(0), nullptr);
  EXPECT_EQ(reader.findEntry(101), nullptr);
}

/** @test PackedTprmReader::load fails for non-existent file. */
TEST(PackedTprmReader, LoadFailsForNonExistentFile) {
  PackedTprmReader reader{};
  std::string error;

  bool result = reader.load("/nonexistent/path/to/file.tprm", error);

  EXPECT_FALSE(result);
  EXPECT_FALSE(error.empty());
}
