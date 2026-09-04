//! Format A v3 payload prelude: self-describing header on every
//! component payload.
//!
//! Layout (20 bytes, little-endian):
//!   magic[4]       = "APV3"
//!   version[2]     = 3
//!   payloadSize[2] = byte length of the payload that follows
//!   fullUid[4]     = (componentId << 8) | instanceIndex the payload targets
//!   layoutHash[4]  = CRC-32 of the canonical field spec (see below)
//!   payloadCrc[4]  = CRC-32 (IEEE) of the payload bytes
//!
//! The header exists so a reader can refuse, loudly and specifically,
//! anything that is not the payload it expects: wrong file (magic),
//! wrong era (version), wrong target (fullUid), wrong struct layout
//! (layoutHash), truncation (payloadSize), corruption (payloadCrc).
//!
//! Canonical field spec: during serialization every leaf field
//! contributes `name:type:size:offset;` in emission order (the TOML
//! declaration order), and the string closes with a `|size:total`
//! terminator; layoutHash is the CRC-32 of that ASCII string. Offsets
//! pin the byte layout, so templates that place the same fields at
//! different positions hash differently -- the same-size
//! silent-misload hole this header closes.

use super::Error;

pub const MAGIC: &[u8; 4] = b"APV3";
pub const VERSION: u16 = 3;
pub const HEADER_SIZE: usize = 20;

/// Parsed v3 prelude.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PayloadHeader {
    pub version: u16,
    pub payload_size: u16,
    pub full_uid: u32,
    pub layout_hash: u32,
    pub payload_crc: u32,
}

/* ----------------------------- CRC-32 ----------------------------- */

/// CRC-32 (IEEE 802.3, reflected, init/final 0xFFFFFFFF).
pub fn crc32(data: &[u8]) -> u32 {
    let mut crc: u32 = 0xFFFF_FFFF;
    for &byte in data {
        crc ^= u32::from(byte);
        for _ in 0..8 {
            let mask = (crc & 1).wrapping_neg();
            crc = (crc >> 1) ^ (0xEDB8_8320 & mask);
        }
    }
    !crc
}

/* ----------------------------- Stamp / parse ----------------------------- */

/// Prepend the v3 prelude to a serialized payload.
pub fn stamp(full_uid: u32, layout_hash: u32, payload: &[u8]) -> Result<Vec<u8>, Error> {
    let size = u16::try_from(payload.len()).map_err(|_| {
        Error::Emit(format!(
            "payload of {} bytes exceeds the {} byte v3 size field",
            payload.len(),
            u16::MAX
        ))
    })?;
    let mut out = Vec::with_capacity(HEADER_SIZE + payload.len());
    out.extend_from_slice(MAGIC);
    out.extend_from_slice(&VERSION.to_le_bytes());
    out.extend_from_slice(&size.to_le_bytes());
    out.extend_from_slice(&full_uid.to_le_bytes());
    out.extend_from_slice(&layout_hash.to_le_bytes());
    out.extend_from_slice(&crc32(payload).to_le_bytes());
    out.extend_from_slice(payload);
    Ok(out)
}

/// Parse and fully verify a v3-stamped payload; returns the header and
/// the payload slice. Every check that fails is a distinct error.
pub fn parse(data: &[u8]) -> Result<(PayloadHeader, &[u8]), Error> {
    if data.len() < HEADER_SIZE {
        return Err(Error::Parse(format!(
            "{} bytes is too small for the {HEADER_SIZE} byte v3 prelude",
            data.len()
        )));
    }
    if &data[0..4] != MAGIC {
        return Err(Error::Parse("bad payload magic (want APV3)".to_string()));
    }
    let version = u16::from_le_bytes([data[4], data[5]]);
    if version != VERSION {
        return Err(Error::Parse(format!(
            "payload format version {version} (reader requires {VERSION})"
        )));
    }
    let payload_size = u16::from_le_bytes([data[6], data[7]]);
    let full_uid = u32::from_le_bytes([data[8], data[9], data[10], data[11]]);
    let layout_hash = u32::from_le_bytes([data[12], data[13], data[14], data[15]]);
    let payload_crc = u32::from_le_bytes([data[16], data[17], data[18], data[19]]);

    let payload = &data[HEADER_SIZE..];
    if payload.len() != payload_size as usize {
        return Err(Error::Parse(format!(
            "payload is {} bytes but the header declares {payload_size}",
            payload.len()
        )));
    }
    let actual_crc = crc32(payload);
    if actual_crc != payload_crc {
        return Err(Error::Parse(format!(
            "payload CRC 0x{actual_crc:08X} does not match header 0x{payload_crc:08X}"
        )));
    }
    Ok((
        PayloadHeader {
            version,
            payload_size,
            full_uid,
            layout_hash,
            payload_crc,
        },
        payload,
    ))
}

/* ----------------------------- Tests ----------------------------- */

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc32_matches_ieee_check_value() {
        // The canonical CRC-32 check: crc32("123456789") = 0xCBF43926.
        assert_eq!(crc32(b"123456789"), 0xCBF4_3926);
    }

    #[test]
    fn stamp_then_parse_round_trips() {
        let payload = [0xDEu8, 0xAD, 0xBE, 0xEF, 0x01];
        let stamped = stamp(0x000100, 0x1234_5678, &payload).unwrap();
        assert_eq!(stamped.len(), HEADER_SIZE + payload.len());
        let (hdr, body) = parse(&stamped).unwrap();
        assert_eq!(hdr.version, VERSION);
        assert_eq!(hdr.payload_size, 5);
        assert_eq!(hdr.full_uid, 0x000100);
        assert_eq!(hdr.layout_hash, 0x1234_5678);
        assert_eq!(body, payload);
    }

    #[test]
    fn parse_rejects_each_field_distinctly() {
        let payload = [1u8, 2, 3];
        let good = stamp(7, 0, &payload).unwrap();

        let mut bad_magic = good.clone();
        bad_magic[0] = b'X';
        assert!(format!("{}", parse(&bad_magic).unwrap_err()).contains("magic"));

        let mut bad_version = good.clone();
        bad_version[4] = 9;
        assert!(format!("{}", parse(&bad_version).unwrap_err()).contains("version"));

        let mut bad_size = good.clone();
        bad_size[6] = 99;
        assert!(format!("{}", parse(&bad_size).unwrap_err()).contains("declares"));

        let mut bad_crc = good;
        *bad_crc.last_mut().unwrap() ^= 0xFF;
        assert!(format!("{}", parse(&bad_crc).unwrap_err()).contains("CRC"));

        assert!(format!("{}", parse(&[0u8; 4]).unwrap_err()).contains("too small"));
    }
}
