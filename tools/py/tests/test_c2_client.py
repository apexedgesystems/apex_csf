"""Unit tests for apex_tools.c2.client module.

Tests CRC32C computation (software implementation matching hardware CRC32C).
"""

from apex_tools.c2.client import crc32c

# =============================== CRC32C ================================


def test_crc32c_empty():
    """CRC32C of empty data returns initial value XORed."""
    result = crc32c(b"")
    assert isinstance(result, int)


def test_crc32c_known_value():
    """CRC32C of known input matches expected checksum."""
    # "123456789" is the standard CRC test vector
    result = crc32c(b"123456789")
    assert result == 0xE3069283


def test_crc32c_deterministic():
    """Same input always produces same CRC."""
    data = b"apex_csf test data"
    assert crc32c(data) == crc32c(data)


def test_crc32c_different_inputs_differ():
    """Different inputs produce different CRCs."""
    assert crc32c(b"aaa") != crc32c(b"bbb")


def test_crc32c_single_byte():
    """CRC32C handles single-byte input."""
    result = crc32c(b"\x00")
    assert isinstance(result, int)
    assert 0 <= result <= 0xFFFFFFFF
