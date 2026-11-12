#!/usr/bin/env python3
"""
Unit tests for PulseShift Analyzer and Calibration Tool (tools/pulseshift_analyzer.py)
Copyright (c) 2026 Aritra Saha. All rights reserved.
"""

import unittest
import tempfile
import csv
from pathlib import Path

from tools.pulseshift_analyzer import (
    parse_can_id,
    load_can_csv,
    filter_records,
    analyze_signal,
    generate_timeline,
    export_timeline_csv,
    calibrate_linear,
    parse_calibration_pairs,
    find_rational_approximation,
    CanRecord
)


class TestPulseShiftAnalyzer(unittest.TestCase):

    def test_parse_can_id(self):
        self.assertEqual(parse_can_id("0x301"), 0x301)
        self.assertEqual(parse_can_id("301"), 0x301)
        self.assertEqual(parse_can_id("12B"), 0x12B)
        self.assertEqual(parse_can_id("0x12b"), 0x12B)
        self.assertEqual(parse_can_id("0x7FF"), 0x7FF)

    def test_find_rational_approximation(self):
        num, den = find_rational_approximation(40.0)
        self.assertEqual(num, 40)
        self.assertEqual(den, 1)

        num, den = find_rational_approximation(42.5)
        self.assertEqual(num, 85)
        self.assertEqual(den, 2)

        num, den = find_rational_approximation(0.5)
        self.assertEqual(num, 1)
        self.assertEqual(den, 2)

    def test_calibration_single_point(self):
        pairs = parse_calibration_pairs("35:1400")
        self.assertEqual(pairs, [(35.0, 1400.0)])
        res = calibrate_linear(pairs)
        self.assertEqual(res["pairs_count"], 1)
        self.assertAlmostEqual(res["slope"], 40.0, places=4)
        self.assertEqual(res["intercept"], 0.0)
        self.assertEqual(res["scaleNumerator"], 40)
        self.assertEqual(res["scaleDenominator"], 1)
        self.assertIn("zero offset", res["warning"])

    def test_calibration_two_point(self):
        pairs = parse_calibration_pairs("35:1400,240:9600")
        self.assertEqual(len(pairs), 2)
        res = calibrate_linear(pairs)
        self.assertEqual(res["pairs_count"], 2)
        self.assertAlmostEqual(res["slope"], 40.0, places=4)
        self.assertAlmostEqual(res["intercept"], 0.0, places=4)
        self.assertEqual(res["scaleNumerator"], 40)
        self.assertEqual(res["scaleDenominator"], 1)
        self.assertEqual(res["offsetNumerator"], 0)

        # Test line with non-zero offset: y = 35 * x + 100
        # point 1: x=10, y=450; point 2: x=50, y=1850
        pairs2 = [(10.0, 450.0), (50.0, 1850.0)]
        res2 = calibrate_linear(pairs2)
        self.assertAlmostEqual(res2["slope"], 35.0, places=4)
        self.assertAlmostEqual(res2["intercept"], 100.0, places=4)
        self.assertEqual(res2["scaleNumerator"], 35)
        self.assertEqual(res2["scaleDenominator"], 1)
        self.assertEqual(res2["offsetNumerator"], 100)

    def test_calibration_multi_point_regression(self):
        # 3 collinear points: y = 40 * x
        pairs = [(35.0, 1400.0), (75.0, 3000.0), (240.0, 9600.0)]
        res = calibrate_linear(pairs)
        self.assertEqual(res["pairs_count"], 3)
        self.assertAlmostEqual(res["slope"], 40.0, places=4)
        self.assertAlmostEqual(res["intercept"], 0.0, places=4)
        self.assertAlmostEqual(res["r_squared"], 1.0, places=4)
        self.assertAlmostEqual(res["rmse"], 0.0, places=4)

    def test_calibration_error_handling(self):
        with self.assertRaises(ValueError):
            calibrate_linear([])

        with self.assertRaises(ValueError):
            calibrate_linear([(35.0, 1400.0), (35.0, 2000.0)])  # Identical x

        with self.assertRaises(ValueError):
            parse_calibration_pairs("invalid_pair")

    def test_csv_loading_and_filtering(self):
        with tempfile.NamedTemporaryFile("w", delete=False, suffix=".csv") as tf:
            tf.write("timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok\n")
            tf.write("1000,301,0,0,8,25000000000000C9,1,1\n")   # 0x25 = 37
            tf.write("1020,301,0,0,8,26000000000000C9,1,1\n")   # 0x26 = 38
            tf.write("1040,302,0,0,8,0000000000000000,1,1\n")   # 0x302 (should filter out)
            tf.write("1060,301,0,0,8,28000000000000C9,2,1\n")   # 0x28 = 40 (stage 2)
            tf.write("1080,301,0,0,8,00000000000000C9,2,0\n")   # bad CRC
            temp_path = Path(tf.name)

        try:
            records = load_can_csv(temp_path)
            self.assertEqual(len(records), 5)

            filtered = filter_records(records, can_id=0x301, crc_only=True)
            self.assertEqual(len(filtered), 3)

            stats = analyze_signal(filtered, byte_index=0)
            self.assertEqual(stats["count"], 3)
            self.assertEqual(stats["min"], 37)
            self.assertEqual(stats["max"], 40)
            self.assertEqual(stats["median"], 38)

            # Test timeline generation
            timeline = generate_timeline(filtered, byte_index=0, bin_size_sec=1.0)
            self.assertEqual(len(timeline), 1)
            self.assertEqual(timeline[0]["count"], 3)
            self.assertEqual(timeline[0]["min"], 37)
            self.assertEqual(timeline[0]["max"], 40)

            # Test timeline CSV export
            with tempfile.NamedTemporaryFile("w", delete=False, suffix=".csv") as out_tf:
                out_path = Path(out_tf.name)
            export_timeline_csv(timeline, out_path)
            self.assertTrue(out_path.exists())
            out_content = out_path.read_text()
            self.assertIn("b0_median", out_content)
            self.assertIn("38.0", out_content)
            out_path.unlink()

        finally:
            temp_path.unlink()


if __name__ == "__main__":
    unittest.main()
