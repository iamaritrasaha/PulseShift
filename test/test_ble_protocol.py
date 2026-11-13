#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Unit tests for PulseShift BLE Protocol v1 wire serialization and parsing."""

import struct
import unittest

PULSESHIFT_BLE_SERVICE_UUID = "fe12d210-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_RPM_UUID = "fe12d211-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_HEALTH_UUID = "fe12d212-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_LOGS_UUID = "fe12d213-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_CONTROL_UUID = "fe12d214-3d4f-4b9d-a21c-92782ecb4000"

# Status Flags
BLE_RPM_FLAG_CANDIDATE_FRESH    = 0x01
BLE_RPM_FLAG_CALIBRATION_OK     = 0x02
BLE_RPM_FLAG_RPM_MAPPED         = 0x04
BLE_RPM_FLAG_SHIFT_LIGHTS_ON    = 0x08
BLE_RPM_FLAG_CAN_HEALTHY        = 0x10
BLE_RPM_FLAG_TRAFFIC_ACTIVE     = 0x20
BLE_RPM_FLAG_RECORDING_ACTIVE   = 0x40
BLE_RPM_FLAG_CAPTURE_PRESERVED  = 0x80

# Health Flags
BLE_HEALTH_FLAG_STORAGE_MOUNTED  = 0x01
BLE_HEALTH_FLAG_STORAGE_FULL     = 0x02
BLE_HEALTH_FLAG_RX_DEGRADED      = 0x04
BLE_HEALTH_FLAG_CLIENT_CONNECTED = 0x08

# Log Flags
BLE_LOG_FLAG_FIRST_CHUNK  = 0x01
BLE_LOG_FLAG_FINAL_CHUNK  = 0x02
BLE_LOG_FLAG_END_OF_PAGE  = 0x04

# Control Commands
BLE_CTRL_CMD_GET_LOG_PAGE = 0x10
BLE_CTRL_CMD_CANCEL_LOGS  = 0x11


class TestBleProtocol(unittest.TestCase):
    def test_rpm_packet_packing_and_size(self):
        fmt = "<BBHIHBB IHH".replace(" ", "")
        self.assertEqual(struct.calcsize(fmt), 20)

        data = struct.pack(
            fmt,
            1,       # version
            0x71,    # status: Fresh | Healthy | Traffic | Recording
            42,      # seq
            12345,   # uptime ms
            1760,    # provisional RPM
            44,      # raw byte
            2,       # stage: Idling
            1500,    # can frames rx
            20,      # candidate age ms
            0        # reserved
        )

        expected_hex = "01712a0039300000e0062c02dc05000014000000"
        self.assertEqual(data.hex(), expected_hex)
        self.assertEqual(len(data), 20)

        unpacked = struct.unpack(fmt, data)
        self.assertEqual(unpacked[0], 1)
        self.assertEqual(unpacked[1], 0x71)
        self.assertEqual(unpacked[2], 42)
        self.assertEqual(unpacked[3], 12345)
        self.assertEqual(unpacked[4], 1760)
        self.assertEqual(unpacked[5], 44)
        self.assertEqual(unpacked[6], 2)
        self.assertEqual(unpacked[7], 1500)
        self.assertEqual(unpacked[8], 20)
        self.assertEqual(unpacked[9], 0)

    def test_rpm_packet_waiting_state(self):
        fmt = "<BBHIHBB IHH".replace(" ", "")
        data = struct.pack(
            fmt,
            1,       # version
            0x90,    # status: Healthy | Capture Preserved
            1,       # seq
            5000,    # uptime ms
            65535,   # provisional RPM: unavailable
            0,       # raw byte
            1,       # stage: KeyOn
            0,       # can frames rx
            65535,   # candidate age ms: unavailable
            0        # reserved
        )

        expected_hex = "0190010088130000ffff000100000000ffff0000"
        self.assertEqual(data.hex(), expected_hex)
        self.assertEqual(len(data), 20)

    def test_health_packet_packing_and_size(self):
        fmt = "<BBHIIII"
        self.assertEqual(struct.calcsize(fmt), 20)

        data = struct.pack(
            fmt,
            1,       # version
            0x09,    # flags: Storage Mounted | Client Connected
            10,      # seq
            0,       # missed
            0,       # overrun
            0,       # bus errors
            23370    # recorded frames
        )

        expected_hex = "01090a000000000000000000000000004a5b0000"
        self.assertEqual(data.hex(), expected_hex)
        self.assertEqual(len(data), 20)

        unpacked = struct.unpack(fmt, data)
        self.assertEqual(unpacked[0], 1)
        self.assertEqual(unpacked[1], 0x09)
        self.assertEqual(unpacked[2], 10)
        self.assertEqual(unpacked[3], 0)
        self.assertEqual(unpacked[4], 0)
        self.assertEqual(unpacked[5], 0)
        self.assertEqual(unpacked[6], 23370)

    def test_log_chunk_packet_single_chunk(self):
        fmt = "<BBHBB14s"
        self.assertEqual(struct.calcsize(fmt), 20)

        payload_bytes = b"Hello, BLE Log" # exactly 14 bytes
        padded_payload = payload_bytes.ljust(14, b"\x00")

        data = struct.pack(
            fmt,
            1,                             # version
            BLE_LOG_FLAG_FIRST_CHUNK | BLE_LOG_FLAG_FINAL_CHUNK, # flags
            101,                           # messageId
            0,                             # chunkIndex must be 0 for FIRST_CHUNK
            len(payload_bytes),            # payloadLength
            padded_payload                 # payload[14]
        )

        self.assertEqual(len(data), 20)
        unpacked = struct.unpack(fmt, data)
        self.assertEqual(unpacked[0], 1)
        self.assertEqual(unpacked[1], 0x03)
        self.assertEqual(unpacked[2], 101)
        self.assertEqual(unpacked[3], 0)
        self.assertEqual(unpacked[4], 14)
        self.assertEqual(unpacked[5][:len(payload_bytes)], payload_bytes)

    def test_log_chunk_multi_chunk_fragmentation(self):
        # Test 15-byte message: chunk 0 (14 bytes) and chunk 1 (1 byte)
        msg = b"A" * 15
        fmt = "<BBHBB14s"

        # Chunk 0
        chunk0 = struct.pack(
            fmt,
            1,
            BLE_LOG_FLAG_FIRST_CHUNK, # only first chunk
            200,
            0, # index 0
            14,
            msg[:14]
        )
        u0 = struct.unpack(fmt, chunk0)
        self.assertEqual(u0[1], BLE_LOG_FLAG_FIRST_CHUNK)
        self.assertEqual(u0[3], 0)
        self.assertEqual(u0[4], 14)

        # Chunk 1
        chunk1 = struct.pack(
            fmt,
            1,
            BLE_LOG_FLAG_FINAL_CHUNK, # only final chunk
            200,
            1, # index 1
            1,
            msg[14:].ljust(14, b"\x00")
        )
        u1 = struct.unpack(fmt, chunk1)
        self.assertEqual(u1[1], BLE_LOG_FLAG_FINAL_CHUNK)
        self.assertEqual(u1[3], 1)
        self.assertEqual(u1[4], 1)

    def test_log_end_of_page_packet(self):
        fmt = "<BBHBB14s"
        # EOP payload: offset (4 bytes) + finished (1 byte) + padding (9 bytes)
        next_offset = 8192
        finished = 0
        eop_payload = struct.pack("<IB", next_offset, finished).ljust(14, b"\x00")

        packet = struct.pack(
            fmt,
            1,
            BLE_LOG_FLAG_END_OF_PAGE | BLE_LOG_FLAG_FINAL_CHUNK,
            300,
            0,
            5,
            eop_payload
        )
        self.assertEqual(len(packet), 20)
        u = struct.unpack(fmt, packet)
        self.assertEqual(u[1], BLE_LOG_FLAG_END_OF_PAGE | BLE_LOG_FLAG_FINAL_CHUNK)
        offset_out, fin_out = struct.unpack("<IB", u[5][:5])
        self.assertEqual(offset_out, 8192)
        self.assertEqual(fin_out, 0)

    def test_control_request_parsing(self):
        # Command GET_LOG_PAGE: exactly 6 bytes [version=0x01, opcode=0x10, offset_u32_LE]
        offset = 4096
        cmd = struct.pack("<BBI", 0x01, BLE_CTRL_CMD_GET_LOG_PAGE, offset)
        self.assertEqual(len(cmd), 6)
        self.assertEqual(cmd[0], 0x01)
        self.assertEqual(cmd[1], 0x10)
        self.assertEqual(struct.unpack("<I", cmd[2:6])[0], 4096)
        self.assertEqual(cmd.hex(), "011000100000")

        # Command CANCEL_LOGS: exactly 2 bytes [version=0x01, opcode=0x11]
        cancel_cmd = struct.pack("<BB", 0x01, BLE_CTRL_CMD_CANCEL_LOGS)
        self.assertEqual(len(cancel_cmd), 2)
        self.assertEqual(cancel_cmd[0], 0x01)
        self.assertEqual(cancel_cmd[1], 0x11)
        self.assertEqual(cancel_cmd.hex(), "0111")


if __name__ == "__main__":
    unittest.main()
