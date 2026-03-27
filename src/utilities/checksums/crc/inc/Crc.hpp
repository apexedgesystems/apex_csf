#ifndef APEX_UTILITIES_CHECKSUMS_CRC_HPP
#define APEX_UTILITIES_CHECKSUMS_CRC_HPP
/**
 * @file Crc.hpp
 * @brief Convenience header with type aliases for standard CRC algorithms.
 *
 * All configurations use canonical parameters from the CRC catalog. For reflected
 * CRCs (refin=true in catalog), this implementation uses a pre-reflected polynomial
 * with LSB-first processing for efficiency.
 *
 * Each standard CRC is available in three implementations:
 * - Bitwise: No table, smallest footprint, slowest
 * - Nibble: 16-entry table, moderate footprint/speed
 * - Table: 256-entry table, largest footprint, fastest
 *
 * All operations are RT-safe after construction.
 */

#include "src/utilities/checksums/crc/inc/CrcBase.hpp"
#include "src/utilities/checksums/crc/inc/CrcBitwise.hpp"
#include "src/utilities/checksums/crc/inc/CrcHardware.hpp"
#include "src/utilities/checksums/crc/inc/CrcNibble.hpp"
#include "src/utilities/checksums/crc/inc/CrcTable.hpp"

#include <stddef.h>
#include <stdint.h>

namespace apex {
namespace checksums {
namespace crc {

/* ----------------------------- CRC-8 Variants ----------------------------- */

// CRC-8/SMBUS: Used in I2C, SMBus
// Catalog: poly=0x07, init=0x00, refin=false, refout=false, xorout=0x00, check=0xF4
using Crc8SmbusTable = CrcTable<uint8_t, 0x07u, 0x00u, 0x00u, false, false>;
using Crc8SmbusBitwise = CrcBitwise<uint8_t, 0x07u, 0x00u, 0x00u, false, false>;
using Crc8SmbusNibble = CrcNibble<uint8_t, 0x07u, 0x00u, 0x00u, false, false>;

// CRC-8/MAXIM-DOW: Used in 1-Wire, iButton devices
// Catalog: poly=0x31, init=0x00, refin=true, refout=true, xorout=0x00, check=0xA1
// Implementation uses reflected poly 0x8C with LSB-first processing
using Crc8MaximTable = CrcTable<uint8_t, 0x8Cu, 0x00u, 0x00u, true, false>;
using Crc8MaximBitwise = CrcBitwise<uint8_t, 0x8Cu, 0x00u, 0x00u, true, false>;
using Crc8MaximNibble = CrcNibble<uint8_t, 0x8Cu, 0x00u, 0x00u, true, false>;

// CRC-8/AUTOSAR: Used in automotive applications
// Catalog: poly=0x2F, init=0xFF, refin=false, refout=false, xorout=0xFF, check=0xDF
using Crc8AutosarTable = CrcTable<uint8_t, 0x2Fu, 0xFFu, 0xFFu, false, false>;
using Crc8AutosarBitwise = CrcBitwise<uint8_t, 0x2Fu, 0xFFu, 0xFFu, false, false>;
using Crc8AutosarNibble = CrcNibble<uint8_t, 0x2Fu, 0xFFu, 0xFFu, false, false>;

/* ----------------------------- CRC-16 Variants ----------------------------- */

// CRC-16/XMODEM: Used in XMODEM, ZMODEM, many serial protocols
// Catalog: poly=0x1021, init=0x0000, refin=false, refout=false, xorout=0x0000, check=0x31C3
using Crc16XmodemTable = CrcTable<uint16_t, 0x1021u, 0x0000u, 0x0000u, false, false>;
using Crc16XmodemBitwise = CrcBitwise<uint16_t, 0x1021u, 0x0000u, 0x0000u, false, false>;
using Crc16XmodemNibble = CrcNibble<uint16_t, 0x1021u, 0x0000u, 0x0000u, false, false>;

// CRC-16/IBM-3740: Common "CCITT" variant (often mislabeled)
// Catalog: poly=0x1021, init=0xFFFF, refin=false, refout=false, xorout=0x0000, check=0x29B1
using Crc16Ibm3740Table = CrcTable<uint16_t, 0x1021u, 0xFFFFu, 0x0000u, false, false>;
using Crc16Ibm3740Bitwise = CrcBitwise<uint16_t, 0x1021u, 0xFFFFu, 0x0000u, false, false>;
using Crc16Ibm3740Nibble = CrcNibble<uint16_t, 0x1021u, 0xFFFFu, 0x0000u, false, false>;

// CRC-16/IBM-SDLC: Used in X.25, HDLC, PPP, V.41
// Catalog: poly=0x1021, init=0xFFFF, refin=true, refout=true, xorout=0xFFFF, check=0x906E
// Implementation uses reflected poly 0x8408 with LSB-first processing
using Crc16IbmSdlcTable = CrcTable<uint16_t, 0x8408u, 0xFFFFu, 0xFFFFu, true, false>;
using Crc16IbmSdlcBitwise = CrcBitwise<uint16_t, 0x8408u, 0xFFFFu, 0xFFFFu, true, false>;
using Crc16IbmSdlcNibble = CrcNibble<uint16_t, 0x8408u, 0xFFFFu, 0xFFFFu, true, false>;

// CRC-16/MODBUS: Used in Modbus RTU industrial protocol
// Catalog: poly=0x8005, init=0xFFFF, refin=true, refout=true, xorout=0x0000, check=0x4B37
// Implementation uses reflected poly 0xA001 with LSB-first processing
using Crc16ModbusTable = CrcTable<uint16_t, 0xA001u, 0xFFFFu, 0x0000u, true, false>;
using Crc16ModbusBitwise = CrcBitwise<uint16_t, 0xA001u, 0xFFFFu, 0x0000u, true, false>;
using Crc16ModbusNibble = CrcNibble<uint16_t, 0xA001u, 0xFFFFu, 0x0000u, true, false>;

// CRC-16/USB: Used in USB token and data packets
// Catalog: poly=0x8005, init=0xFFFF, refin=true, refout=true, xorout=0xFFFF, check=0xB4C8
// Implementation uses reflected poly 0xA001 with LSB-first processing
using Crc16UsbTable = CrcTable<uint16_t, 0xA001u, 0xFFFFu, 0xFFFFu, true, false>;
using Crc16UsbBitwise = CrcBitwise<uint16_t, 0xA001u, 0xFFFFu, 0xFFFFu, true, false>;
using Crc16UsbNibble = CrcNibble<uint16_t, 0xA001u, 0xFFFFu, 0xFFFFu, true, false>;

/* ----------------------------- CRC-24 Variants ----------------------------- */

// CRC-24/OPENPGP: Used in PGP/GPG
// Catalog: poly=0x864CFB, init=0xB704CE, refin=false, refout=false, xorout=0x000000, check=0x21CF02
using Crc24OpenpgpTable = CrcTable<uint32_t, 0x864CFBu, 0xB704CEu, 0x000000u, false, false, 24>;
using Crc24OpenpgpBitwise = CrcBitwise<uint32_t, 0x864CFBu, 0xB704CEu, 0x000000u, false, false, 24>;
using Crc24OpenpgpNibble = CrcNibble<uint32_t, 0x864CFBu, 0xB704CEu, 0x000000u, false, false, 24>;

/* ----------------------------- CRC-32 Variants ----------------------------- */

// CRC-32/ISO-HDLC: THE standard CRC-32 (Ethernet, ZIP, PNG, gzip, MPEG-2 payload)
// Catalog: poly=0x04C11DB7, init=0xFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFF,
// check=0xCBF43926 Implementation uses reflected poly 0xEDB88320 with LSB-first processing
using Crc32IsoHdlcTable = CrcTable<uint32_t, 0xEDB88320u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;
using Crc32IsoHdlcBitwise =
    CrcBitwise<uint32_t, 0xEDB88320u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;
using Crc32IsoHdlcNibble = CrcNibble<uint32_t, 0xEDB88320u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;

// CRC-32/ISCSI: Used in iSCSI, SCTP, Btrfs, ext4 (also called CRC-32C)
// Catalog: poly=0x1EDC6F41, init=0xFFFFFFFF, refin=true, refout=true, xorout=0xFFFFFFFF,
// check=0xE3069283 Implementation uses reflected poly 0x82F63B78 with LSB-first processing Note:
// Hardware accelerated on x86 (SSE4.2) and ARM (CRC32 extension)
using Crc32IscsiTable = CrcTable<uint32_t, 0x82F63B78u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;
using Crc32IscsiBitwise = CrcBitwise<uint32_t, 0x82F63B78u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;
using Crc32IscsiNibble = CrcNibble<uint32_t, 0x82F63B78u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;

// Hardware-accelerated CRC-32C (ISCSI) - uses SSE4.2 or ARM CRC32 when available
#if APEX_CRC_HAS_SSE42
using Crc32IscsiHardware = CrcSse42<uint32_t, 0x82F63B78u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;
#elif APEX_CRC_HAS_ARM_CRC32
using Crc32IscsiHardware = CrcArm<uint32_t, 0x82F63B78u, 0xFFFFFFFFu, 0xFFFFFFFFu, true, false>;
#else
using Crc32IscsiHardware = Crc32IscsiTable; // Software fallback
#endif

// Default CRC-32C alias: best available implementation
using Crc32Iscsi = Crc32IscsiHardware;

// CRC-32/MPEG-2: Used in MPEG-2 sections, ATSC, DVB
// Catalog: poly=0x04C11DB7, init=0xFFFFFFFF, refin=false, refout=false, xorout=0x00000000,
// check=0x0376E6E7
using Crc32Mpeg2Table = CrcTable<uint32_t, 0x04C11DB7u, 0xFFFFFFFFu, 0x00000000u, false, false>;
using Crc32Mpeg2Bitwise = CrcBitwise<uint32_t, 0x04C11DB7u, 0xFFFFFFFFu, 0x00000000u, false, false>;
using Crc32Mpeg2Nibble = CrcNibble<uint32_t, 0x04C11DB7u, 0xFFFFFFFFu, 0x00000000u, false, false>;

/* ----------------------------- CRC-64 Variants ----------------------------- */

// CRC-64/ECMA-182: ECMA-182 standard
// Catalog: poly=0x42F0E1EBA9EA3693, init=0x0000..., refin=false, refout=false, xorout=0x0000...,
// check=0x6C40DF5F0B497347
using Crc64EcmaTable = CrcTable<uint64_t, 0x42F0E1EBA9EA3693ull, 0x0000000000000000ull,
                                0x0000000000000000ull, false, false>;
using Crc64EcmaBitwise = CrcBitwise<uint64_t, 0x42F0E1EBA9EA3693ull, 0x0000000000000000ull,
                                    0x0000000000000000ull, false, false>;
using Crc64EcmaNibble = CrcNibble<uint64_t, 0x42F0E1EBA9EA3693ull, 0x0000000000000000ull,
                                  0x0000000000000000ull, false, false>;

// CRC-64/XZ: Used in XZ Utils, LZMA
// Catalog: poly=0x42F0E1EBA9EA3693, init=0xFFFF..., refin=true, refout=true, xorout=0xFFFF...,
// check=0x995DC9BBDF1939FA Implementation uses reflected poly 0xC96C5795D7870F42 with LSB-first
// processing
using Crc64XzTable = CrcTable<uint64_t, 0xC96C5795D7870F42ull, 0xFFFFFFFFFFFFFFFFull,
                              0xFFFFFFFFFFFFFFFFull, true, false>;
using Crc64XzBitwise = CrcBitwise<uint64_t, 0xC96C5795D7870F42ull, 0xFFFFFFFFFFFFFFFFull,
                                  0xFFFFFFFFFFFFFFFFull, true, false>;
using Crc64XzNibble = CrcNibble<uint64_t, 0xC96C5795D7870F42ull, 0xFFFFFFFFFFFFFFFFull,
                                0xFFFFFFFFFFFFFFFFull, true, false>;

} // namespace crc
} // namespace checksums
} // namespace apex

#endif // APEX_UTILITIES_CHECKSUMS_CRC_HPP
