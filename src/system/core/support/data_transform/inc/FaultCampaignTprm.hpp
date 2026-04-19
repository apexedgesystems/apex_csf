#ifndef APEX_SUPPORT_DATA_TRANSFORM_FAULT_CAMPAIGN_TPRM_HPP
#define APEX_SUPPORT_DATA_TRANSFORM_FAULT_CAMPAIGN_TPRM_HPP
/**
 * @file FaultCampaignTprm.hpp
 * @brief Binary TPRM format for pre-defined fault injection campaigns.
 *
 * A fault campaign defines a set of timed data mutations that are translated
 * into an ATS (Absolute Time Sequence) at init time and loaded into the
 * action engine. Each fault entry specifies a target, mask type, trigger
 * cycle, and duration.
 *
 * The ATS approach provides cycle-accurate fault timing through the action
 * engine's existing sequencer, with zero additional runtime overhead in
 * DataTransform.
 *
 * TPRM layout:
 *   4 bytes header (entryCount + padding)
 *   8 x 36 bytes entries = 288 bytes
 *   Total: 292 bytes
 */

#include <cstdint>
#include <type_traits>

namespace system_core {
namespace support {

/* ----------------------------- Constants ----------------------------- */

/// Maximum fault entries per campaign.
constexpr std::size_t FAULT_CAMPAIGN_MAX_ENTRIES = 8;

/* ----------------------------- MaskType ----------------------------- */

/**
 * @enum MaskType
 * @brief Type of byte mask to apply.
 */
enum class MaskType : std::uint8_t {
  ZERO = 0,  ///< Force bytes to 0x00.
  HIGH = 1,  ///< Force bytes to 0xFF.
  FLIP = 2,  ///< Invert all bits.
  CUSTOM = 3 ///< Custom AND/XOR mask (uses customAnd/customXor fields).
};

/* ----------------------------- FaultEntry ----------------------------- */

/**
 * @struct FaultEntry
 * @brief Single pre-defined fault injection entry.
 */
struct __attribute__((packed)) FaultEntry {
  /* ---- Target ---- */
  std::uint32_t targetFullUid{0};    ///< Target component fullUid.
  std::uint8_t targetCategory{0};    ///< DataCategory enum value.
  std::uint16_t targetByteOffset{0}; ///< Byte offset within data block.
  std::uint8_t targetByteLen{0};     ///< Number of bytes to mutate.

  /* ---- Mask ---- */
  std::uint8_t maskType{0};    ///< MaskType enum value.
  std::uint8_t customAnd[8]{}; ///< AND mask (CUSTOM only).
  std::uint8_t customXor[8]{}; ///< XOR mask (CUSTOM only).

  /* ---- Timing ---- */
  std::uint32_t triggerCycle{0};   ///< Cycle at which to inject fault.
  std::uint32_t durationCycles{0}; ///< Cycles to hold (0 = one-shot apply then disarm).
  std::uint8_t reserved[3]{};      ///< Padding to 36 bytes.
};

static_assert(sizeof(FaultEntry) == 36, "FaultEntry must be 36 bytes");

/* ----------------------------- FaultCampaignTprm ----------------------------- */

/**
 * @struct FaultCampaignTprm
 * @brief Complete fault campaign TPRM (binary, loaded at boot).
 *
 * Entries are sorted by triggerCycle at build time (TOML -> binary).
 * At init, DataTransform translates entries into an ATS and loads it
 * into the action engine.
 */
struct __attribute__((packed)) FaultCampaignTprm {
  std::uint8_t entryCount{0};                     ///< Number of valid entries (0-8).
  std::uint8_t reserved[3]{};                     ///< Padding to 4-byte alignment.
  FaultEntry entries[FAULT_CAMPAIGN_MAX_ENTRIES]; ///< Fault table.
};

static_assert(sizeof(FaultCampaignTprm) == 4 + 8 * 36, "FaultCampaignTprm size mismatch");
static_assert(std::is_trivially_copyable_v<FaultCampaignTprm>,
              "FaultCampaignTprm must be trivially copyable");

} // namespace support
} // namespace system_core

#endif // APEX_SUPPORT_DATA_TRANSFORM_FAULT_CAMPAIGN_TPRM_HPP
