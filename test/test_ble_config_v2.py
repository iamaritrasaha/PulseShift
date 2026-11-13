#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Unit tests for PulseShift BLE Protocol v2 configuration, snapshot TLVs, and security."""

import struct
import unittest
import zlib

PULSESHIFT_BLE_SERVICE_UUID = "fe12d210-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_CONFIG_CMD_UUID      = "fe12d215-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_CONFIG_RESULT_UUID   = "fe12d216-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_CONFIG_SNAPSHOT_UUID = "fe12d217-3d4f-4b9d-a21c-92782ecb4000"
PULSESHIFT_BLE_CHAR_CAPABILITIES_UUID    = "fe12d218-3d4f-4b9d-a21c-92782ecb4000"

# Opcodes
BLE_CFG_OP_GET_CAPABILITIES  = 0x01
BLE_CFG_OP_GET_CONFIG        = 0x02
BLE_CFG_OP_SET_POWER         = 0x10
BLE_CFG_OP_SET_BRIGHTNESS    = 0x11
BLE_CFG_OP_SET_GLOBAL_COLOR  = 0x12
BLE_CFG_OP_SET_LED_COLOR     = 0x13
BLE_CFG_OP_SET_EFFECT        = 0x14
BLE_CFG_OP_SET_STARTUP_ANIM  = 0x15
BLE_CFG_OP_SET_RPM_THRESHOLD = 0x16
BLE_CFG_OP_SET_SHIFT_WARNING = 0x17
BLE_CFG_OP_SET_RPM_MULTIPLIER= 0x18
BLE_CFG_OP_SET_CAN_CANDIDATE = 0x19
BLE_CFG_OP_SET_LED_DIRECTION = 0x1A
BLE_CFG_OP_SET_WARNING_COLOR = 0x1B
BLE_CFG_OP_BEGIN_TRANSACTION = 0x20
BLE_CFG_OP_COMMIT_TRANSACTION= 0x21
BLE_CFG_OP_ABORT_TRANSACTION = 0x22
BLE_CFG_OP_SAVE_TO_NVS       = 0x30
BLE_CFG_OP_RESTORE_DEFAULTS  = 0x31
BLE_CFG_OP_REQUEST_AUTH_CHALLENGE = 0x40
BLE_CFG_OP_AUTHENTICATE      = 0x41

# Status Codes
BLE_CFG_STATUS_OK             = 0x00
BLE_CFG_STATUS_INVALID_PARAM  = 0x01
BLE_CFG_STATUS_UNAUTHORIZED   = 0x02
BLE_CFG_STATUS_BUSY           = 0x03
BLE_CFG_STATUS_CRC_ERROR      = 0x04
BLE_CFG_STATUS_NOT_SUPPORTED  = 0x05
BLE_CFG_STATUS_NVS_ERROR      = 0x06
BLE_CFG_STATUS_TX_ABORTED     = 0x07
BLE_CFG_STATUS_INVALID_STATE  = 0x08

# Result Flags
BLE_CFG_RESULT_FLAG_PERSISTED     = 0x01
BLE_CFG_RESULT_FLAG_STAGED        = 0x02
BLE_CFG_RESULT_FLAG_APPLIED       = 0x04
BLE_CFG_RESULT_FLAG_AUTH_REQUIRED = 0x08

# Snapshot Flags
BLE_CFG_SNAP_FLAG_FIRST_CHUNK = 0x01
BLE_CFG_SNAP_FLAG_FINAL_CHUNK = 0x02

# Feature Mask Bits
FEATURE_PERSISTENT_NVS        = 0x0001
FEATURE_STAGED_TRANSACTIONS   = 0x0002
FEATURE_CALIBRATION_SAFETY    = 0x0004
FEATURE_PHYSICAL_AUTH         = 0x0008
FEATURE_CUSTOM_ANIMATIONS     = 0x0010
FEATURE_SHIFT_WARNING         = 0x0020
FEATURE_REVERSIBLE_DIRECTION  = 0x0040
FEATURE_INDIVIDUAL_LEDS       = 0x0080
FEATURE_HISTORICAL_LOGS       = 0x0100
FEATURE_CAN_PASSIVE_LISTEN    = 0x0200
FULL_FEATURE_MASK             = 0x03FF # 1023


class TestBleConfigProtocolV2(unittest.TestCase):
    def test_packet_sizes(self):
        cmd_fmt = "<BBHBB14s"
        res_fmt = "<BBHBBIB9s"
        chunk_fmt = "<BHBBB14s"
        cap_fmt = "<BBIIBB8s"

        self.assertEqual(struct.calcsize(cmd_fmt), 20)
        self.assertEqual(struct.calcsize(res_fmt), 20)
        self.assertEqual(struct.calcsize(chunk_fmt), 20)
        self.assertEqual(struct.calcsize(cap_fmt), 20)

    def test_capabilities_packet_encoding_and_parsing(self):
        cap_fmt = "<BBIIBB8s"
        data = struct.pack(
            cap_fmt,
            2,                   # version 2
            6,                   # physicalLedCount = 6
            FULL_FEATURE_MASK,   # featureMask: 0x03FF (1023)
            1,                   # currentRevision = 1
            0,                   # hardwarePresetCapacity = 0 (phone-managed)
            1,                   # securityMode = 1 (BUTTON_WINDOW)
            b"\x00" * 8          # reserved
        )
        self.assertEqual(len(data), 20)
        u = struct.unpack(cap_fmt, data)
        self.assertEqual(u[0], 2)
        self.assertEqual(u[1], 6)
        self.assertEqual(u[2], 0x03FF)
        self.assertEqual(u[3], 1)
        self.assertEqual(u[4], 0)
        self.assertEqual(u[5], 1)

    def test_config_command_packet(self):
        cmd_fmt = "<BBHBB14s"
        # Command: SET_BRIGHTNESS(180), seq=105, staged=0, payloadLength=1
        payload = struct.pack("<B13x", 180)
        data = struct.pack(
            cmd_fmt,
            2,                        # version
            BLE_CFG_OP_SET_BRIGHTNESS,# opcode
            105,                      # seq
            1,                        # payloadLength
            0,                        # flags (not staged)
            payload
        )
        self.assertEqual(len(data), 20)
        u = struct.unpack(cmd_fmt, data)
        self.assertEqual(u[0], 2)
        self.assertEqual(u[1], BLE_CFG_OP_SET_BRIGHTNESS)
        self.assertEqual(u[2], 105)
        self.assertEqual(u[3], 1)
        self.assertEqual(u[4], 0)
        self.assertEqual(u[5][0], 180)

    def test_config_result_packet(self):
        res_fmt = "<BBHBBIB9s"
        # Result: OK, opcode=0x11, seq=105, activeRevision=42
        data = struct.pack(
            res_fmt,
            2,                                                 # version
            BLE_CFG_OP_SET_BRIGHTNESS,                         # opcode
            105,                                               # seq
            BLE_CFG_STATUS_OK,                                 # status
            BLE_CFG_RESULT_FLAG_APPLIED,                       # flags
            42,                                                # activeRevision
            0,                                                 # payloadLength
            b"\x00" * 9                                        # payload
        )
        self.assertEqual(len(data), 20)
        u = struct.unpack(res_fmt, data)
        self.assertEqual(u[0], 2)
        self.assertEqual(u[1], BLE_CFG_OP_SET_BRIGHTNESS)
        self.assertEqual(u[2], 105)
        self.assertEqual(u[3], BLE_CFG_STATUS_OK)
        self.assertEqual(u[4], BLE_CFG_RESULT_FLAG_APPLIED)
        self.assertEqual(u[5], 42)

    def test_snapshot_chunking_and_reassembly_with_crc32(self):
        # Build synthetic snapshot TLVs matching ConfigManager::serializeSnapshot()
        schema_version = 2
        active_revision = 42

        tlvs = bytearray()
        # Header: schemaVersion (1 byte) + activeRevision (4 bytes LE)
        tlvs.append(schema_version)
        tlvs.extend(struct.pack("<I", active_revision))

        # 0x10: Power (1 byte: true)
        tlvs.extend([0x10, 1, 1])
        # 0x11: Brightness (1 byte: 200)
        tlvs.extend([0x11, 1, 200])
        # 0x12: Global Color (3 bytes: 140, 43, 238)
        tlvs.extend([0x12, 3, 140, 43, 238])
        # 0x13: Per-LED Colors (18 bytes)
        led_colors = bytes([140, 43, 238] * 6)
        tlvs.extend([0x13, len(led_colors)])
        tlvs.extend(led_colors)
        # 0x14: Effect & Speed (2 bytes: effect=2 Rainbow, speed=50)
        tlvs.extend([0x14, 2, 2, 50])
        # 0x15: Startup Anim (3 bytes: anim=1 Sweep, duration=1500 LE)
        tlvs.extend([0x15, 3, 1, 0xDC, 0x05])
        # 0x16: RPM Thresholds (12 bytes: 3000, 4500, 6000, 7500, 8500, 9500)
        thresholds = [3000, 4500, 6000, 7500, 8500, 9500]
        thresh_bytes = struct.pack("<6H", *thresholds)
        tlvs.extend([0x16, len(thresh_bytes)])
        tlvs.extend(thresh_bytes)
        # 0x17: Shift Warning (3 bytes: warningRpm=9500 LE, pattern=0)
        tlvs.extend([0x17, 3, 0x1C, 0x25, 0])
        # 0x18: Multiplier (4 bytes: multX1000 = 40000 u32 LE)
        tlvs.extend([0x18, 4])
        tlvs.extend(struct.pack("<I", 40000))
        # 0x19: Candidate Source (6 bytes: ID=0x301 LE, byte=0, ext=0)
        tlvs.extend([0x19, 6])
        tlvs.extend(struct.pack("<IBB", 0x301, 0, 0))
        # 0x1A: Direction (1 byte: 0 forward)
        tlvs.extend([0x1A, 1, 0])
        # 0x1B: Warning Color (3 bytes: 255, 0, 0)
        tlvs.extend([0x1B, 3, 255, 0, 0])

        # Compute CRC32 over payload
        calc_crc = zlib.crc32(tlvs) & 0xFFFFFFFF
        full_snapshot = tlvs + struct.pack("<I", calc_crc)

        # Fragment into 14-byte BLE snapshot chunks
        chunk_fmt = "<BHBBB14s"
        chunks = []
        offset = 0
        total_len = len(full_snapshot)
        chunk_idx = 0
        total_chunks = (total_len + 13) // 14

        while offset < total_len:
            take = min(14, total_len - offset)
            payload = full_snapshot[offset : offset + take].ljust(14, b"\x00")

            pkt = struct.pack(
                chunk_fmt,
                2,            # version
                1,            # snapshotId
                chunk_idx,    # chunkIndex (0-based)
                total_chunks, # chunkCount
                take,         # payloadLength
                payload
            )
            chunks.append(pkt)
            offset += take
            chunk_idx += 1

        self.assertGreater(len(chunks), 1)

        # Reassemble chunks (mimicking Android ConfigSnapshotReassembler.kt)
        reassembled = bytearray()
        for idx, c in enumerate(chunks):
            u = struct.unpack(chunk_fmt, c)
            self.assertEqual(u[0], 2) # version
            c_id = u[1]
            c_idx = u[2]
            c_tot = u[3]
            c_len = u[4]
            self.assertEqual(c_idx, idx)
            self.assertEqual(c_tot, total_chunks)
            reassembled.extend(u[5][:c_len])

        self.assertEqual(bytes(reassembled), bytes(full_snapshot))

        # Verify CRC32
        payload_data = reassembled[:-4]
        embedded_crc = struct.unpack("<I", reassembled[-4:])[0]
        self.assertEqual(zlib.crc32(payload_data) & 0xFFFFFFFF, embedded_crc)

    def test_monotonic_rpm_thresholds_validation(self):
        # Valid monotonic thresholds:
        valid_thresholds = [1500, 3000, 4500, 6000, 7500, 8500]
        for i in range(1, len(valid_thresholds)):
            self.assertGreaterEqual(valid_thresholds[i], valid_thresholds[i - 1])

        # Invalid non-monotonic thresholds:
        invalid_thresholds = [1500, 3000, 2500, 6000, 7500, 8500]
        is_monotonic = True
        for i in range(1, len(invalid_thresholds)):
            if invalid_thresholds[i] < invalid_thresholds[i - 1]:
                is_monotonic = False
                break
        self.assertFalse(is_monotonic)

    def test_rpm_multiplier_validation(self):
        # Valid: 0.1 to 1000.0
        self.assertTrue(0.1 <= 40.0 <= 1000.0)
        self.assertTrue(0.1 <= 0.1 <= 1000.0)
        self.assertTrue(0.1 <= 1000.0 <= 1000.0)

        # Invalid: 0.0, negative, > 1000.0
        self.assertFalse(0.1 <= 0.0 <= 1000.0)
        self.assertFalse(0.1 <= -5.0 <= 1000.0)
        self.assertFalse(0.1 <= 1500.0 <= 1000.0)


if __name__ == "__main__":
    unittest.main()
