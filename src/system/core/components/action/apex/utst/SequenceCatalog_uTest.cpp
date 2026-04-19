/**
 * @file SequenceCatalog_uTest.cpp
 * @brief Unit tests for SequenceCatalog.
 */

#include "src/system/core/components/action/apex/inc/SequenceCatalog.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>

using system_core::data::CATALOG_MAX_ENTRIES;
using system_core::data::CatalogEntry;
using system_core::data::SequenceCatalog;
using system_core::data::SequenceType;

/* ----------------------------- Default Construction ----------------------------- */

/** @test Catalog starts empty. */
TEST(SequenceCatalog, DefaultConstruction) {
  SequenceCatalog cat;
  EXPECT_EQ(cat.size(), 0u);
  EXPECT_EQ(cat.rtsCount(), 0u);
  EXPECT_EQ(cat.atsCount(), 0u);
}

/* ----------------------------- Add ----------------------------- */

/** @test Add a single entry. */
TEST(SequenceCatalog, AddEntry) {
  SequenceCatalog cat;

  CatalogEntry e{};
  e.sequenceId = 100;
  e.eventId = 5;
  e.type = SequenceType::RTS;
  e.stepCount = 3;
  e.priority = 50;
  std::snprintf(e.filename, sizeof(e.filename), "rts_100.rts");

  EXPECT_TRUE(cat.add(e));
  EXPECT_EQ(cat.size(), 1u);
  EXPECT_EQ(cat.rtsCount(), 1u);
  EXPECT_EQ(cat.atsCount(), 0u);
}

/** @test Duplicate ID rejected. */
TEST(SequenceCatalog, AddDuplicateRejected) {
  SequenceCatalog cat;

  CatalogEntry e{};
  e.sequenceId = 100;
  e.type = SequenceType::RTS;

  EXPECT_TRUE(cat.add(e));
  EXPECT_FALSE(cat.add(e));
  EXPECT_EQ(cat.size(), 1u);
}

/** @test Add RTS and ATS counted separately. */
TEST(SequenceCatalog, MixedTypes) {
  SequenceCatalog cat;

  CatalogEntry rts{};
  rts.sequenceId = 1;
  rts.type = SequenceType::RTS;
  cat.add(rts);

  CatalogEntry ats{};
  ats.sequenceId = 200;
  ats.type = SequenceType::ATS;
  cat.add(ats);

  EXPECT_EQ(cat.size(), 2u);
  EXPECT_EQ(cat.rtsCount(), 1u);
  EXPECT_EQ(cat.atsCount(), 1u);
}

/* ----------------------------- Lookup ----------------------------- */

/** @test Find by ID returns correct entry. */
TEST(SequenceCatalog, FindById) {
  SequenceCatalog cat;

  for (std::uint16_t i = 0; i < 10; ++i) {
    CatalogEntry e{};
    e.sequenceId = static_cast<std::uint16_t>(100 + i * 7); // Non-sequential IDs
    e.type = SequenceType::RTS;
    e.stepCount = static_cast<std::uint8_t>(i);
    cat.add(e);
  }

  // Find existing
  const auto* found = cat.findById(121); // 100 + 3*7
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->sequenceId, 121u);
  EXPECT_EQ(found->stepCount, 3u);

  // Not found
  EXPECT_EQ(cat.findById(999), nullptr);
  EXPECT_EQ(cat.findById(0), nullptr);
}

/** @test ForEachByEvent invokes callback for matching entries. */
TEST(SequenceCatalog, ForEachByEvent) {
  SequenceCatalog cat;

  CatalogEntry e1{};
  e1.sequenceId = 1;
  e1.eventId = 10;
  e1.type = SequenceType::RTS;
  cat.add(e1);

  CatalogEntry e2{};
  e2.sequenceId = 2;
  e2.eventId = 10; // Same event
  e2.type = SequenceType::RTS;
  cat.add(e2);

  CatalogEntry e3{};
  e3.sequenceId = 3;
  e3.eventId = 20; // Different event
  e3.type = SequenceType::RTS;
  cat.add(e3);

  int matchCount = 0;
  cat.forEachByEvent(10, [&](const CatalogEntry& e) {
    EXPECT_EQ(e.eventId, 10u);
    ++matchCount;
  });
  EXPECT_EQ(matchCount, 2);
}

/* ----------------------------- Filesystem Scan ----------------------------- */

/** @test Scan reads sequence headers from filesystem. */
TEST(SequenceCatalog, ScanDirectory) {
  // Create a temp directory with test RTS files
  const std::filesystem::path TMP = std::filesystem::temp_directory_path() / "catalog_test_rts";
  std::filesystem::create_directories(TMP);

  // Write 3 minimal RTS files (8-byte header + padding to 1352 bytes)
  for (int i = 0; i < 3; ++i) {
    std::array<std::uint8_t, 1352> binary{};
    // Header: sequenceId, eventId, stepCount, repeatCount, type, armed
    const std::uint16_t SEQ_ID = static_cast<std::uint16_t>(50 + i);
    const std::uint16_t EVT_ID = static_cast<std::uint16_t>(i == 0 ? 5 : 0);
    std::memcpy(&binary[0], &SEQ_ID, 2);
    std::memcpy(&binary[2], &EVT_ID, 2);
    binary[4] = static_cast<std::uint8_t>(2 + i); // stepCount
    binary[7] = 1;                                // armed

    const std::string FILENAME = "rts_" + std::to_string(50 + i) + ".rts";
    std::ofstream out(TMP / FILENAME, std::ios::binary);
    out.write(reinterpret_cast<const char*>(binary.data()), binary.size());
  }

  SequenceCatalog cat;
  std::size_t added = cat.scan(TMP, SequenceType::RTS);

  EXPECT_EQ(added, 3u);
  EXPECT_EQ(cat.size(), 3u);
  EXPECT_EQ(cat.rtsCount(), 3u);

  // Verify entries
  const auto* e50 = cat.findById(50);
  ASSERT_NE(e50, nullptr);
  EXPECT_EQ(e50->stepCount, 2u);
  EXPECT_EQ(e50->eventId, 5u);
  EXPECT_TRUE(e50->armed);
  EXPECT_TRUE(e50->binaryLoaded) << "Binary should be cached at scan time";
  EXPECT_EQ(e50->binary.size(), CatalogEntry::HEADER_SIZE + 2 * CatalogEntry::STEP_SIZE)
      << "Cached binary should be header + stepCount * step size";

  // Verify cached binary header matches metadata
  std::uint16_t cachedId = 0;
  std::memcpy(&cachedId, e50->binary.data(), 2);
  EXPECT_EQ(cachedId, 50u) << "Cached binary header should match sequenceId";

  const auto* e51 = cat.findById(51);
  ASSERT_NE(e51, nullptr);
  EXPECT_EQ(e51->stepCount, 3u);
  EXPECT_EQ(e51->eventId, 0u);

  // Cleanup
  std::filesystem::remove_all(TMP);
}

/** @test Scan non-existent directory returns 0. */
TEST(SequenceCatalog, ScanNonExistentDir) {
  SequenceCatalog cat;
  EXPECT_EQ(cat.scan("/nonexistent/path", SequenceType::RTS), 0u);
}

/* ----------------------------- Clear ----------------------------- */

/** @test Clear removes all entries. */
TEST(SequenceCatalog, Clear) {
  SequenceCatalog cat;

  CatalogEntry e{};
  e.sequenceId = 1;
  e.type = SequenceType::RTS;
  cat.add(e);
  EXPECT_EQ(cat.size(), 1u);

  cat.clear();
  EXPECT_EQ(cat.size(), 0u);
  EXPECT_EQ(cat.rtsCount(), 0u);
  EXPECT_EQ(cat.atsCount(), 0u);
  EXPECT_EQ(cat.findById(1), nullptr);
}

/* ----------------------------- AbortEventId ----------------------------- */

/** @test CatalogEntry abortEventId defaults to 0. */
TEST(SequenceCatalog, AbortEventIdDefaultsToZero) {
  CatalogEntry e{};
  EXPECT_EQ(e.abortEventId, 0U);
}

/** @test CatalogEntry abortEventId is preserved through add/find. */
TEST(SequenceCatalog, AbortEventIdPreservedThroughCatalog) {
  SequenceCatalog cat;

  CatalogEntry e{};
  e.sequenceId = 42;
  e.type = SequenceType::RTS;
  e.abortEventId = 777;
  cat.add(e);

  const auto* found = cat.findById(42);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->abortEventId, 777U);
}

/** @test CatalogEntry abortEventId can be modified via findByIdMut. */
TEST(SequenceCatalog, AbortEventIdMutableUpdate) {
  SequenceCatalog cat;

  CatalogEntry e{};
  e.sequenceId = 10;
  e.type = SequenceType::RTS;
  e.abortEventId = 0;
  cat.add(e);

  auto* entry = cat.findByIdMut(10);
  ASSERT_NE(entry, nullptr);
  entry->abortEventId = 500;
  EXPECT_EQ(cat.findById(10)->abortEventId, 500U);
}

/* ----------------------------- ExclusionGroup ----------------------------- */

/** @test CatalogEntry exclusionGroup defaults to 0. */
TEST(SequenceCatalog, ExclusionGroupDefaultsToZero) {
  CatalogEntry e{};
  EXPECT_EQ(e.exclusionGroup, 0U);
}

/** @test CatalogEntry exclusionGroup is preserved through add/find. */
TEST(SequenceCatalog, ExclusionGroupPreservedThroughCatalog) {
  SequenceCatalog cat;

  CatalogEntry e{};
  e.sequenceId = 50;
  e.type = SequenceType::RTS;
  e.exclusionGroup = 3;
  cat.add(e);

  const auto* found = cat.findById(50);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->exclusionGroup, 3U);
}

/** @test Multiple entries in the same exclusion group can be found. */
TEST(SequenceCatalog, MultipleEntriesSameExclusionGroup) {
  SequenceCatalog cat;

  CatalogEntry e1{};
  e1.sequenceId = 10;
  e1.type = SequenceType::RTS;
  e1.exclusionGroup = 5;
  cat.add(e1);

  CatalogEntry e2{};
  e2.sequenceId = 20;
  e2.type = SequenceType::RTS;
  e2.exclusionGroup = 5;
  cat.add(e2);

  CatalogEntry e3{};
  e3.sequenceId = 30;
  e3.type = SequenceType::RTS;
  e3.exclusionGroup = 0; // Not in any group
  cat.add(e3);

  EXPECT_EQ(cat.findById(10)->exclusionGroup, 5U);
  EXPECT_EQ(cat.findById(20)->exclusionGroup, 5U);
  EXPECT_EQ(cat.findById(30)->exclusionGroup, 0U);
}
