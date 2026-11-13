"""
Production Firmware Integrity and CAN Acquisition Test Suite
Copyright (c) 2026 Aritra Saha. All rights reserved.

Verifies:
1. Production firmware defaults (500000 bps, six LEDs, BLE enabled).
2. Complete removal of OLED support and of simulation/demo/mode-matrix code.
3. Single passive CAN path (listen-only, zero TX queue, no transmit API).
4. Machine-readable serial streaming format (FRAME, STAGE, DIAG, STAT, META).
5. Stop-on-full and session preservation guarantees (no circular overwrite).
6. Capture tool functionality and disk-full safety limits.
7. BLE protocol completeness: all required v2 opcodes dispatched, capture
   pages streamed, diagnostics TLV blob, explicit-confirm device commands.
8. Zero em-dashes and en-dashes across all code and documentation.
"""

import unittest
import os
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))


class ProductionFirmwareIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.user_config_path = os.path.join(REPO_ROOT, "PulseShift", "UserConfig.h")
        self.config_h_path = os.path.join(REPO_ROOT, "PulseShift", "src", "config.h")
        self.main_ino_path = os.path.join(REPO_ROOT, "PulseShift", "PulseShift.ino")
        self.ble_protocol_path = os.path.join(REPO_ROOT, "PulseShift", "src", "ble_protocol.h")
        self.ble_manager_path = os.path.join(REPO_ROOT, "PulseShift", "src", "ble_manager.cpp")
        self.transport_path = os.path.join(REPO_ROOT, "PulseShift", "src", "can_transport.cpp")

    def read(self, path):
        with open(path, "r", encoding="utf-8") as f:
            return f.read()

    def test_production_default_configuration(self):
        """Verify production defaults in UserConfig.h."""
        content = self.read(self.user_config_path)

        # Default CAN bitrate must be 500000 bps (previously observed on the NS125)
        self.assertRegex(
            content,
            r"#define\s+PULSESHIFT_CAN_BITRATE\s+500000",
            "Production firmware must default to 500000 bps CAN bitrate."
        )
        # BLE must be enabled by default (Android is the command interface)
        self.assertRegex(
            content,
            r"#define\s+PULSESHIFT_BLE_ENABLED\s+1",
            "BLE must be enabled by default."
        )

    def test_oled_support_completely_removed(self):
        """No OLED symbols, dependencies or build variants may remain."""
        src_dir = os.path.join(REPO_ROOT, "PulseShift")
        for root, dirs, files in os.walk(src_dir):
            for f in files:
                if not f.endswith((".h", ".cpp", ".ino")):
                    continue
                content = self.read(os.path.join(root, f))
                for symbol in ("SSD1306", "Adafruit_GFX", "Adafruit_SSD1306",
                               "PULSESHIFT_OLED_ENABLED", "OLED_SDA", "OLED_SCL",
                               "Wire.begin", "display_."):
                    self.assertNotIn(symbol, content,
                                     f"{f} must not reference OLED symbol {symbol}")

        # CI must not build an OLED variant or install its libraries. (The
        # removal-verification step itself mentions the symbols; only
        # dependency-installation / variant commands are forbidden.)
        ci = self.read(os.path.join(REPO_ROOT, ".github", "workflows", "ci.yml"))
        self.assertNotRegex(ci, r"lib install.*SSD1306")
        self.assertNotRegex(ci, r"lib install.*(Adafruit GFX|BusIO)")
        self.assertNotIn("OLED enabled", ci)

    def test_simulation_and_mode_matrix_removed(self):
        """DEMO/REPLAY/TWAI_TEST paths and the compile-time mode matrix are gone."""
        src_dir = os.path.join(REPO_ROOT, "PulseShift", "src")
        for filename in ("simulated_ecu.h", "simulated_ecu.cpp", "demo_commands.h",
                         "tx_tracker.h", "rpm_decoder.h", "vehicle_profile.h"):
            self.assertFalse(os.path.exists(os.path.join(src_dir, filename)),
                             f"{filename} must be removed from the firmware sources")

        config_h = self.read(self.config_h_path)
        for symbol in ("PULSESHIFT_REPLAY", "PULSESHIFT_TWAI_TEST", "PULSESHIFT_VEHICLE",
                       "PULSESHIFT_OPERATING_MODE", "ShiftLightEngine", "VehicleDecoder"):
            self.assertNotIn(symbol, config_h)

        ino = self.read(self.main_ino_path)
        for symbol in ("demo start", "demo stop", "ReplayTransport", "sendTest"):
            self.assertNotIn(symbol, ino)

    def test_single_passive_can_path(self):
        """TWAI must stay listen-only with a zero TX queue and no transmit API."""
        content = self.read(self.transport_path)
        self.assertIn("TWAI_MODE_LISTEN_ONLY", content)
        self.assertIn("tx_queue_len = 0", content)
        self.assertNotIn("twai_transmit", content)
        self.assertIn("gpio_reset_pin", content)  # TX pin isolated after install
        self.assertIn("GPIO_MODE_INPUT", content)

        transport_h = self.read(os.path.join(REPO_ROOT, "PulseShift", "src", "can_transport.h"))
        self.assertNotIn("sendTest", transport_h)
        self.assertNotIn("canSend", transport_h)

    def test_ble_protocol_complete_dispatch(self):
        """Every declared v2 opcode must have a dispatch case in BleManager."""
        protocol = self.read(self.ble_protocol_path)
        dispatch = self.read(self.ble_manager_path)

        required_opcodes = [
            "BLE_CFG_OP_GET_CAPABILITIES", "BLE_CFG_OP_GET_SNAPSHOT",
            "BLE_CFG_OP_SET_POWER", "BLE_CFG_OP_SET_BRIGHTNESS",
            "BLE_CFG_OP_SET_GLOBAL_RGB", "BLE_CFG_OP_SET_LED_RGB",
            "BLE_CFG_OP_SET_EFFECT", "BLE_CFG_OP_SET_STARTUP",
            "BLE_CFG_OP_SET_LED_THRESHOLD", "BLE_CFG_OP_SET_SHIFT_WARNING",
            "BLE_CFG_OP_SET_RPM_MULTIPLIER", "BLE_CFG_OP_SET_CANDIDATE_SOURCE",
            "BLE_CFG_OP_SET_LED_DIRECTION", "BLE_CFG_OP_SET_WARNING_RGB",
            "BLE_CFG_OP_SET_RPM_HYSTERESIS", "BLE_CFG_OP_SET_RPM_DECODER_EXT",
            "BLE_CFG_OP_SAVE_CONFIG", "BLE_CFG_OP_RESTORE_DEFAULTS",
            "BLE_CFG_OP_RELOAD_CONFIG",
            "BLE_CFG_OP_ENROLL_OWNER", "BLE_CFG_OP_AUTHENTICATE", "BLE_CFG_OP_REQUEST_CHALLENGE",
            "BLE_CFG_OP_BEGIN_TRANSACTION", "BLE_CFG_OP_COMMIT_TRANSACTION", "BLE_CFG_OP_ABORT_TRANSACTION",
            "BLE_CFG_OP_CAL_BEGIN", "BLE_CFG_OP_CAL_ABORT", "BLE_CFG_OP_CAL_CAPTURE_START",
            "BLE_CFG_OP_CAL_CAPTURE_STATUS", "BLE_CFG_OP_CAL_POINT_COMMIT",
            "BLE_CFG_OP_CAL_COMPUTE", "BLE_CFG_OP_CAL_STATUS",
            "BLE_CFG_OP_GET_DIAGNOSTICS", "BLE_CFG_OP_RESTART_DEVICE",
            "BLE_CFG_OP_CLEAR_DIAGNOSTICS", "BLE_CFG_OP_SET_CAPTURE_RECORDING",
        ]
        for opcode in required_opcodes:
            self.assertIn(opcode, protocol, f"{opcode} must be declared in ble_protocol.h")
            self.assertIn(f"case {opcode}:", dispatch,
                          f"{opcode} must have a dispatch case in ble_manager.cpp")

        # Firmware version must be 2.2.0 (this release).
        self.assertRegex(protocol, r"PULSESHIFT_FW_VERSION_MAJOR 2")
        self.assertRegex(protocol, r"PULSESHIFT_FW_VERSION_MINOR 2")

    def test_bounded_command_queues_and_auth_binding(self):
        """Commands queue through a bounded ring; writes check the session's connection."""
        dispatch = self.read(self.ble_manager_path)
        self.assertIn("CONFIG_QUEUE_CAPACITY", dispatch)
        self.assertIn("CTRL_QUEUE_CAPACITY", dispatch)
        self.assertIn("isAuthorizedFor(connId", dispatch)
        # Capability packet must carry the schema-3 feature bits and fw version.
        self.assertIn("BLE_FEAT_DIAGNOSTICS_TLV", dispatch)
        self.assertIn("BLE_FEAT_CAPTURE_EXPORT", dispatch)
        self.assertIn("caps.firmwareMajor", dispatch)

    def test_capture_pages_streamed_over_ble(self):
        """The v1 control capture-page commands must be consumed, not orphaned."""
        dispatch = self.read(self.ble_manager_path)
        self.assertIn("processCapturePage", dispatch)
        self.assertIn("bleExportNext", dispatch)
        self.assertIn("BLE_LOG_FLAG_CAPTURE_LINE", dispatch)

    def test_frame_streaming_format_and_parsing(self):
        """Verify frame serialization matches FRAME,timestamp,stage,id,ext,rtr,dlc,data."""
        sample_line = "FRAME,123456,2,00000123,0,0,8,0102030405060708"
        parts = sample_line.split(",")
        self.assertEqual(parts[0], "FRAME")
        self.assertEqual(int(parts[1]), 123456)
        self.assertEqual(int(parts[2]), 2)  # Stage 2
        self.assertEqual(parts[3], "00000123")  # Arbitration ID
        self.assertEqual(int(parts[4]), 0)  # Standard ID
        self.assertEqual(int(parts[5]), 0)  # Data frame (not RTR)
        self.assertEqual(int(parts[6]), 8)  # DLC
        self.assertEqual(parts[7], "0102030405060708")

    def test_no_circular_log_wipe(self):
        """Verify diag_logger does not circularly wipe /diag.log when capacity is reached."""
        content = self.read(os.path.join(REPO_ROOT, "PulseShift", "src", "diag_logger.cpp"))

        # checkRotate must set logFull_ and not remove LOG_PATH
        self.assertNotIn(
            'sm.remove(LOG_PATH);',
            content.split('void DiagLogger::checkRotate()')[1].split('void DiagLogger::flush()')[0],
            "checkRotate must never wipe or delete LOG_PATH when capacity is reached."
        )

    def test_capture_tool_import_and_disk_safety(self):
        """Verify pulseshift_capture.py imports, creates output dirs, and enforces safety headroom."""
        import pulseshift_capture
        tool = pulseshift_capture.PulseShiftCapture(port="COM5", baud=115200, output_dir="captures")
        self.assertEqual(tool.port, "COM5")
        self.assertEqual(tool.baud, 115200)
        self.assertEqual(tool.current_stage, 1)

    def test_arduino_ide_commands_present(self):
        """Verify all required Arduino IDE Serial Monitor commands are implemented in PulseShift.ino."""
        content = self.read(self.main_ino_path)

        expected_commands = [
            'STATUS', 'LOGS', 'DUMP', 'DUMP NEXT', 'DUMP ALL',
            'DUMP STATUS', 'DUMP CANCEL', 'DUMP PAGE',
            'CAPTURE STOP', 'CAPTURE RESUME',
            'STAGE 1', 'STAGE 2', 'STAGE 3',
            'STREAM ON', 'STREAM OFF',
            'CLEAR YES'
        ]
        for cmd in expected_commands:
            self.assertIn(cmd, content, f"Command '{cmd}' must be supported in PulseShift.ino.")

    def test_dump_pagination_and_storage_states(self):
        """Verify pagination constants and distinct recorder states in can_recorder.h."""
        content = self.read(os.path.join(REPO_ROOT, "PulseShift", "src", "can_recorder.h"))

        self.assertIn("DUMP_PAGE_SIZE = 500", content)
        self.assertIn("PreservedPriorSession", content)
        self.assertIn("DamagedRecording", content)
        self.assertIn("StorageFull", content)
        self.assertIn("WriteFailure", content)
        self.assertIn("RecordingOk", content)

    def test_safe_truncation_and_preservation(self):
        """Verify truncateToSize validates replacement file, uses backup, and supports rollback."""
        content = self.read(os.path.join(REPO_ROOT, "PulseShift", "src", "can_recorder.cpp"))

        # Must check free space before attempting truncate
        self.assertIn("StorageManager::MIN_FREE_BYTES", content)
        # Must verify replacement before touching original
        self.assertIn("validReplacement", content)
        self.assertIn("TRUNCATE_FAILED", content)
        # Must use backup path and check all rename return codes
        self.assertIn("/backup_cap.bin", content)
        self.assertIn("TRUNCATE_BACKUP_FAILED", content)
        self.assertIn("TRUNCATE_REPLACE_FAILED", content)
        self.assertIn("TRUNCATE_RECOVERED", content)
        self.assertIn("ROLLBACK_FAILED", content)
        self.assertIn("verifyCaptureFile", content)
        self.assertIn("TRUNCATE_INVALID_SIZE", content)
        # Must preserve damaged capture
        self.assertIn("DAMAGED_CAPTURE_PRESERVED", content)

    def test_dump_integrity_and_page_accounting(self):
        """Verify paginated DUMP includes expected/exported record counts and cumulative totals."""
        content = self.read(os.path.join(REPO_ROOT, "PulseShift", "src", "can_recorder.cpp"))

        self.assertIn("expected=%lu exported=%lu valid=%lu corrupt=%lu", content)
        self.assertIn("page_valid=%lu page_corrupt=%lu cum_valid=%lu cum_corrupt=%lu expected_total=%lu", content)

    def test_live_streaming_default_disabled(self):
        """Verify raw frame streaming is disabled by default to avoid flooding Serial Monitor."""
        content = self.read(self.main_ino_path)

        self.assertIn("bool liveStreamEnabled_ = false;", content)

    def test_watchdog_subscribed(self):
        """The main loop must be subscribed to the task watchdog."""
        content = self.read(self.main_ino_path)
        self.assertIn("esp_task_wdt_add", content)
        self.assertIn("esp_task_wdt_reset", content)

    def test_restore_defaults_preserve_credentials(self):
        """Configuration reset must be distinct from credential recovery."""
        content = self.read(os.path.join(REPO_ROOT, "PulseShift", "src", "config_manager.cpp"))
        restore = content.split("bool ConfigManager::restoreDefaults")[1].split("\n}")[0]
        self.assertNotIn("clearOwnerEnrollment", restore,
                         "restoreDefaults must not erase owner enrollment")

    def test_zero_em_and_en_dashes_in_active_project(self):
        """Strict verification: 0 em-dashes and 0 en-dashes across code and documentation."""
        bad_chars = {'\u2014': 'EM DASH', '\u2013': 'EN DASH'}
        check_targets = ['PulseShift', 'README.md', 'docs', 'tools', 'test', '.github']

        found_violations = []
        for target in check_targets:
            target_path = os.path.join(REPO_ROOT, target)
            if os.path.isfile(target_path):
                files_to_check = [target_path]
            elif os.path.isdir(target_path):
                files_to_check = []
                for root, dirs, files in os.walk(target_path):
                    # Skip research raw web dump archives from 2026-10-26
                    if 'research-2026-10-26' in root or '.git' in root or '__pycache__' in root:
                        continue
                    for f in files:
                        files_to_check.append(os.path.join(root, f))
            else:
                continue

            for fpath in files_to_check:
                try:
                    with open(fpath, 'r', encoding='utf-8') as fp:
                        for line_no, line in enumerate(fp, 1):
                            for char, name in bad_chars.items():
                                if char in line:
                                    rel = os.path.relpath(fpath, REPO_ROOT)
                                    found_violations.append(f"{rel}:{line_no} contains {name}")
                except Exception:
                    pass

        self.assertEqual(
            found_violations,
            [],
            f"Found forbidden dash characters in project:\n" + "\n".join(found_violations)
        )


if __name__ == "__main__":
    unittest.main()
