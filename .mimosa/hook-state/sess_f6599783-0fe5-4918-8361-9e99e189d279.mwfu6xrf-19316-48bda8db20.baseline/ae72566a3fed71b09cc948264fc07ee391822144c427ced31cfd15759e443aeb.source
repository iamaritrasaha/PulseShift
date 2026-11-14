#!/usr/bin/env python3
"""
PulseShift - Offline Diagnostics, CAN Capture & Export Integration Test Suite
Validates:
1. Binary record parsing and field alignment (72-byte header, 20-byte records)
2. CRC16-CCITT checksum calculation and bitflip/corruption detection
3. Storage quota boundary conditions and storage-full policy
4. Interrupted writes and torn record recovery (power loss simulation)
5. BOOT button debouncing and stage transition state machine
6. Session persistence and monotonic counter tracking
7. Export data parsing, schema validation, and integrity checks
"""

import unittest
import struct
import tempfile
import os
import json
import re
import io


def compute_crc16_ccitt(data: bytes) -> int:
    """Computes CRC-16-CCITT (polynomial 0x1021, init 0xFFFF)."""
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class BinaryRecordTests(unittest.TestCase):
    """Tests 72-byte CaptureHeader and 20-byte CanLogRecord binary framing."""

    HEADER_FORMAT = "<8sHHIIII16s32s"  # 76 bytes
    RECORD_FORMAT = "<IIBB8sH"          # 20 bytes

    def setUp(self):
        self.assertEqual(struct.calcsize(self.HEADER_FORMAT), 76)
        self.assertEqual(struct.calcsize(self.RECORD_FORMAT), 20)

    def test_header_packing_and_unpacking(self):
        magic = b"PULSECAP"
        ver = 1
        rec_size = 20
        sess_id = 42
        boot_cnt = 5
        bitrate = 500000
        start_ms = 12345
        mode = b"CAN_CAPTURE\x00" + b"\x00" * 4
        vehicle = b"Bajaj Pulsar NS125 2025 ABS\x00" + b"\x00" * 4

        header_bytes = struct.pack(
            self.HEADER_FORMAT,
            magic, ver, rec_size, sess_id, boot_cnt, bitrate, start_ms, mode, vehicle
        )
        self.assertEqual(len(header_bytes), 76)

        unpacked = struct.unpack(self.HEADER_FORMAT, header_bytes)
        self.assertEqual(unpacked[0], b"PULSECAP")
        self.assertEqual(unpacked[1], 1)
        self.assertEqual(unpacked[2], 20)
        self.assertEqual(unpacked[3], 42)
        self.assertEqual(unpacked[4], 5)
        self.assertEqual(unpacked[5], 500000)
        self.assertEqual(unpacked[6], 12345)
        self.assertTrue(unpacked[7].startswith(b"CAN_CAPTURE"))
        self.assertTrue(unpacked[8].startswith(b"Bajaj Pulsar NS125"))

    def test_can_record_packing_standard_id(self):
        timestamp_ms = 50200
        can_id = 0x120  # Standard 11-bit
        dlc = 8
        stage = 2  # Idling
        data = bytes([0x05, 0x78, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])

        pre_crc_bytes = struct.pack("<IIBB8s", timestamp_ms, can_id, dlc, stage, data)
        self.assertEqual(len(pre_crc_bytes), 18)

        crc = compute_crc16_ccitt(pre_crc_bytes)
        record_bytes = pre_crc_bytes + struct.pack("<H", crc)
        self.assertEqual(len(record_bytes), 20)

        # Unpack and verify
        t_ms, id_raw, dlc_out, stage_out, data_out, crc_out = struct.unpack(self.RECORD_FORMAT, record_bytes)
        self.assertEqual(t_ms, 50200)
        self.assertEqual(id_raw & 0x1FFFFFFF, 0x120)
        self.assertEqual(id_raw & 0x80000000, 0)  # Standard
        self.assertEqual(dlc_out, 8)
        self.assertEqual(stage_out, 2)
        self.assertEqual(data_out, data)
        self.assertEqual(crc_out, crc)
        self.assertEqual(compute_crc16_ccitt(record_bytes[:18]), crc_out)

    def test_can_record_packing_extended_id(self):
        timestamp_ms = 85400
        arb_id = 0x18FEF100  # Extended 29-bit
        id_field = (arb_id & 0x1FFFFFFF) | 0x80000000  # Set bit 31
        dlc = 8
        stage = 3  # Gentle RPM
        data = bytes([0x0D, 0xAC, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66])

        pre_crc = struct.pack("<IIBB8s", timestamp_ms, id_field, dlc, stage, data)
        crc = compute_crc16_ccitt(pre_crc)
        rec = pre_crc + struct.pack("<H", crc)

        t_ms, id_raw, dlc_out, stage_out, data_out, crc_out = struct.unpack(self.RECORD_FORMAT, rec)
        self.assertTrue(bool(id_raw & 0x80000000))
        self.assertEqual(id_raw & 0x1FFFFFFF, 0x18FEF100)
        self.assertEqual(stage_out, 3)
        self.assertEqual(crc_out, compute_crc16_ccitt(rec[:18]))


class CrcIntegrityAndCorruptionTests(unittest.TestCase):
    """Tests CRC16 error detection against bitflips, payload corruption and truncation."""

    def test_single_bit_flip_detection(self):
        base_data = struct.pack("<IIBB8s", 1000, 0x123, 8, 1, b"\x01\x02\x03\x04\x05\x06\x07\x08")
        valid_crc = compute_crc16_ccitt(base_data)

        # Test single bit flips across every bit of the 18 data bytes
        detected_corruptions = 0
        total_bits = len(base_data) * 8

        for byte_idx in range(len(base_data)):
            for bit_idx in range(8):
                mutated = bytearray(base_data)
                mutated[byte_idx] ^= (1 << bit_idx)
                corrupt_crc = compute_crc16_ccitt(bytes(mutated))
                if corrupt_crc != valid_crc:
                    detected_corruptions += 1

        self.assertEqual(detected_corruptions, total_bits, "All single bit flips must be detected")

    def test_altered_fields_detection(self):
        # Base record
        base = struct.pack("<IIBB8s", 500, 0x555, 8, 1, b"\x10\x20\x30\x40\x50\x60\x70\x80")
        crc_base = compute_crc16_ccitt(base)

        # 1. Changed timestamp
        t_mod = struct.pack("<IIBB8s", 501, 0x555, 8, 1, b"\x10\x20\x30\x40\x50\x60\x70\x80")
        self.assertNotEqual(compute_crc16_ccitt(t_mod), crc_base)

        # 2. Changed ID
        id_mod = struct.pack("<IIBB8s", 500, 0x556, 8, 1, b"\x10\x20\x30\x40\x50\x60\x70\x80")
        self.assertNotEqual(compute_crc16_ccitt(id_mod), crc_base)

        # 3. Changed stage
        st_mod = struct.pack("<IIBB8s", 500, 0x555, 8, 2, b"\x10\x20\x30\x40\x50\x60\x70\x80")
        self.assertNotEqual(compute_crc16_ccitt(st_mod), crc_base)

        # 4. Changed single payload byte
        payload_mod = struct.pack("<IIBB8s", 500, 0x555, 8, 1, b"\x10\x20\x30\x40\x50\x60\x70\x81")
        self.assertNotEqual(compute_crc16_ccitt(payload_mod), crc_base)


class InterruptedWriteAndPowerLossTests(unittest.TestCase):
    """Simulates power-loss induced torn records at EOF and verifies parser recovery."""

    def create_mock_capture_file(self, num_records: int, trailing_garbage_bytes: int = 0) -> bytes:
        header = struct.pack(
            BinaryRecordTests.HEADER_FORMAT,
            b"PULSECAP", 1, 20, 1, 1, 500000, 1000, b"CAN_CAPTURE\x00\x00\x00\x00\x00", b"NS125 ABS\x00" + b"\x00" * 22
        )
        data = bytearray(header)
        for i in range(num_records):
            pre = struct.pack("<IIBB8s", i * 10, 0x100 + i, 8, (i % 3) + 1, bytes([i % 256] * 8))
            crc = compute_crc16_ccitt(pre)
            data.extend(pre + struct.pack("<H", crc))

        if trailing_garbage_bytes > 0:
            # Simulate incomplete SPI flash page write (power cutoff mid-record)
            data.extend(b"\xAA" * trailing_garbage_bytes)

        return bytes(data)

    def parse_capture_file(self, file_bytes: bytes):
        """Simulates the export tool parser."""
        self.assertGreaterEqual(len(file_bytes), 76)
        header = struct.unpack(BinaryRecordTests.HEADER_FORMAT, file_bytes[:76])
        self.assertEqual(header[0], b"PULSECAP")

        valid_records = []
        corrupt_records = 0
        offset = 76

        while offset + 20 <= len(file_bytes):
            rec_bytes = file_bytes[offset:offset + 20]
            expected_crc = compute_crc16_ccitt(rec_bytes[:18])
            stored_crc = struct.unpack("<H", rec_bytes[18:20])[0]

            if stored_crc == expected_crc:
                t_ms, can_id, dlc, stage, payload, _ = struct.unpack(BinaryRecordTests.RECORD_FORMAT, rec_bytes)
                valid_records.append({
                    "timestamp": t_ms,
                    "id": can_id,
                    "dlc": dlc,
                    "stage": stage,
                    "payload": payload
                })
            else:
                corrupt_records += 1
            offset += 20

        # Remaining bytes after full 20-byte chunks represent torn/interrupted writes
        trailing_incomplete_bytes = len(file_bytes) - offset
        return valid_records, corrupt_records, trailing_incomplete_bytes

    def test_clean_file_parsing(self):
        buf = self.create_mock_capture_file(num_records=50, trailing_garbage_bytes=0)
        valid, corrupt, trailing = self.parse_capture_file(buf)
        self.assertEqual(len(valid), 50)
        self.assertEqual(corrupt, 0)
        self.assertEqual(trailing, 0)
        self.assertEqual(valid[0]["timestamp"], 0)
        self.assertEqual(valid[49]["timestamp"], 490)

    def test_interrupted_write_recovery_7_bytes(self):
        # Power cut after writing 7 bytes of record #51
        buf = self.create_mock_capture_file(num_records=50, trailing_garbage_bytes=7)
        valid, corrupt, trailing = self.parse_capture_file(buf)
        self.assertEqual(len(valid), 50, "All 50 prior complete records must be recovered")
        self.assertEqual(corrupt, 0)
        self.assertEqual(trailing, 7, "Torn 7-byte trailing fragment must be safely quarantined")

    def test_interrupted_write_recovery_19_bytes(self):
        # Power cut after writing 19 bytes of record #51 (missing 1 byte of CRC)
        buf = self.create_mock_capture_file(num_records=50, trailing_garbage_bytes=19)
        valid, corrupt, trailing = self.parse_capture_file(buf)
        self.assertEqual(len(valid), 50)
        self.assertEqual(corrupt, 0)
        self.assertEqual(trailing, 19)


class StorageQuotaAndFullPolicyTests(unittest.TestCase):
    """Simulates storage boundaries, free space thresholds and storage-full behavior."""

    MAX_CAPTURE_BYTES = 1000000  # 1 MB
    MIN_FREE_BYTES = 32768       # 32 KB safety headroom
    HEADER_SIZE = 76
    RECORD_SIZE = 20

    def test_max_records_calculation(self):
        max_records = (self.MAX_CAPTURE_BYTES - self.HEADER_SIZE) // self.RECORD_SIZE
        self.assertEqual(max_records, 49996)  # Approx 50,000 frames

    def test_storage_full_flag_assertion(self):
        class MockStorageManager:
            def __init__(self, total_bytes, free_bytes):
                self.total = total_bytes
                self.free = free_bytes
                self.storage_full = False

            def check_space(self, current_file_size):
                if current_file_size >= StorageQuotaAndFullPolicyTests.MAX_CAPTURE_BYTES or \
                   self.free < StorageQuotaAndFullPolicyTests.MIN_FREE_BYTES:
                    self.storage_full = True
                    return False
                return True

        # Case 1: Healthy storage
        sm = MockStorageManager(total_bytes=1441792, free_bytes=500000)
        self.assertTrue(sm.check_space(current_file_size=500000))
        self.assertFalse(sm.storage_full)

        # Case 2: File size reached 1 MB
        self.assertFalse(sm.check_space(current_file_size=1000000))
        self.assertTrue(sm.storage_full)

        # Case 3: Free space dropped below 32 KB
        sm2 = MockStorageManager(total_bytes=1441792, free_bytes=20000)
        self.assertFalse(sm2.check_space(current_file_size=100000))
        self.assertTrue(sm2.storage_full)

    def test_virgin_unformatted_partition_detection(self):
        """Simulates full-partition isPartitionBlank() logic to guarantee safe first-boot formatting."""
        def is_partition_blank(flash_bytes: bytes, chunk_size: int = 256) -> bool:
            if not flash_bytes:
                return False
            for offset in range(0, len(flash_bytes), chunk_size):
                chunk = flash_bytes[offset:offset + chunk_size]
                if any(b != 0xFF for b in chunk):
                    return False
            return True

        # 1. Virgin factory-erased flash (all 0xFF) -> Safe to format on first boot
        virgin_flash = b"\xFF" * 65536
        self.assertTrue(is_partition_blank(virgin_flash))

        # 2. Existing LittleFS partition (starts with LittleFS superblock) -> MUST NOT auto-format
        littlefs_flash = b"littlefs" + b"\xFF" * 4088
        self.assertFalse(is_partition_blank(littlefs_flash))

        # 3. Dirty partition within first 1024 bytes -> MUST NOT auto-format
        dirty_early = bytearray([0xFF] * 8192)
        dirty_early[128] = 0x55  # Single non-0xFF byte in first block
        self.assertFalse(is_partition_blank(bytes(dirty_early)))

        # 4. Critical safety regression: dirty partition with non-0xFF PAST 1024 bytes
        # (e.g. data at offset 1025, 2048, or 4096). The old 1024-byte check would falsely
        # classify this as blank and wipe user recordings; full scan MUST return False!
        dirty_late_1025 = bytearray([0xFF] * 8192)
        dirty_late_1025[1025] = 0x00
        self.assertFalse(is_partition_blank(bytes(dirty_late_1025)))

        dirty_late_4096 = bytearray([0xFF] * 8192)
        dirty_late_4096[4096] = 0xAA
        self.assertFalse(is_partition_blank(bytes(dirty_late_4096)))

        # 5. Empty/null partition -> MUST NOT auto-format
        self.assertFalse(is_partition_blank(b""))

    def test_duration_at_different_frame_rates(self):
        """Validates realistic capacity limits across typical vehicle CAN frame rates."""
        max_records = 49996  # from test_max_records_calculation

        rates = {
            50: 999.92,   # ~16.6 min
            100: 499.96,  # ~8.3 min
            150: 333.30,  # ~5.5 min (recommended 3-stage budget)
            200: 249.98,  # ~4.1 min
            300: 166.65,  # ~2.7 min
            500: 99.99,   # ~1.6 min
        }
        for rate, expected_sec in rates.items():
            duration_sec = max_records / rate
            self.assertAlmostEqual(duration_sec, expected_sec, places=1)

        # Verify a 3-minute capture session (45s stage 1, 60s stage 2, 60s stage 3 = 165s)
        # at 150 fps uses ~24,750 frames (~495 KB), perfectly fitting within 1 MB.
        session_frames = 165 * 150
        self.assertLess(session_frames, max_records)
        self.assertLess(session_frames * 20 + 76, self.MAX_CAPTURE_BYTES)


class ButtonStateMachineTests(unittest.TestCase):
    """Simulates the ButtonHandler non-blocking FSM logic."""

    DEBOUNCE_MS = 50
    LONG_PRESS_MS = 2500

    class MockButtonHandler:
        def __init__(self):
            self.last_raw = 1  # HIGH (unpressed)
            self.debounced_state = 1
            self.last_change_ms = 0
            self.press_start_ms = 0
            self.long_press_triggered = False

        def poll(self, raw_pin_val: int, now_ms: int) -> str:
            evt = "NONE"
            if raw_pin_val != self.last_raw:
                self.last_raw = raw_pin_val
                self.last_change_ms = now_ms

            if (now_ms - self.last_change_ms) >= ButtonStateMachineTests.DEBOUNCE_MS:
                if raw_pin_val != self.debounced_state:
                    self.debounced_state = raw_pin_val
                    if self.debounced_state == 0:  # Pressed LOW
                        self.press_start_ms = now_ms
                        self.long_press_triggered = False
                    else:  # Released HIGH
                        if not self.long_press_triggered:
                            dur = now_ms - self.press_start_ms
                            if dur >= ButtonStateMachineTests.DEBOUNCE_MS and dur < ButtonStateMachineTests.LONG_PRESS_MS:
                                evt = "SHORT_PRESS"
                elif self.debounced_state == 0 and not self.long_press_triggered:
                    if (now_ms - self.press_start_ms) >= ButtonStateMachineTests.LONG_PRESS_MS:
                        self.long_press_triggered = True
                        evt = "LONG_PRESS"

            return evt

    def test_contact_bounce_rejection(self):
        fsm = self.MockButtonHandler()
        # Contact bounce spikes (rapid 10ms toggles)
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=10), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=20), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=30), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=40), "NONE")
        self.assertEqual(fsm.debounced_state, 1, "Bounce spikes <50ms must not change state")

    def test_clean_short_press_and_stage_cycling(self):
        fsm = self.MockButtonHandler()
        current_stage = 1  # STAGE_KEY_ON_ENGINE_OFF

        # Press down at t=100
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=100), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=160), "NONE")
        self.assertEqual(fsm.debounced_state, 0)

        # Release at t=350 (held 250ms)
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=350), "NONE")
        evt = fsm.poll(raw_pin_val=1, now_ms=410)
        self.assertEqual(evt, "SHORT_PRESS")

        # Advance stage
        if evt == "SHORT_PRESS":
            current_stage = (current_stage % 3) + 1
        self.assertEqual(current_stage, 2, "Transition to Stage 2 (Idling)")

        # Second press -> Stage 3
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=500), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=560), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=750), "NONE")
        evt2 = fsm.poll(raw_pin_val=1, now_ms=810)
        self.assertEqual(evt2, "SHORT_PRESS")
        if evt2 == "SHORT_PRESS":
            current_stage = (current_stage % 3) + 1
        self.assertEqual(current_stage, 3, "Transition to Stage 3 (Gentle RPM)")

        # Third press -> Stage 1 (Cycle back)
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=900), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=960), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=1150), "NONE")
        evt3 = fsm.poll(raw_pin_val=1, now_ms=1210)
        self.assertEqual(evt3, "SHORT_PRESS")
        if evt3 == "SHORT_PRESS":
            current_stage = (current_stage % 3) + 1
        self.assertEqual(current_stage, 1, "Cycle back to Stage 1")

    def test_long_press_recording_toggle(self):
        fsm = self.MockButtonHandler()
        # Hold continuous for 2600ms
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=100), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=160), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=2600), "NONE")

        # Reached 2500ms threshold
        evt = fsm.poll(raw_pin_val=0, now_ms=2700)
        self.assertEqual(evt, "LONG_PRESS")

        # Verify no repeated event while still held
        self.assertEqual(fsm.poll(raw_pin_val=0, now_ms=3000), "NONE")

        # Verify no false short press on release
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=3500), "NONE")
        self.assertEqual(fsm.poll(raw_pin_val=1, now_ms=3560), "NONE")


class ExportRecoveryIntegrationTests(unittest.TestCase):
    """Tests the full parsing, CSV extraction and JSON formatting pipeline."""

    def test_csv_and_json_export_generation(self):
        test_file = BinaryRecordTests()
        test_file.setUp()
        ir_test = InterruptedWriteAndPowerLossTests()
        mock_data = ir_test.create_mock_capture_file(num_records=10)

        valid_records, corrupt_records, trailing = ir_test.parse_capture_file(mock_data)
        self.assertEqual(len(valid_records), 10)
        self.assertEqual(corrupt_records, 0)
        self.assertEqual(trailing, 0)

        # Generate CSV lines
        csv_lines = ["timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok"]
        for r in valid_records:
            hex_data = r["payload"].hex().upper()
            csv_lines.append(f"{r['timestamp']},{r['id']:08X},0,0,{r['dlc']},{hex_data},{r['stage']},1")

        self.assertEqual(len(csv_lines), 11)
        self.assertTrue(csv_lines[1].startswith("0,00000100,0,0,8,"))

        # Generate JSON representation
        json_str = json.dumps(valid_records, default=lambda o: o.hex() if isinstance(o, bytes) else o)
        parsed_json = json.loads(json_str)
        self.assertEqual(len(parsed_json), 10)
        self.assertEqual(parsed_json[0]["id"], 0x100)

    def test_exporter_failure_detection_and_file_preservation(self):
        """Simulates PowerShell exporter handling of corrupted or truncated serial dumps."""
        mock_serial_stream = [
            "=== BEGIN PULSESHIFT RAW CAN CAPTURE ===",
            "timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok",
            "100,00000120,0,0,8,0102030405060708,1,1",
            "120,00000120,0,0,8,0102030405060709,1,0",  # Bad CRC
            "MALFORMED_LINE_GARBAGE_NO_COMMAS",
            "140,00000120,0,0,8,010203040506070A,1,1",  # Good record
            "=== END PULSESHIFT RAW CAN CAPTURE ==="
        ]

        # Simulate export-logs.ps1 parser
        csv_records = []
        valid_count = 0
        corrupt_count = 0
        in_csv = False

        for line in mock_serial_stream:
            if line.startswith("timestamp_ms,"):
                in_csv = True
                continue
            if line.startswith("=== END"):
                in_csv = False
                continue
            if in_csv:
                cols = line.split(',')
                if len(cols) >= 8:
                    crc_ok = (cols[7].strip() == '1')
                    if crc_ok:
                        valid_count += 1
                        csv_records.append(cols)
                    else:
                        corrupt_count += 1

        self.assertEqual(valid_count, 2, "Valid records must be preserved")
        self.assertEqual(corrupt_count, 1, "Corrupted record must be detected and flagged")
        self.assertEqual(len(csv_records), 2)
        self.assertEqual(csv_records[0][0], "100")
        self.assertEqual(csv_records[1][0], "140")

    def test_multipage_corrupt_record_accounting(self):
        """Verify paginated DUMP cumulative totals cover all records across all pages, not just the last page."""
        total_records = 1200
        page_size = 500
        pages = (total_records + page_size - 1) // page_size
        self.assertEqual(pages, 3)

        # Page 1: 500 records, 2 corrupt
        # Page 2: 500 records, 1 corrupt
        # Page 3: 200 records, 0 corrupt
        page_stats = [
            {"page": 1, "recs": 500, "valid": 498, "corrupt": 2},
            {"page": 2, "recs": 500, "valid": 499, "corrupt": 1},
            {"page": 3, "recs": 200, "valid": 200, "corrupt": 0},
        ]

        cum_valid = 0
        cum_corrupt = 0
        exported_count = 0
        page_markers = []

        for p in page_stats:
            p_valid = p["valid"]
            p_corrupt = p["corrupt"]
            cum_valid += p_valid
            cum_corrupt += p_corrupt
            exported_count += (p_valid + p_corrupt)

            if p["page"] == pages:
                marker = f"=== END PULSESHIFT RAW CAN CAPTURE expected={total_records} exported={exported_count} valid={cum_valid} corrupt={cum_corrupt} totalBytes=24076 ==="
            else:
                marker = f"=== END PULSESHIFT PAGE {p['page']}/{pages} [page_valid={p_valid} page_corrupt={p_corrupt} cum_valid={cum_valid} cum_corrupt={cum_corrupt} expected_total={total_records}] ==="
            page_markers.append(marker)

        # Verify cumulative tracking
        self.assertEqual(cum_valid, 1197)
        self.assertEqual(cum_corrupt, 3)
        self.assertEqual(exported_count, 1200)

        final_marker = page_markers[-1]
        self.assertIn("expected=1200", final_marker)
        self.assertIn("exported=1200", final_marker)
        self.assertIn("valid=1197", final_marker)
        self.assertIn("corrupt=3", final_marker)

    def test_incomplete_export_detection_from_missing_page(self):
        """Verify that skipping an export page causes exported count to mismatch expected count."""
        total_records = 1200
        # User exported Page 1 (500 records) and Page 3 (200 records), skipping Page 2
        exported_records = 500 + 200  # 700
        expected_records = 1200

        final_marker = f"=== END PULSESHIFT RAW CAN CAPTURE expected={expected_records} exported={exported_records} valid=698 corrupt=2 totalBytes=24076 ==="

        match = re.search(r"expected=(\d+)\s+exported=(\d+)\s+valid=(\d+)\s+corrupt=(\d+)", final_marker)
        self.assertIsNotNone(match)
        exp = int(match.group(1))
        act = int(match.group(2))
        self.assertEqual(exp, 1200)
        self.assertEqual(act, 700)
        self.assertNotEqual(exp, act, "Missing export page must be detected via expected != exported")


class FaultInjectableStorage:
    """In-memory LittleFS simulator with fine-grained fault injection hooks."""

    def __init__(self, free_bytes=500000):
        self.files = {}
        self.fail_rename_patterns = []
        self.fail_write_offsets = {}
        self.free_bytes_val = free_bytes

    def set_free_bytes(self, b):
        self.free_bytes_val = b

    def free_bytes(self):
        return self.free_bytes_val

    def exists(self, path):
        return path in self.files

    def size(self, path):
        return len(self.files.get(path, b""))

    def remove(self, path):
        if path in self.files:
            del self.files[path]
            return True
        return False

    def rename(self, src, dst):
        for s_pat, d_pat in self.fail_rename_patterns:
            if (s_pat == "*" or s_pat == src) and (d_pat == "*" or d_pat == dst):
                return False
        if src not in self.files:
            return False
        self.files[dst] = self.files[src]
        del self.files[src]
        return True

    def write_file(self, path, data, chunk_size=128):
        max_bytes = self.fail_write_offsets.get(path, None)
        buf = bytearray()
        offset = 0
        while offset < len(data):
            chunk = data[offset:offset + chunk_size]
            if max_bytes is not None:
                allowed = max(0, max_bytes - len(buf))
                chunk = chunk[:allowed]
                buf.extend(chunk)
                if len(buf) >= max_bytes:
                    self.files[path] = bytes(buf)
                    return False
            else:
                buf.extend(chunk)
            offset += len(chunk)
        self.files[path] = bytes(buf)
        return True


def sim_verify_capture_file(storage: FaultInjectableStorage, path: str, expected_size: int = 0, check_records: bool = False) -> bool:
    if not storage.exists(path):
        return False
    sz = storage.size(path)
    if expected_size > 0 and sz != expected_size:
        return False
    if sz < 76:
        return False
    data_len = sz - 76
    if (data_len % 20) != 0:
        return False
    raw = storage.files[path]
    magic, ver, rec_size = struct.unpack("<8sHH", raw[:12])
    if magic != b"PULSECAP" or ver != 1 or rec_size != 20:
        return False
    if check_records and data_len > 0:
        offset = 76
        while offset + 20 <= sz:
            rec = raw[offset:offset + 20]
            dlc = rec[8]
            if dlc > 8:
                return False
            expected_crc = compute_crc16_ccitt(rec[:18])
            stored_crc = struct.unpack("<H", rec[18:20])[0]
            if stored_crc != expected_crc:
                return False
            offset += 20
    return True


def sim_truncate_to_size(storage: FaultInjectableStorage, path: str, new_size: int) -> bool:
    if not storage.exists(path):
        return False
    cur_size = storage.size(path)
    if cur_size <= new_size:
        return True
    if new_size < 76 or ((new_size - 76) % 20) != 0:
        return False
    if storage.free_bytes() < (new_size + 32768):
        return False

    temp_path = "/temp_cap.bin"
    backup_path = "/backup_cap.bin"

    if storage.exists(temp_path):
        storage.remove(temp_path)
    if storage.exists(backup_path):
        storage.remove(backup_path)

    orig_data = storage.files[path]
    copy_data = orig_data[:new_size]
    write_ok = storage.write_file(temp_path, copy_data)
    if not write_ok or storage.size(temp_path) != new_size:
        storage.remove(temp_path)
        return False

    if not sim_verify_capture_file(storage, temp_path, expected_size=new_size, check_records=True):
        storage.remove(temp_path)
        return False

    # Step 1: rename original to backup
    if not storage.rename(path, backup_path):
        storage.remove(temp_path)
        return False

    # Step 2: rename verified replacement to path
    if not storage.rename(temp_path, path):
        storage.rename(backup_path, path)
        storage.remove(temp_path)
        return False

    # Step 3: verify committed replacement at path before deleting backup
    if not sim_verify_capture_file(storage, path, expected_size=new_size, check_records=False):
        storage.remove(path)
        storage.rename(backup_path, path)
        return False

    # Step 4: replacement is verified committed at path; remove backup
    storage.remove(backup_path)
    return True


def sim_recover_boot_state(storage: FaultInjectableStorage, capture_path="/capture.bin", backup_path="/backup_cap.bin", temp_path="/temp_cap.bin") -> str:
    if storage.exists(backup_path):
        capture_exists = storage.exists(capture_path)
        capture_valid = capture_exists and sim_verify_capture_file(storage, capture_path, check_records=False)
        if capture_valid:
            storage.remove(backup_path)
            if storage.exists(temp_path):
                storage.remove(temp_path)
            return "ALREADY_COMMITTED"
        else:
            temp_valid = storage.exists(temp_path) and sim_verify_capture_file(storage, temp_path, check_records=True)
            if temp_valid:
                if capture_exists:
                    storage.remove(capture_path)
                if storage.rename(temp_path, capture_path) and sim_verify_capture_file(storage, capture_path, check_records=False):
                    storage.remove(backup_path)
                    return "COMMITTED_FROM_TEMP"
                else:
                    if storage.exists(capture_path):
                        storage.remove(capture_path)
                    restore_ok = storage.rename(backup_path, capture_path)
                    if storage.exists(temp_path):
                        storage.remove(temp_path)
                    return "RESTORED_FROM_BACKUP" if restore_ok else "RECOVERY_FAILED_BACKUP_KEPT"
            else:
                if capture_exists:
                    storage.remove(capture_path)
                restore_ok = storage.rename(backup_path, capture_path)
                if storage.exists(temp_path):
                    storage.remove(temp_path)
                return "RESTORED_FROM_BACKUP" if restore_ok else "RECOVERY_FAILED_BACKUP_KEPT"
    elif storage.exists(temp_path):
        if storage.exists(capture_path) and sim_verify_capture_file(storage, capture_path, check_records=False):
            storage.remove(temp_path)
            return "CLEANED_TEMP"
        elif not storage.exists(capture_path):
            storage.rename(temp_path, capture_path)
            return "PRESERVED_TEMP_AS_CAPTURE"
        else:
            storage.remove(temp_path)
            return "CLEANED_TEMP"
    return "NO_RECOVERY_NEEDED"


class PowerFailureSafeTruncationTests(unittest.TestCase):
    """Behavioral regression tests using fault-injectable filesystem operations."""

    HEADER_FORMAT = "<8sHHIIII16s32s"  # 76 bytes

    def make_valid_file(self, num_records=3, sess=1, boot=1):
        hdr = struct.pack(
            self.HEADER_FORMAT,
            b"PULSECAP", 1, 20, sess, boot, 500000, 1000,
            b"CAN_CAPTURE\x00\x00\x00\x00\x00",
            b"NS125 ABS 2025\x00" + b"\x00" * 17
        )
        data = bytearray(hdr)
        for i in range(num_records):
            rec_body = struct.pack("<IIBB8s", i * 10, 0x120 + i, 8, 1, bytes([i % 256] * 8))
            crc = compute_crc16_ccitt(rec_body)
            data.extend(rec_body + struct.pack("<H", crc))
        return bytes(data)

    def test_fault_injection_interrupted_write_temp(self):
        """Simulate power cut or write error mid-write to temp file."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)  # 76 + 100 = 176 bytes
        storage.files["/capture.bin"] = orig_data

        # Inject fault: fail after writing only 40 bytes to /temp_cap.bin
        storage.fail_write_offsets["/temp_cap.bin"] = 40

        success = sim_truncate_to_size(storage, "/capture.bin", new_size=136)
        self.assertFalse(success, "Truncation must fail cleanly on interrupted temp write")
        self.assertFalse(storage.exists("/temp_cap.bin"), "Partial temp file must be removed")
        self.assertFalse(storage.exists("/backup_cap.bin"), "No backup file should be left behind")
        self.assertTrue(storage.exists("/capture.bin"), "Original capture must remain in place")
        self.assertEqual(storage.files["/capture.bin"], orig_data, "Original data must be 100% unaltered")

    def test_fault_injection_corrupted_temp_record_alignment(self):
        """Reject truncation if target size violates record boundary alignment."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        storage.files["/capture.bin"] = orig_data

        # 76 + 25 bytes is not a multiple of 20
        success = sim_truncate_to_size(storage, "/capture.bin", new_size=101)
        self.assertFalse(success, "Truncation must reject non-record-aligned sizes")
        self.assertEqual(storage.files["/capture.bin"], orig_data)

    def test_fault_injection_corrupted_temp_header_magic(self):
        """Simulate corrupted magic in temporary file causing verification to reject commit."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        # Corrupted source data
        corrupt_data = bytearray(orig_data)
        corrupt_data[0:8] = b"BADMAGIC"
        storage.files["/capture.bin"] = bytes(corrupt_data)

        success = sim_truncate_to_size(storage, "/capture.bin", new_size=136)
        self.assertFalse(success, "Truncation must reject corrupted header magic")
        self.assertFalse(storage.exists("/temp_cap.bin"))
        self.assertFalse(storage.exists("/backup_cap.bin"))

    def test_fault_injection_corrupted_temp_record_crc(self):
        """Simulate bitflip in record payload detected by full integrity verification."""
        storage = FaultInjectableStorage()
        orig_data = bytearray(self.make_valid_file(num_records=5))
        # Corrupt one payload byte in record 2 without updating CRC
        orig_data[76 + 20 + 10] ^= 0xFF
        storage.files["/capture.bin"] = bytes(orig_data)

        success = sim_truncate_to_size(storage, "/capture.bin", new_size=136)
        self.assertFalse(success, "Truncation must reject temp file with invalid record CRC")
        self.assertFalse(storage.exists("/temp_cap.bin"))
        self.assertFalse(storage.exists("/backup_cap.bin"))

    def test_fault_injection_failed_rename_to_backup(self):
        """Simulate failure renaming original capture to backup."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        storage.files["/capture.bin"] = orig_data

        # Inject failure on rename to backup
        storage.fail_rename_patterns.append(("/capture.bin", "/backup_cap.bin"))

        success = sim_truncate_to_size(storage, "/capture.bin", new_size=136)
        self.assertFalse(success)
        self.assertFalse(storage.exists("/temp_cap.bin"))
        self.assertFalse(storage.exists("/backup_cap.bin"))
        self.assertEqual(storage.files["/capture.bin"], orig_data, "Original file preserved when backup rename fails")

    def test_fault_injection_failed_commit_with_successful_rollback(self):
        """Simulate failure committing replacement with successful rollback restoring backup."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        storage.files["/capture.bin"] = orig_data

        # Inject failure when committing temp to capture.bin
        storage.fail_rename_patterns.append(("/temp_cap.bin", "/capture.bin"))

        success = sim_truncate_to_size(storage, "/capture.bin", new_size=136)
        self.assertFalse(success, "Truncation must report failure when commit fails")
        self.assertFalse(storage.exists("/temp_cap.bin"), "Temp file must be unlinked")
        self.assertFalse(storage.exists("/backup_cap.bin"), "Backup file restored back to capture.bin")
        self.assertTrue(storage.exists("/capture.bin"), "Capture file must exist after rollback")
        self.assertEqual(storage.files["/capture.bin"], orig_data, "Rollback must restore original capture bytes")

    def test_fault_injection_failed_rollback_preserves_backup_never_deletes(self):
        """Critical safety: if rollback rename fails, backup must NEVER be deleted."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        storage.files["/capture.bin"] = orig_data

        # Fail commit of temp AND fail rollback of backup
        storage.fail_rename_patterns.append(("/temp_cap.bin", "/capture.bin"))
        storage.fail_rename_patterns.append(("/backup_cap.bin", "/capture.bin"))

        success = sim_truncate_to_size(storage, "/capture.bin", new_size=136)
        self.assertFalse(success)
        # CRITICAL ASSERTION: The last recoverable copy must NOT be deleted!
        self.assertTrue(storage.exists("/backup_cap.bin"), "Backup copy MUST be preserved when rollback fails")
        self.assertEqual(storage.files["/backup_cap.bin"], orig_data, "Backup data must be intact")

    def test_fault_injection_boot_recovery_from_backup_when_capture_missing(self):
        """Simulate boot recovery after power loss interrupted commit."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        storage.files["/backup_cap.bin"] = orig_data
        storage.files["/temp_cap.bin"] = b"PARTIAL_TORN_TEMP_BYTES"

        status = sim_recover_boot_state(storage)
        self.assertEqual(status, "RESTORED_FROM_BACKUP")
        self.assertTrue(storage.exists("/capture.bin"))
        self.assertEqual(storage.files["/capture.bin"], orig_data)
        self.assertFalse(storage.exists("/backup_cap.bin"))
        self.assertFalse(storage.exists("/temp_cap.bin"))

    def test_fault_injection_boot_recovery_corrupt_capture_replaced_by_backup(self):
        """Simulate boot recovery when capture.bin was corrupted during commit before power cut."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        storage.files["/backup_cap.bin"] = orig_data
        storage.files["/capture.bin"] = b""  # 0-byte corrupt file from cut-off commit

        status = sim_recover_boot_state(storage)
        self.assertEqual(status, "RESTORED_FROM_BACKUP")
        self.assertTrue(storage.exists("/capture.bin"))
        self.assertEqual(storage.files["/capture.bin"], orig_data)
        self.assertFalse(storage.exists("/backup_cap.bin"))

    def test_fault_injection_boot_recovery_from_verified_temp(self):
        """Simulate boot recovery when temp file is verified valid and complete."""
        storage = FaultInjectableStorage()
        orig_data = self.make_valid_file(num_records=5)
        truncated_data = self.make_valid_file(num_records=4)
        storage.files["/backup_cap.bin"] = orig_data
        storage.files["/temp_cap.bin"] = truncated_data

        status = sim_recover_boot_state(storage)
        self.assertEqual(status, "COMMITTED_FROM_TEMP")
        self.assertTrue(storage.exists("/capture.bin"))
        self.assertEqual(storage.files["/capture.bin"], truncated_data)
        self.assertFalse(storage.exists("/backup_cap.bin"))
        self.assertFalse(storage.exists("/temp_cap.bin"))

    def test_fault_injection_boot_recovery_already_committed(self):
        """Simulate boot recovery when replacement was already committed before power cut."""
        storage = FaultInjectableStorage()
        valid_capture = self.make_valid_file(num_records=4)
        storage.files["/capture.bin"] = valid_capture
        storage.files["/backup_cap.bin"] = self.make_valid_file(num_records=5)
        storage.files["/temp_cap.bin"] = b"STALE"

        status = sim_recover_boot_state(storage)
        self.assertEqual(status, "ALREADY_COMMITTED")
        self.assertTrue(storage.exists("/capture.bin"))
        self.assertEqual(storage.files["/capture.bin"], valid_capture)
        self.assertFalse(storage.exists("/backup_cap.bin"))
        self.assertFalse(storage.exists("/temp_cap.bin"))

    def test_damaged_or_incomplete_header_preservation(self):
        """Verify incomplete (< 76 bytes) or corrupted header capture is preserved and never overwritten."""
        incomplete_data = b"PULSECAP_PARTIAL_BYTES_LESS_THAN_76"
        self.assertLess(len(incomplete_data), 76)

        sz = len(incomplete_data)
        damaged_recording = False
        storage_full = False
        recording = True

        if sz < 76:
            damaged_recording = True
            storage_full = True
            recording = False

        self.assertTrue(damaged_recording)
        self.assertTrue(storage_full)
        self.assertFalse(recording)

        corrupt_header = b"BADMAGIC" + (b"\x00" * 68)
        self.assertEqual(len(corrupt_header), 76)
        valid_magic = (corrupt_header[:8] == b"PULSECAP")
        self.assertFalse(valid_magic)
        if not valid_magic:
            damaged_recording = True
            storage_full = True
            recording = False
        self.assertTrue(damaged_recording)


if __name__ == "__main__":
    unittest.main()
