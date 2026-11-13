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


class TestBleProtocol(unittest.TestCase):
    def test_rpm_packet_packing_and_size(self):
        # Format: < B B H I H B B I H H
        # Version (1B), Status (1B), Seq (2B), Uptime (4B), RPM (2B),
        # Raw (1B), Stage (1B), Rx (4B), Age (2B), Reserved (2B)
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

        # Unpack verification
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
        # Format: < B B H I I I I
        # Version (1B), Flags (1B), Seq (2B), Missed (4B), Overrun (4B), Errors (4B), Recorded (4B)
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

    def test_log_chunk_packet_packing(self):
        # Format: < B B H B B 14s
        fmt = "<BBHBB14s"
        self.assertEqual(struct.calcsize(fmt), 20)

        payload_bytes = b"Hello, BLE Log"
        padded_payload = payload_bytes.ljust(14, b"\x00")

        data = struct.pack(
            fmt,
            1,                             # version
            BLE_LOG_FLAG_FIRST_CHUNK | BLE_LOG_FLAG_FINAL_CHUNK, # flags
            101,                           # messageId
            0,                             # chunkIndex
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

    def test_control_request_parsing(self):
        # Command GET_LOG_PAGE (opcode 0x01) with offset 4096 (0x1000)
        cmd = struct.pack("<BI", 0x01, 4096)
        self.assertEqual(cmd[0], 0x01)
        offset = struct.unpack("<I", cmd[1:5])[0]
        self.assertEqual(offset, 4096)


if __name__ == "__main__":
    unittest.main()
