/**
 * @file TprmPayload_uTest.cpp
 * @brief Unit tests for the TPRM payload check names.
 */

#include "src/system/core/infrastructure/system_component/posix/inc/TprmPayload.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using system_core::system_component::toFaultCode;
using system_core::system_component::toString;
using system_core::system_component::TPRM_PAYLOAD_VERSION;
using system_core::system_component::TprmPayloadCheck;

/** @test Each check's reject text states its reason; the version text names the version read. */
TEST(TprmPayloadTest, CheckNamesStateTheReason) {
  const std::vector<std::pair<TprmPayloadCheck, std::string>> NAMES = {
      {TprmPayloadCheck::OK, "ok"},
      {TprmPayloadCheck::FILE_ERROR, "file open/read failed"},
      {TprmPayloadCheck::TOO_SMALL, "smaller than the v4 prelude"},
      {TprmPayloadCheck::BAD_MAGIC, "bad payload magic (want APV4)"},
      {TprmPayloadCheck::BAD_VERSION,
       "payload version not " + std::to_string(TPRM_PAYLOAD_VERSION)},
      {TprmPayloadCheck::SIZE_MISMATCH, "body length differs from header"},
      {TprmPayloadCheck::UID_MISMATCH, "payload targets another fullUid"},
      {TprmPayloadCheck::CRC_MISMATCH, "payload CRC mismatch"},
      {TprmPayloadCheck::BODY_SIZE_MISMATCH, "body size differs from TParams"},
      {TprmPayloadCheck::CONSTRAINT_VIOLATION, "field value outside its declared legal range"},
      {TprmPayloadCheck::LAYOUT_MISMATCH, "payload layout hash differs from the spec expectation"},
  };

  // The table holds every check: the codes run from 0 to the last without a gap.
  ASSERT_EQ(NAMES.size(), toFaultCode(TprmPayloadCheck::LAYOUT_MISMATCH) + 1u);
  for (std::size_t i = 0; i < NAMES.size(); ++i) {
    EXPECT_EQ(toFaultCode(NAMES[i].first), i);
    EXPECT_EQ(std::string(toString(NAMES[i].first)), NAMES[i].second) << "check " << i;
  }

  const auto PAST_LAST =
      static_cast<TprmPayloadCheck>(toFaultCode(TprmPayloadCheck::LAYOUT_MISMATCH) + 1u);
  EXPECT_EQ(std::string(toString(PAST_LAST)), "unknown");
  EXPECT_EQ(std::string(toString(static_cast<TprmPayloadCheck>(255))), "unknown");
}
