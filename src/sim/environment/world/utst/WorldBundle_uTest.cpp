/**
 * @file WorldBundle_uTest.cpp
 * @brief Unit tests for the world bundle container.
 *
 * Round-trip, determinism, and one test per corruption verdict: every
 * WorldBundleCheck a loader can report is pinned by a vector here.
 */

#include "src/sim/environment/world/inc/WorldBundle.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace sim::environment::world;

namespace {

/* ----------------------------- Fixture ----------------------------- */

class WorldBundleTest : public ::testing::Test {
protected:
  void SetUp() override {
    dir_ = std::filesystem::temp_directory_path() /
           ("world_utst_" + std::to_string(::getpid()) + "_" + std::to_string(counter_++));
    std::filesystem::create_directories(dir_);
  }
  void TearDown() override {
    std::error_code ec;
    std::filesystem::remove_all(dir_, ec);
  }

  /// Write a source artifact with deterministic content.
  std::filesystem::path makeSource(const char* name, std::size_t size, std::uint8_t seed) {
    const auto P = dir_ / name;
    std::ofstream out(P, std::ios::binary);
    for (std::size_t i = 0; i < size; ++i) {
      out.put(static_cast<char>((seed + i * 7) & 0xFF));
    }
    return P;
  }

  /// Standard three-role source set.
  std::vector<WorldEntrySource> threeSources() {
    return {
        {WorldEntryRole::GRAVITY, ".grav", makeSource("g.grav", 3600, 1), 0x1111},
        {WorldEntryRole::TERRAIN, ".htile", makeSource("t.htile", 517, 2), 0x2222},
        {WorldEntryRole::ATMOSPHERE, ".atm", makeSource("a.atm", 288, 3), 0x3333},
    };
  }

  /// Flip one byte at an absolute file offset.
  static void flipByte(const std::filesystem::path& p, std::uint64_t off) {
    std::FILE* f = std::fopen(p.string().c_str(), "rb+");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(std::fseek(f, static_cast<long>(off), SEEK_SET), 0);
    const int C = std::fgetc(f);
    ASSERT_NE(C, EOF);
    ASSERT_EQ(std::fseek(f, static_cast<long>(off), SEEK_SET), 0);
    ASSERT_NE(std::fputc((C ^ 0xFF) & 0xFF, f), EOF);
    std::fclose(f);
  }

  std::filesystem::path dir_;
  static int counter_;
};

int WorldBundleTest::counter_ = 0;

constexpr std::uint32_t K_EARTH_UID = worldFullUid(0x0101);

/* ----------------------------- Identity helpers ----------------------------- */

TEST(WorldUidTest, RangeMembership) {
  static_assert(worldFullUid(0x0100) == 0x010000u);
  static_assert(isWorldUid(worldFullUid(0x0100)));
  static_assert(isWorldUid(worldFullUid(0x01FF)));
  static_assert(!isWorldUid(0x00DC00u)); // component space
  static_assert(!isWorldUid(worldFullUid(0x0200)));
  SUCCEED();
}

/* ----------------------------- Kind registry ----------------------------- */

TEST(BundleKindTest, RegistryResolvesWorldAndRefusesUnknown) {
  const auto* W = bundleKindByName("world");
  ASSERT_NE(W, nullptr);
  EXPECT_EQ(W->componentIdFirst, WORLD_COMPONENT_ID_FIRST);
  EXPECT_EQ(W->suffix, WORLD_FILE_SUFFIX);
  EXPECT_EQ(W->roleCount, 3u);
  EXPECT_EQ(bundleKindByName("engine"), nullptr); // registers when real

  EXPECT_EQ(bundleKindByUid(worldFullUid(0x0101)), W);
  EXPECT_EQ(bundleKindByUid(0x00DC00u), nullptr); // component space

  WorldRoleInfo info{};
  EXPECT_TRUE(bundleRoleFromName(*W, "terrain", info));
  EXPECT_EQ(info.suffix, ".htile");
  EXPECT_FALSE(bundleRoleFromName(*W, "perf_map", info));
}

/* ----------------------------- Round trip ----------------------------- */

TEST_F(WorldBundleTest, RoundTripThreeRoles) {
  const auto OUT = dir_ / "earth.world.tprm";
  const auto SRC = threeSources();
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", SRC), WorldBundleCheck::OK);

  WorldBundleReader r;
  ASSERT_EQ(r.open(OUT), WorldBundleCheck::OK);
  EXPECT_EQ(r.header().fullUid, K_EARTH_UID);
  EXPECT_EQ(r.header().entryCount, 3u);
  EXPECT_STREQ(r.header().body, "earth");
  EXPECT_EQ(r.header().totalSize, std::filesystem::file_size(OUT));

  for (const auto& e : r.entries()) {
    EXPECT_EQ(e.offset % WORLD_PAYLOAD_ALIGN, 0u);
  }

  std::size_t idx = 0;
  ASSERT_EQ(r.findEntry(WorldEntryRole::ATMOSPHERE, idx), WorldBundleCheck::OK);
  EXPECT_EQ(r.entries()[idx].size, 288u);
  EXPECT_EQ(r.entries()[idx].specHash, 0x3333u);
  EXPECT_STREQ(r.entries()[idx].suffix, ".atm");

  std::vector<std::uint8_t> payload;
  ASSERT_EQ(r.readEntry(idx, payload), WorldBundleCheck::OK);
  ASSERT_EQ(payload.size(), 288u);
  EXPECT_EQ(payload[0], static_cast<std::uint8_t>(3));
  EXPECT_EQ(payload[1], static_cast<std::uint8_t>(10));

  EXPECT_EQ(r.verifyContentHash(), WorldBundleCheck::OK);
}

TEST_F(WorldBundleTest, DeterministicBytes) {
  const auto A = dir_ / "a.world.tprm";
  const auto B = dir_ / "b.world.tprm";
  const auto SRC = threeSources();
  ASSERT_EQ(WorldBundleWriter::write(A, K_EARTH_UID, "earth", SRC), WorldBundleCheck::OK);
  ASSERT_EQ(WorldBundleWriter::write(B, K_EARTH_UID, "earth", SRC), WorldBundleCheck::OK);

  std::ifstream fa(A, std::ios::binary), fb(B, std::ios::binary);
  const std::string BYTES_A((std::istreambuf_iterator<char>(fa)), {});
  const std::string BYTES_B((std::istreambuf_iterator<char>(fb)), {});
  EXPECT_EQ(BYTES_A, BYTES_B);
}

/* ----------------------------- Writer refusals ----------------------------- */

TEST_F(WorldBundleTest, WriterRejectsComponentSpaceUid) {
  EXPECT_EQ(WorldBundleWriter::write(dir_ / "x.world.tprm", 0x00DC00u, "earth", threeSources()),
            WorldBundleCheck::BAD_UID);
}

TEST_F(WorldBundleTest, WriterRejectsDuplicateRole) {
  auto src = threeSources();
  src.push_back({WorldEntryRole::ATMOSPHERE, ".atm", makeSource("dup.atm", 64, 9), 0});
  EXPECT_EQ(WorldBundleWriter::write(dir_ / "x.world.tprm", K_EARTH_UID, "earth", src),
            WorldBundleCheck::TABLE_INVALID);
}

TEST_F(WorldBundleTest, WriterRejectsMissingSource) {
  std::vector<WorldEntrySource> src = {
      {WorldEntryRole::ATMOSPHERE, ".atm", dir_ / "absent.atm", 0}};
  EXPECT_EQ(WorldBundleWriter::write(dir_ / "x.world.tprm", K_EARTH_UID, "earth", src),
            WorldBundleCheck::FILE_ERROR);
}

/* ----------------------------- Corruption verdicts ----------------------------- */

TEST_F(WorldBundleTest, PayloadFlipFailsEntryCrcAndContentHash) {
  const auto OUT = dir_ / "earth.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);

  WorldBundleReader probe;
  ASSERT_EQ(probe.open(OUT), WorldBundleCheck::OK);
  std::size_t idx = 0;
  ASSERT_EQ(probe.findEntry(WorldEntryRole::TERRAIN, idx), WorldBundleCheck::OK);
  const std::uint64_t FLIP_AT = probe.entries()[idx].offset + 5;
  probe.close();

  flipByte(OUT, FLIP_AT);

  WorldBundleReader r;
  ASSERT_EQ(r.open(OUT), WorldBundleCheck::OK); // header/table untouched
  std::vector<std::uint8_t> payload;
  EXPECT_EQ(r.readEntry(idx, payload), WorldBundleCheck::ENTRY_CRC);
  EXPECT_EQ(r.verifyContentHash(), WorldBundleCheck::CONTENT_HASH);
}

TEST_F(WorldBundleTest, TruncationFailsSizeCheck) {
  const auto OUT = dir_ / "earth.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);
  std::filesystem::resize_file(OUT, std::filesystem::file_size(OUT) - 1);
  WorldBundleReader r;
  EXPECT_EQ(r.open(OUT), WorldBundleCheck::SIZE_MISMATCH);
}

TEST_F(WorldBundleTest, MagicFlipRefused) {
  const auto OUT = dir_ / "earth.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);
  flipByte(OUT, 0);
  WorldBundleReader r;
  EXPECT_EQ(r.open(OUT), WorldBundleCheck::BAD_MAGIC);
}

TEST_F(WorldBundleTest, VersionFlipRefused) {
  const auto OUT = dir_ / "earth.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);
  flipByte(OUT, 4); // version low byte
  WorldBundleReader r;
  EXPECT_EQ(r.open(OUT), WorldBundleCheck::BAD_VERSION);
}

TEST_F(WorldBundleTest, UidFlipOutOfRangeRefused) {
  const auto OUT = dir_ / "earth.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);
  flipByte(OUT, 10); // fullUid high byte: 0x0101xx -> outside range
  WorldBundleReader r;
  EXPECT_EQ(r.open(OUT), WorldBundleCheck::BAD_UID);
}

TEST_F(WorldBundleTest, DoctoredEntryOffsetRefused) {
  const auto OUT = dir_ / "earth.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);
  // First entry record's offset field sits at header + 16 (role/pad,
  // crc32, suffix precede it).
  const std::uint64_t OFFSET_FIELD = WORLD_HEADER_SIZE + 16;
  std::FILE* f = std::fopen(OUT.string().c_str(), "rb+");
  ASSERT_NE(f, nullptr);
  ASSERT_EQ(std::fseek(f, static_cast<long>(OFFSET_FIELD), SEEK_SET), 0);
  const std::uint64_t HUGE = ~0ull;
  ASSERT_EQ(std::fwrite(&HUGE, 1, sizeof(HUGE), f), sizeof(HUGE));
  std::fclose(f);
  WorldBundleReader r;
  EXPECT_EQ(r.open(OUT), WorldBundleCheck::TABLE_INVALID);
}

TEST_F(WorldBundleTest, MissingRoleReportsNotFound) {
  const auto OUT = dir_ / "nogravity.world.tprm";
  std::vector<WorldEntrySource> src = {
      {WorldEntryRole::ATMOSPHERE, ".atm", makeSource("only.atm", 288, 3), 0}};
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", src), WorldBundleCheck::OK);
  WorldBundleReader r;
  ASSERT_EQ(r.open(OUT), WorldBundleCheck::OK);
  std::size_t idx = 0;
  EXPECT_EQ(r.findEntry(WorldEntryRole::GRAVITY, idx), WorldBundleCheck::ENTRY_NOT_FOUND);
}

// An empty bundle is a manifest error: an unbound config is the way
// to say "no world", not a bundle with nothing in it.
TEST_F(WorldBundleTest, EmptyBundleRefused) {
  EXPECT_EQ(WorldBundleWriter::write(dir_ / "empty.world.tprm", K_EARTH_UID, "earth", {}),
            WorldBundleCheck::TABLE_INVALID);
}

// Unknown roles below the ceiling are tolerated on open (findEntry
// simply never matches), so grown worlds keep serving old consumers;
// roles at or above the ceiling are refused for mask safety.
TEST_F(WorldBundleTest, UnknownRoleToleratedCeilingRefused) {
  const auto OUT = dir_ / "grown.world.tprm";
  ASSERT_EQ(WorldBundleWriter::write(OUT, K_EARTH_UID, "earth", threeSources()),
            WorldBundleCheck::OK);
  // Rewrite entry 0's role to an unknown-but-legal value (40).
  {
    std::FILE* f = std::fopen(OUT.string().c_str(), "rb+");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(std::fseek(f, static_cast<long>(WORLD_HEADER_SIZE), SEEK_SET), 0);
    ASSERT_NE(std::fputc(40, f), EOF);
    std::fclose(f);
  }
  WorldBundleReader r;
  ASSERT_EQ(r.open(OUT), WorldBundleCheck::OK); // tolerated
  std::size_t idx = 0;
  EXPECT_EQ(r.findEntry(WorldEntryRole::GRAVITY, idx), WorldBundleCheck::ENTRY_NOT_FOUND);
  EXPECT_EQ(r.findEntry(WorldEntryRole::ATMOSPHERE, idx), WorldBundleCheck::OK);
  r.close();
  // Now the ceiling: role 64 refuses.
  {
    std::FILE* f = std::fopen(OUT.string().c_str(), "rb+");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(std::fseek(f, static_cast<long>(WORLD_HEADER_SIZE), SEEK_SET), 0);
    ASSERT_NE(std::fputc(64, f), EOF);
    std::fclose(f);
  }
  EXPECT_EQ(r.open(OUT), WorldBundleCheck::TABLE_INVALID);
}

} // namespace
