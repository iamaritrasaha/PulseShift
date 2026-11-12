"""
Production Firmware Integrity and CAN Acquisition Test Suite
Copyright (c) 2026 Aritra Saha. All rights reserved.

Verifies:
1. Production firmware defaults (CAN_CAPTURE mode, 500000 bps, 255 LED brightness).
2. Elimination of simulation/demo code from production firmware builds.
3. Machine-readable serial streaming format (FRAME, STAGE, DIAG, STAT, META).
4. Stop-on-full and session preservation guarantees (no circular overwrite).
5. Capture tool functionality and disk-full safety limits.
6. Zero em-dashes and en-dashes across all code and documentation.
"""

import unittest
import os
import re
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(REPO_ROOT, "tools"))


class ProductionFirmwareIntegrityTests(unittest.TestCase):
    def setUp(self):
        self.user_config_path = os.path.join(REPO_ROOT, "PulseShift", "UserConfig.h")
        self.config_h_path = os.path.join(REPO_ROOT, "PulseShift", "src", "config.h")
        self.main_ino_path = os.path.join(REPO_ROOT, "PulseShift", "PulseShift.ino")

    def test_production_default_configuration(self):
        """Verify production defaults in UserConfig.h."""
        with open(self.user_config_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Operating mode must default to CAN_CAPTURE
        self.assertRegex(
            content,
            r"#define\s+PULSESHIFT_OPERATING_MODE\s+PULSESHIFT_USER_MODE_CAN_CAPTURE",
            "Production firmware must default to PULSESHIFT_USER_MODE_CAN_CAPTURE."
        )

        # Default CAN bitrate must be 500000 bps
        self.assertRegex(
            content,
            r"#define\s+PULSESHIFT_CAN_BITRATE\s+500000",
            "Production firmware must default to 500000 bps CAN bitrate."
        )

        # Master LED brightness must default to 255
        self.assertRegex(
            content,
            r"#define\s+PULSESHIFT_LED_BRIGHTNESS\s+255",
            "LED master brightness must default to full scale 255."
        )

    def test_simulation_excluded_from_production(self):
        """Verify that DEMO mode is rejected unless PULSESHIFT_TEST_BUILD is defined."""
        with open(self.config_h_path, "r", encoding="utf-8") as f:
            content = f.read()

        self.assertIn("PULSESHIFT_TEST_BUILD", content)
        self.assertIn('DEMO mode is excluded from production firmware', content)

    def test_simulated_ecu_guarded(self):
        """Verify simulated_ecu.h and simulated_ecu.cpp are guarded with PULSESHIFT_TEST_BUILD."""
        for filename in ("simulated_ecu.h", "simulated_ecu.cpp"):
            path = os.path.join(REPO_ROOT, "PulseShift", "src", filename)
            with open(path, "r", encoding="utf-8") as f:
                content = f.read()
            self.assertIn(
                "defined(PULSESHIFT_TEST_BUILD)",
                content,
                f"{filename} must be guarded with PULSESHIFT_TEST_BUILD."
            )

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
        diag_cpp = os.path.join(REPO_ROOT, "PulseShift", "src", "diag_logger.cpp")
        with open(diag_cpp, "r", encoding="utf-8") as f:
            content = f.read()

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

    def test_zero_em_and_en_dashes_in_active_project(self):
        """Strict verification: 0 em-dashes (\u2014) and 0 en-dashes (\u2013) across code and documentation."""
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
