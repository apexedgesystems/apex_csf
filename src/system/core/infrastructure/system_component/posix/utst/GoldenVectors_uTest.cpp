/**
 * @file GoldenVectors_uTest.cpp
 * @brief Cross-language conformance against the committed TPRM golden vectors.
 *
 * The files under compat/tprm/ (APEX_TPRM_VECTORS_DIR) are the
 * byte-level contract between the rust reference implementation, this
 * runtime, and external consumers. The rust suite pins the generator to the
 * committed bytes; these tests pin hex2cpp and PackedTprmReader to the same
 * bytes, so a format change on either side fails a test before review.
 *
 * Every float value in the vectors is exactly representable, so comparisons
 * are EXPECT_EQ -- any tolerance would mask byte-level drift.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/PackedTprm.hpp"
#include "src/utilities/helpers/inc/Files.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using system_core::system_component::isRtsEntry;
using system_core::system_component::PackedTprmReader;
using system_core::system_component::sequenceSlot;

namespace vectors {

/* ----------------------------- Mirror structs ----------------------------- */
// Field order and widths mirror the TOML sources one to one; the payload
// format is the packed memory image, so the layouts are locked by
// static_assert against the committed file sizes.

#pragma pack(push, 1)

struct ScalarTypes {
  std::uint8_t u8MaxMinus;
  std::uint16_t u16Pattern;
  std::uint32_t u32Pattern;
  std::uint64_t u64Wide;
  std::int8_t i8Neg;
  std::int16_t i16Neg;
  std::int32_t i32Neg;
  std::int64_t i64Neg;
  float f32Exact;
  double f64AsFloat8;
  double f64AsDouble;
  bool flag;
  char letter;
};
static_assert(sizeof(ScalarTypes) == 52, "scalar_types.bin layout");

struct StringsArrays {
  char name[12];
  std::uint16_t counts[4];
  double gains[3];
  std::uint8_t sentinel;
};
static_assert(sizeof(StringsArrays) == 45, "strings_arrays.bin layout");

struct NestedEnum {
  std::uint8_t mode;
  std::uint32_t flags;
  struct Inner {
    std::uint16_t innerA;
    float innerB;
  } inner;
  std::int16_t tail;
};
static_assert(sizeof(NestedEnum) == 13, "nested_enum.bin layout");

#pragma pack(pop)

/* ----------------------------- Helpers ----------------------------- */

inline std::filesystem::path dir() { return std::filesystem::path(APEX_TPRM_VECTORS_DIR); }

inline std::vector<std::uint8_t> readAll(const std::filesystem::path& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

template <typename T> T load(const char* name) {
  T out{};
  std::string error;
  std::optional<std::reference_wrapper<std::string>> errorRef{error};
  const bool OK =
      apex::helpers::files::hex2cpp((dir() / "payloads" / name).string(), out, errorRef);
  EXPECT_TRUE(OK) << name << ": " << error;
  return out;
}

} // namespace vectors

/* ----------------------------- Payload conformance ----------------------------- */

/** @test Every scalar shape round-trips from the committed vector. */
TEST(GoldenVectors, ScalarTypesPayload) {
  const auto V = vectors::load<vectors::ScalarTypes>("scalar_types.bin");
  EXPECT_EQ(V.u8MaxMinus, 127U);
  EXPECT_EQ(V.u16Pattern, 0xBEEFU);
  EXPECT_EQ(V.u32Pattern, 0xDEADBEEFU);
  EXPECT_EQ(V.u64Wide, 0x0123456789ABCDEFULL);
  EXPECT_EQ(V.i8Neg, -5);
  EXPECT_EQ(V.i16Neg, -1234);
  EXPECT_EQ(V.i32Neg, -100000);
  EXPECT_EQ(V.i64Neg, -5000000000LL);
  EXPECT_EQ(V.f32Exact, 1.5F);
  EXPECT_EQ(V.f64AsFloat8, -2.25);
  EXPECT_EQ(V.f64AsDouble, 1048576.125);
  EXPECT_TRUE(V.flag);
  EXPECT_EQ(V.letter, 'A');
}

/** @test Fixed-width string padding and array element packing. */
TEST(GoldenVectors, StringsArraysPayload) {
  const auto V = vectors::load<vectors::StringsArrays>("strings_arrays.bin");
  EXPECT_EQ(std::string(V.name), "apex");
  for (std::size_t i = 4; i < sizeof(V.name); ++i) {
    EXPECT_EQ(V.name[i], '\0') << "string padding at byte " << i;
  }
  EXPECT_EQ(V.counts[0], 1U);
  EXPECT_EQ(V.counts[1], 2U);
  EXPECT_EQ(V.counts[2], 513U);
  EXPECT_EQ(V.counts[3], 65535U);
  EXPECT_EQ(V.gains[0], 0.5);
  EXPECT_EQ(V.gains[1], -1.25);
  EXPECT_EQ(V.gains[2], 4096.0625);
  EXPECT_EQ(V.sentinel, 0xA5U);
}

/** @test Enum resolution, hex literals, and nested-struct flattening. */
TEST(GoldenVectors, NestedEnumPayload) {
  const auto V = vectors::load<vectors::NestedEnum>("nested_enum.bin");
  EXPECT_EQ(V.mode, 7U);
  EXPECT_EQ(V.flags, 0x00C0FFEEU);
  EXPECT_EQ(V.inner.innerA, 0x0102U);
  EXPECT_EQ(V.inner.innerB, 3.5F);
  EXPECT_EQ(V.tail, -2);
}

/* ----------------------------- Archive conformance ----------------------------- */

/** @test The committed archive loads, indexes, and extracts byte-identically. */
TEST(GoldenVectors, ArchiveRoundTrip) {
  PackedTprmReader reader;
  std::string error;
  ASSERT_TRUE(reader.load(vectors::dir() / "archives" / "basic.tprm", error)) << error;
  ASSERT_EQ(reader.count(), 3U);

  const struct {
    std::uint32_t fullUid;
    const char* payload;
  } EXPECTED[] = {
      {0x000000U, "scalar_types.bin"},
      {0x00D001U, "strings_arrays.bin"},
      {0xFF0005U, "nested_enum.bin"},
  };

  for (const auto& e : EXPECTED) {
    const auto* ENTRY = reader.findEntry(e.fullUid);
    ASSERT_NE(ENTRY, nullptr) << std::hex << e.fullUid;
    const auto* DATA = reader.getData(*ENTRY);
    ASSERT_NE(DATA, nullptr);
    const auto FILE_BYTES = vectors::readAll(vectors::dir() / "payloads" / e.payload);
    ASSERT_EQ(ENTRY->size, FILE_BYTES.size());
    EXPECT_EQ(std::vector<std::uint8_t>(DATA, DATA + ENTRY->size), FILE_BYTES)
        << e.payload << " bytes diverge inside the archive";
  }

  // The RTS reserved range is part of the container contract.
  EXPECT_TRUE(isRtsEntry(0xFF0005U));
  EXPECT_EQ(sequenceSlot(0xFF0005U), 5U);

  const auto TMP = std::filesystem::temp_directory_path() / "tprm_golden_extract";
  std::filesystem::remove_all(TMP);
  ASSERT_TRUE(reader.extractAll(TMP, error)) << error;
  EXPECT_EQ(vectors::readAll(TMP / "000000.tprm"),
            vectors::readAll(vectors::dir() / "payloads" / "scalar_types.bin"));
  EXPECT_EQ(vectors::readAll(TMP / "00d001.tprm"),
            vectors::readAll(vectors::dir() / "payloads" / "strings_arrays.bin"));
  EXPECT_EQ(vectors::readAll(TMP / "ff0005.tprm"),
            vectors::readAll(vectors::dir() / "payloads" / "nested_enum.bin"));
  std::filesystem::remove_all(TMP);
}
