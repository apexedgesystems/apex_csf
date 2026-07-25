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
// Packed members are read into locals before asserting: EXPECT_EQ binds
// references to its arguments, and a reference to a misaligned packed member
// is undefined behavior (UBSan enforces it). Value loads are well-defined --
// the same rule that makes the runtime's TParams structs naturally
// padding-free by field order instead of packed.

/** @test Every scalar shape round-trips from the committed vector. */
TEST(GoldenVectors, ScalarTypesPayload) {
  const auto V = vectors::load<vectors::ScalarTypes>("scalar_types.bin");
  const std::uint8_t U8 = V.u8MaxMinus;
  const std::uint16_t U16 = V.u16Pattern;
  const std::uint32_t U32 = V.u32Pattern;
  const std::uint64_t U64 = V.u64Wide;
  const std::int8_t I8 = V.i8Neg;
  const std::int16_t I16 = V.i16Neg;
  const std::int32_t I32 = V.i32Neg;
  const std::int64_t I64 = V.i64Neg;
  const float F32 = V.f32Exact;
  const double F64A = V.f64AsFloat8;
  const double F64B = V.f64AsDouble;
  const bool FLAG = V.flag;
  const char LETTER = V.letter;
  EXPECT_EQ(U8, 127U);
  EXPECT_EQ(U16, 0xBEEFU);
  EXPECT_EQ(U32, 0xDEADBEEFU);
  EXPECT_EQ(U64, 0x0123456789ABCDEFULL);
  EXPECT_EQ(I8, -5);
  EXPECT_EQ(I16, -1234);
  EXPECT_EQ(I32, -100000);
  EXPECT_EQ(I64, -5000000000LL);
  EXPECT_EQ(F32, 1.5F);
  EXPECT_EQ(F64A, -2.25);
  EXPECT_EQ(F64B, 1048576.125);
  EXPECT_TRUE(FLAG);
  EXPECT_EQ(LETTER, 'A');
}

/** @test Fixed-width string padding and array element packing. */
TEST(GoldenVectors, StringsArraysPayload) {
  const auto V = vectors::load<vectors::StringsArrays>("strings_arrays.bin");
  EXPECT_EQ(std::string(V.name), "apex");
  for (std::size_t i = 4; i < sizeof(V.name); ++i) {
    const char PAD = V.name[i];
    EXPECT_EQ(PAD, '\0') << "string padding at byte " << i;
  }
  const std::uint16_t COUNTS[] = {V.counts[0], V.counts[1], V.counts[2], V.counts[3]};
  EXPECT_EQ(COUNTS[0], 1U);
  EXPECT_EQ(COUNTS[1], 2U);
  EXPECT_EQ(COUNTS[2], 513U);
  EXPECT_EQ(COUNTS[3], 65535U);
  const double GAINS[] = {V.gains[0], V.gains[1], V.gains[2]};
  EXPECT_EQ(GAINS[0], 0.5);
  EXPECT_EQ(GAINS[1], -1.25);
  EXPECT_EQ(GAINS[2], 4096.0625);
  const std::uint8_t SENTINEL = V.sentinel;
  EXPECT_EQ(SENTINEL, 0xA5U);
}

/** @test Enum resolution, hex literals, and nested-struct flattening. */
TEST(GoldenVectors, NestedEnumPayload) {
  const auto V = vectors::load<vectors::NestedEnum>("nested_enum.bin");
  const std::uint8_t MODE = V.mode;
  const std::uint32_t FLAGS = V.flags;
  const std::uint16_t INNER_A = V.inner.innerA;
  const float INNER_B = V.inner.innerB;
  const std::int16_t TAIL = V.tail;
  EXPECT_EQ(MODE, 7U);
  EXPECT_EQ(FLAGS, 0x00C0FFEEU);
  EXPECT_EQ(INNER_A, 0x0102U);
  EXPECT_EQ(INNER_B, 3.5F);
  EXPECT_EQ(TAIL, -2);
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
