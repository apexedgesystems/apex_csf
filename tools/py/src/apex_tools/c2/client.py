"""
APROTO C2 client for commanding Apex executive applications.

Provides a high-level interface for:
  - Basic commands (noop, ping, status, arbitrary command)
  - Executive control (pause, resume, shutdown, health)
  - Ground test inspection (read registered data by category)
  - File transfer (chunked with CRC32-C verification)
  - Runtime updates (lock, unlock, TPRM reload, exec restart)

Usage:
    from apex_tools.c2.client import AprotoClient

    with AprotoClient("raspberrypi.local", 9000) as c2:
        health = c2.get_health()
        c2.update_tprm(0x000100, "new_params.tprm")
        c2.update_component(0x006600, "new_model.so", "PolynomialModel", 0)
"""

import socket
import struct
import time
from pathlib import Path
from typing import Callable, Optional

from . import protocol as proto

# CRC-32C (iSCSI) using reflected polynomial 0x82F63B78
_CRC32C_TABLE: Optional[list[int]] = None


def _crc32c_table() -> list[int]:
    global _CRC32C_TABLE
    if _CRC32C_TABLE is not None:
        return _CRC32C_TABLE
    table = []
    for i in range(256):
        crc = i
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0x82F63B78
            else:
                crc >>= 1
        table.append(crc)
    _CRC32C_TABLE = table
    return table


def crc32c(data: bytes, init: int = 0xFFFFFFFF) -> int:
    """Compute CRC-32C (iSCSI/Castagnoli) of data."""
    table = _crc32c_table()
    crc = init
    for b in data:
        crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8)
    return crc ^ 0xFFFFFFFF


class AprotoClient:
    """High-level APROTO C2 client over TCP with SLIP framing."""

    def __init__(self, host: str, port: int = 9000, timeout: float = 5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self._sock: Optional[socket.socket] = None
        self._seq = 0
        self._rx_buf = bytearray()

    def __enter__(self):
        self.connect()
        return self

    def __exit__(self, *args):
        self.close()

    def connect(self) -> None:
        """Connect to target."""
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.settimeout(self.timeout)
        self._sock.connect((self.host, self.port))
        self._rx_buf.clear()

    def close(self) -> None:
        """Close connection."""
        if self._sock is not None:
            try:
                self._sock.close()
            except OSError:
                pass
            self._sock = None

    @property
    def connected(self) -> bool:
        return self._sock is not None

    def _next_seq(self) -> int:
        seq = self._seq
        self._seq = (self._seq + 1) & 0xFFFF
        return seq

    def _send_raw(self, packet: bytes) -> None:
        """SLIP-encode and send a packet."""
        if self._sock is None:
            raise ConnectionError("Not connected")
        encoded = proto.slip_encode(packet)
        self._sock.sendall(encoded)

    def _recv_response(self, timeout: Optional[float] = None) -> dict:
        """Receive and parse one APROTO response."""
        if self._sock is None:
            raise ConnectionError("Not connected")

        deadline = time.monotonic() + (timeout or self.timeout)
        while time.monotonic() < deadline:
            # Check for complete frames in buffer.
            frames = proto.slip_decode_stream(self._rx_buf)
            for frame in frames:
                parsed = proto.parse_header(frame)
                if parsed is not None and parsed["is_response"]:
                    return parsed

            # Read more data.
            remaining = max(0.1, deadline - time.monotonic())
            self._sock.settimeout(remaining)
            try:
                data = self._sock.recv(65536)
                if len(data) == 0:
                    raise ConnectionError("Connection closed by remote")
                self._rx_buf.extend(data)
            except socket.timeout:
                break

        raise TimeoutError("No response received within timeout")

    def send_command(
        self, full_uid: int, opcode: int, payload: bytes = b"", ack_req: bool = True
    ) -> dict:
        """Send command and wait for ACK/NAK response."""
        seq = self._next_seq()
        packet = proto.build_packet(full_uid, opcode, seq, payload, ack_req=ack_req)
        self._send_raw(packet)

        if not ack_req:
            return {"status": 0, "status_name": "SUCCESS"}

        resp = self._recv_response()
        ack = proto.parse_ack(resp.get("payload", b""))
        ack["raw_response"] = resp
        return ack

    # Basic commands

    def noop(self, full_uid: int = 0) -> dict:
        """Send NOOP command (connectivity test)."""
        return self.send_command(full_uid, proto.SYS_NOOP)

    def ping(self, full_uid: int = 0, data: bytes = b"PING") -> dict:
        """Send PING with echo payload."""
        return self.send_command(full_uid, proto.SYS_PING, data)

    def get_status(self, full_uid: int = 0) -> dict:
        """Get interface status."""
        return self.send_command(full_uid, proto.SYS_GET_STATUS)

    def command(self, full_uid: int, opcode: int, payload: bytes = b"") -> dict:
        """Send arbitrary command to a component."""
        return self.send_command(full_uid, opcode, payload)

    # Executive commands

    def get_health(self) -> dict:
        """Get executive health packet."""
        return self.send_command(0x000000, proto.EXEC_GET_HEALTH)

    def pause(self) -> dict:
        """Pause execution."""
        return self.send_command(0x000000, proto.EXEC_CMD_PAUSE)

    def resume(self) -> dict:
        """Resume execution."""
        return self.send_command(0x000000, proto.EXEC_CMD_RESUME)

    def request_shutdown(self) -> dict:
        """Request graceful shutdown."""
        return self.send_command(0x000000, proto.EXEC_CMD_SHUTDOWN)

    def sleep(self) -> dict:
        """Enter sleep mode (clock ticks, tasks paused)."""
        return self.send_command(0x000000, proto.EXEC_CMD_SLEEP)

    def wake(self) -> dict:
        """Exit sleep mode (resume task dispatch)."""
        return self.send_command(0x000000, proto.EXEC_CMD_WAKE)

    def get_clock_cycles(self) -> int:
        """Get current clock cycle count."""
        result = self.send_command(0x000000, proto.EXEC_GET_CLOCK_CYCLES)
        extra = result.get("extra", b"")
        if len(extra) >= 8:
            return struct.unpack("<Q", extra[:8])[0]
        return 0

    # Ground test / inspection

    def inspect(self, full_uid: int, category: int, offset: int = 0, length: int = 0) -> dict:
        """Read registered data from a component (ground test mode).

        Args:
            full_uid: Target component's fullUid (e.g., 0x007800 for plant model).
            category: DataCategory (0=STATIC_PARAM, 1=TUNABLE_PARAM, 2=STATE,
                      3=INPUT, 4=OUTPUT).
            offset: Byte offset within the data block (default: 0 = start).
            length: Number of bytes to read (default: 0 = entire block).

        Returns:
            dict with 'status', 'status_name', and 'extra' (raw bytes on success).
        """
        payload = struct.pack("<IBHH", full_uid, category, offset, length)
        return self.send_command(0x000000, proto.EXEC_INSPECT, payload)

    # File transfer

    def send_file(
        self,
        local_path: str,
        remote_path: str,
        chunk_size: int = 4096,
        progress_cb: Optional[Callable[[int, int], None]] = None,
    ) -> dict:
        """Transfer a file to the target filesystem.

        Args:
            local_path: Local file to send.
            remote_path: Destination path relative to .apex_fs/ root.
            chunk_size: Bytes per chunk (default 4096).
            progress_cb: Optional callback(chunks_sent, total_chunks).

        Returns:
            ACK/NAK result from FILE_END.
        """
        data = Path(local_path).read_bytes()
        total_size = len(data)
        total_chunks = (total_size + chunk_size - 1) // chunk_size if total_size > 0 else 1
        file_crc = crc32c(data)

        # FILE_BEGIN
        begin_payload = proto.build_file_begin(
            total_size, chunk_size, total_chunks, file_crc, remote_path
        )
        result = self.send_command(0, proto.FILE_BEGIN, begin_payload)
        if result.get("status", 0) != 0:
            return result

        # FILE_CHUNK (sequential)
        for i in range(total_chunks):
            offset = i * chunk_size
            chunk_data = data[offset : offset + chunk_size]
            chunk_payload = proto.build_file_chunk(i, chunk_data)
            result = self.send_command(0, proto.FILE_CHUNK, chunk_payload)
            if result.get("status", 0) != 0:
                # Abort on failure.
                self.send_command(0, proto.FILE_ABORT, ack_req=False)
                return result
            if progress_cb is not None:
                progress_cb(i + 1, total_chunks)

        # FILE_END
        result = self.send_command(0, proto.FILE_END)
        if result.get("status", 0) == 0:
            # Parse FileEndResponse from extra data.
            extra = result.get("extra", b"")
            if len(extra) >= 8:
                end_resp = proto.parse_file_end_response(extra)
                result["file_end"] = end_resp
        return result

    def abort_transfer(self) -> dict:
        """Abort current file transfer."""
        return self.send_command(0, proto.FILE_ABORT)

    def get_transfer_status(self) -> dict:
        """Get current file transfer status."""
        result = self.send_command(0, proto.FILE_STATUS)
        extra = result.get("extra", b"")
        if len(extra) >= 8:
            result["transfer"] = proto.parse_file_status_response(extra)
        return result

    # Runtime update commands

    def lock_component(self, full_uid: int) -> dict:
        """Lock component for runtime update."""
        payload = struct.pack("<I", full_uid)
        return self.send_command(0x000000, proto.EXEC_CMD_LOCK, payload)

    def unlock_component(self, full_uid: int) -> dict:
        """Unlock component after runtime update."""
        payload = struct.pack("<I", full_uid)
        return self.send_command(0x000000, proto.EXEC_CMD_UNLOCK, payload)

    def reload_tprm(self, full_uid: int) -> dict:
        """Reload TPRM for a component (file must already be on disk)."""
        payload = struct.pack("<I", full_uid)
        return self.send_command(0x000000, proto.EXEC_RELOAD_TPRM, payload)

    def reload_library(self, full_uid: int) -> dict:
        """Hot-swap component .so via dlopen (.so must already be on disk)."""
        payload = struct.pack("<I", full_uid)
        return self.send_command(0x000000, proto.EXEC_RELOAD_LIBRARY, payload)

    def reload_executive(self) -> dict:
        """Restart executive via execve.

        The process calls execv() before the ACK can be sent, so the
        connection will be closed by the remote side.  This method catches
        the expected ConnectionError / TimeoutError and returns a synthetic
        SUCCESS result.  The caller should reconnect after a brief delay.
        """
        try:
            return self.send_command(0x000000, proto.EXEC_RELOAD_EXECUTIVE)
        except (ConnectionError, TimeoutError):
            # Expected: execv replaced the process before ACK was sent.
            self._sock = None
            return {"status": 0, "status_name": "SUCCESS (execve)"}

    # Compound operations

    def update_tprm(
        self,
        full_uid: int,
        local_tprm_path: str,
        inactive_bank: str = "bank_b",
        progress_cb: Optional[Callable[[int, int], None]] = None,
    ) -> dict:
        """Transfer TPRM file to inactive bank and reload component parameters.

        Compound operation:
          1. Transfer TPRM file to {inactive_bank}/tprm/{fullUid:06x}.tprm
          2. Send RELOAD_TPRM command (executive loads from inactive, swaps banks)

        Args:
            full_uid: Component fullUid.
            local_tprm_path: Path to local .tprm file.
            inactive_bank: Inactive bank directory name ("bank_a" or "bank_b").
            progress_cb: Optional file transfer progress callback.

        Returns:
            Result of RELOAD_TPRM command.
        """
        remote_path = f"{inactive_bank}/tprm/{full_uid:06x}.tprm"
        result = self.send_file(local_tprm_path, remote_path, progress_cb=progress_cb)
        if result.get("status", 0) != 0:
            return result
        return self.reload_tprm(full_uid)

    def update_component(
        self,
        full_uid: int,
        local_so_path: str,
        component_name: str,
        instance_index: int = 0,
        inactive_bank: str = "bank_b",
        progress_cb: Optional[Callable[[int, int], None]] = None,
    ) -> dict:
        """Lock, transfer library to inactive bank, reload, and unlock a component.

        Compound operation:
          1. Lock component
          2. Transfer .so file to {inactive_bank}/libs/{name}_{index}.so
          3. Send RELOAD_LIBRARY (executive loads from inactive, swaps banks)
          4. Component auto-unlocked by executive on success

        On failure at any step, attempts to unlock the component.

        Args:
            full_uid: Component fullUid.
            local_so_path: Path to local .so file.
            component_name: Component name (e.g. "PolynomialModel").
            instance_index: Component instance index (default 0).
            inactive_bank: Inactive bank directory name ("bank_a" or "bank_b").
            progress_cb: Optional file transfer progress callback.

        Returns:
            Result of RELOAD_LIBRARY command.
        """
        so_filename = f"{component_name}_{instance_index}.so"
        remote_path = f"{inactive_bank}/libs/{so_filename}"

        # Lock
        result = self.lock_component(full_uid)
        if result.get("status", 0) != 0:
            return result

        try:
            # Transfer .so to inactive bank
            result = self.send_file(local_so_path, remote_path, progress_cb=progress_cb)
            if result.get("status", 0) != 0:
                self.unlock_component(full_uid)
                return result

            # Reload library (executive auto-unlocks on success)
            return self.reload_library(full_uid)
        except Exception:
            # Best-effort unlock on failure.
            try:
                self.unlock_component(full_uid)
            except Exception:
                pass
            raise
