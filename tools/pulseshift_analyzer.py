#!/usr/bin/env python3
"""
PulseShift - Offline CAN Analysis & RPM Calibration Tool
Copyright (c) 2026 Aritra Saha. All rights reserved.

Analyzes raw exported motorcycle CAN CSV captures, filters signals by arbitration ID,
computes inter-frame timing and payload statistics, generates timeline aggregations,
and calculates provisional linear calibration models (slope, intercept, and rational fractions).

Important Safety Note:
All calibration results remain strictly UNVERIFIED hypotheses until calibrated against
an independent physical tachometer or manufacturer diagnostic scan tool.
Physical shift lights must remain DISABLED (RPM_MAPPED=NO) until verified.
"""

from __future__ import annotations

import argparse
import csv
import math
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import List, Optional, Tuple, Dict
import statistics


@dataclass
class CanRecord:
    timestamp_ms: int
    can_id: int
    extended: bool
    remote: bool
    dlc: int
    data: bytes
    stage: int
    crc_ok: bool


def parse_can_id(val_str: str) -> int:
    """Parses a CAN ID from hex (e.g. '0x301' or '301') or decimal."""
    clean = val_str.strip()
    if clean.lower().startswith("0x"):
        return int(clean, 16)
    # CAN arbitration IDs in PulseShift CSV exports are hexadecimal (e.g. 12B, 301, 451)
    try:
        return int(clean, 16)
    except ValueError:
        return int(clean, 10)


def load_can_csv(path: Path, max_rows: int = 500000) -> List[CanRecord]:
    """Loads CAN records from a PulseShift exported CSV file."""
    if not path.is_file():
        raise FileNotFoundError(f"File not found: {path}")

    records: List[CanRecord] = []
    with path.open(mode="r", encoding="utf-8", errors="replace") as f:
        reader = csv.reader(f)
        header = next(reader, None)
        if not header:
            return records

        # Identify column indices
        cols = [c.strip().lower() for c in header]
        try:
            t_idx = cols.index("timestamp_ms")
            id_idx = cols.index("can_id")
            ext_idx = cols.index("extended")
            rtr_idx = cols.index("rtr")
            dlc_idx = cols.index("dlc")
            data_idx = cols.index("data")
            stage_idx = cols.index("stage")
            crc_idx = cols.index("crc_ok")
        except ValueError as e:
            raise ValueError(f"CSV header missing required column: {e}. Found: {header}")

        for line_no, row in enumerate(reader, start=2):
            if not row or len(row) < 8:
                continue

            try:
                t_ms = int(row[t_idx].strip())
                id_val = parse_can_id(row[id_idx])
                ext = bool(int(row[ext_idx].strip()))
                rtr = bool(int(row[rtr_idx].strip()))
                dlc = int(row[dlc_idx].strip())
                data_hex = row[data_idx].strip()
                data_bytes = bytes.fromhex(data_hex) if data_hex else b""
                stage = int(row[stage_idx].strip())
                crc = bool(int(row[crc_idx].strip()))

                records.append(CanRecord(
                    timestamp_ms=t_ms,
                    can_id=id_val,
                    extended=ext,
                    remote=rtr,
                    dlc=dlc,
                    data=data_bytes,
                    stage=stage,
                    crc_ok=crc
                ))
            except Exception as ex:
                print(f"[WARN] Line {line_no} malformed ({ex}), skipping", file=sys.stderr)
                continue

            if len(records) >= max_rows:
                break

    return records


def filter_records(records: List[CanRecord],
                   can_id: Optional[int] = None,
                   stage: Optional[List[int]] = None,
                   crc_only: bool = True,
                   start_ms: Optional[int] = None,
                   end_ms: Optional[int] = None) -> List[CanRecord]:
    """Filters records by CAN ID, stage, CRC status, and time range."""
    filtered: List[CanRecord] = []
    for r in records:
        if crc_only and not r.crc_ok:
            continue
        if can_id is not None and r.can_id != can_id:
            continue
        if stage is not None and r.stage not in stage:
            continue
        if start_ms is not None and r.timestamp_ms < start_ms:
            continue
        if end_ms is not None and r.timestamp_ms > end_ms:
            continue
        filtered.append(r)
    return filtered


def analyze_signal(records: List[CanRecord], byte_index: int = 0) -> Dict:
    """Computes comprehensive statistics for a specific payload byte and timing."""
    if not records:
        return {"count": 0}

    values: List[int] = []
    timestamps: List[int] = []
    stages: Dict[int, int] = {}

    for r in records:
        if len(r.data) > byte_index and not r.remote:
            values.append(r.data[byte_index])
            timestamps.append(r.timestamp_ms)
            stages[r.stage] = stages.get(r.stage, 0) + 1

    if not values:
        return {"count": 0}

    # Inter-frame arrival delta t
    intervals_ms: List[int] = []
    for i in range(1, len(timestamps)):
        dt = timestamps[i] - timestamps[i - 1]
        if dt >= 0:
            intervals_ms.append(dt)

    total_duration_ms = timestamps[-1] - timestamps[0] if len(timestamps) > 1 else 0

    stats = {
        "count": len(values),
        "min": min(values),
        "max": max(values),
        "mean": statistics.mean(values),
        "median": statistics.median(values),
        "stddev": statistics.stdev(values) if len(values) > 1 else 0.0,
        "stages": stages,
        "duration_ms": total_duration_ms,
        "dt_count": len(intervals_ms),
        "dt_min": min(intervals_ms) if intervals_ms else 0,
        "dt_max": max(intervals_ms) if intervals_ms else 0,
        "dt_mean": statistics.mean(intervals_ms) if intervals_ms else 0.0,
        "dt_median": statistics.median(intervals_ms) if intervals_ms else 0.0,
        "dt_stddev": statistics.stdev(intervals_ms) if len(intervals_ms) > 1 else 0.0,
        "estimated_hz": (len(values) * 1000.0 / total_duration_ms) if total_duration_ms > 0 else 0.0,
    }
    return stats


def generate_timeline(records: List[CanRecord],
                      byte_index: int = 0,
                      bin_size_sec: float = 1.0) -> List[Dict]:
    """Aggregates signal metrics over discrete time windows."""
    if not records:
        return []

    t0 = records[0].timestamp_ms
    bin_size_ms = int(bin_size_sec * 1000)
    if bin_size_ms <= 0:
        bin_size_ms = 1000

    bins: Dict[int, List[Tuple[int, int]]] = {}  # bin_index -> list of (stage, value)

    for r in records:
        if len(r.data) > byte_index and not r.remote:
            val = r.data[byte_index]
            rel_t = r.timestamp_ms - t0
            bin_idx = rel_t // bin_size_ms
            if bin_idx not in bins:
                bins[bin_idx] = []
            bins[bin_idx].append((r.stage, val))

    timeline: List[Dict] = []
    for bin_idx in sorted(bins.keys()):
        entries = bins[bin_idx]
        vals = [e[1] for e in entries]
        stg_counts: Dict[int, int] = {}
        for stg, _ in entries:
            stg_counts[stg] = stg_counts.get(stg, 0) + 1
        majority_stage = max(stg_counts.keys(), key=lambda s: stg_counts[s])

        sec = round((bin_idx * bin_size_ms + (bin_size_ms / 2.0)) / 1000.0, 3)
        timeline.append({
            "sec": sec,
            "stage": float(majority_stage),
            "count": len(vals),
            "median": float(statistics.median(vals)),
            "max": max(vals),
            "min": min(vals),
            "mean": round(statistics.mean(vals), 2)
        })

    return timeline


def export_timeline_csv(timeline: List[Dict], out_path: Path):
    """Exports generated timeline to a CSV file."""
    with out_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["sec", "stage", "count", "b0_median", "b0_max", "b0_min"])
        for row in timeline:
            writer.writerow([
                row["sec"],
                row["stage"],
                row["count"],
                row["median"],
                row["max"],
                row["min"]
            ])


def find_rational_approximation(value: float, max_denominator: int = 100) -> Tuple[int, int]:
    """Finds best integer fraction numerator / denominator approximating a float."""
    best_num = round(value)
    best_den = 1
    best_err = abs(value - float(best_num))

    for den in range(1, max_denominator + 1):
        num = round(value * den)
        err = abs(value - (float(num) / float(den)))
        if err < best_err:
            best_err = err
            best_num = num
            best_den = den
            if err < 1e-6:
                break
    return int(best_num), int(best_den)


def calibrate_linear(pairs: List[Tuple[float, float]]) -> Dict:
    """
    Performs linear calibration from raw-to-RPM measurement points: (raw_val, ref_rpm).
    Supports 1 point (proportional, zero-offset), 2 points (exact line), or N points (least-squares).
    """
    if not pairs:
        raise ValueError("At least one calibration pair (raw, rpm) is required.")

    if len(pairs) == 1:
        raw, rpm = pairs[0]
        if raw <= 0:
            raise ValueError(f"Single-point calibration requires raw > 0, got {raw}")
        slope = rpm / raw
        intercept = 0.0
        r_squared = 1.0
        num, den = find_rational_approximation(slope)
        return {
            "type": "Single-point proportional (Assumes zero offset)",
            "pairs_count": 1,
            "slope": slope,
            "intercept": intercept,
            "r_squared": r_squared,
            "scaleNumerator": num,
            "scaleDenominator": den,
            "offsetNumerator": 0,
            "warning": "Single-point calibration assumes zero offset (RPM = raw * slope). Linearity cannot be confirmed without multiple points."
        }

    elif len(pairs) == 2:
        (x1, y1), (x2, y2) = pairs[0], pairs[1]
        dx = x2 - x1
        if abs(dx) < 1e-9:
            raise ValueError("Two calibration points must have different raw values.")
        slope = (y2 - y1) / dx
        intercept = y1 - slope * x1
        num, den = find_rational_approximation(slope)
        offset_num = round(intercept * den)

        return {
            "type": "Two-point linear calibration",
            "pairs_count": 2,
            "slope": slope,
            "intercept": intercept,
            "r_squared": 1.0,
            "scaleNumerator": num,
            "scaleDenominator": den,
            "offsetNumerator": offset_num,
            "warning": "Two-point calibration is provisional. Physical validation across additional intermediate RPM points is recommended."
        }

    else:
        # N-point least-squares linear regression
        n = len(pairs)
        xs = [p[0] for p in pairs]
        ys = [p[1] for p in pairs]
        mean_x = statistics.mean(xs)
        mean_y = statistics.mean(ys)

        ss_xx = sum((x - mean_x) ** 2 for x in xs)
        ss_xy = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys))
        ss_yy = sum((y - mean_y) ** 2 for y in ys)

        if ss_xx < 1e-9:
            raise ValueError("All raw values are identical; cannot fit slope.")

        slope = ss_xy / ss_xx
        intercept = mean_y - slope * mean_x
        r_squared = (ss_xy ** 2) / (ss_xx * ss_yy) if ss_yy > 0 else 1.0

        residuals = [abs(y - (slope * x + intercept)) for x, y in zip(xs, ys)]
        rmse = math.sqrt(sum(r ** 2 for r in residuals) / n)
        max_residual = max(residuals)

        num, den = find_rational_approximation(slope)
        offset_num = round(intercept * den)

        return {
            "type": "Multi-point least-squares linear regression",
            "pairs_count": n,
            "slope": slope,
            "intercept": intercept,
            "r_squared": r_squared,
            "rmse": rmse,
            "max_residual": max_residual,
            "scaleNumerator": num,
            "scaleDenominator": den,
            "offsetNumerator": offset_num,
            "warning": "Verification requires independent holdout session to prevent overfitting."
        }


def parse_calibration_pairs(spec: str) -> List[Tuple[float, float]]:
    """
    Parses string like '35:1400,240:9600' or '35=1400 240=9600' into [(35.0, 1400.0), (240.0, 9600.0)].
    """
    pairs: List[Tuple[float, float]] = []
    # Replace spaces and semicolons with commas
    normalized = spec.replace(" ", ",").replace(";", ",")
    items = [item.strip() for item in normalized.split(",") if item.strip()]

    for item in items:
        sep = ":" if ":" in item else "="
        if sep not in item:
            raise ValueError(f"Invalid pair format '{item}'. Expected 'raw:rpm', e.g. '35:1400'")
        left, right = item.split(sep, 1)
        raw_val = float(left.strip())
        rpm_val = float(right.strip())
        pairs.append((raw_val, rpm_val))

    return pairs


def main():
    parser = argparse.ArgumentParser(
        description="PulseShift Offline CAN Analysis & RPM Calibration Tool",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Analyze 0x301 signal across all stages
  python tools/pulseshift_analyzer.py captures/PulseShift_CAN_22542_frames.csv --id 0x301

  # Generate 1-second timeline summary for 0x301 byte 0
  python tools/pulseshift_analyzer.py captures/PulseShift_CAN_22542_frames.csv --id 0x301 --timeline

  # Two-point linear calibration regression
  python tools/pulseshift_analyzer.py --calibrate "35:1400,240:9600"
        """
    )

    parser.add_argument("csv_file", nargs="?", default=None, help="Path to exported CAN frames CSV")
    parser.add_argument("--id", type=str, default="0x301", help="CAN arbitration ID in hex or dec (default: 0x301)")
    parser.add_argument("--byte", type=int, default=0, help="Payload byte index (default: 0)")
    parser.add_argument("--stage", type=str, default=None, help="Filter by stages, e.g. '1,2' or '3'")
    parser.add_argument("--timeline", action="store_true", help="Print timeline summary")
    parser.add_argument("--bin-size", type=float, default=1.0, help="Timeline bin size in seconds (default: 1.0)")
    parser.add_argument("--timeline-csv", type=str, default=None, help="Export timeline table to CSV file")
    parser.add_argument("--calibrate", type=str, default=None, help="Calibration pairs 'raw:rpm,...', e.g. '35:1400,240:9600'")
    parser.add_argument("--all-frames", action="store_true", help="Include frames failing CRC check (default: crc_ok=1 only)")
    parser.add_argument("--start-ms", type=int, default=None, help="Filter start timestamp in ms")
    parser.add_argument("--end-ms", type=int, default=None, help="Filter end timestamp in ms")

    args = parser.parse_args()

    # Handle standalone calibration option if provided
    if args.calibrate:
        pairs = parse_calibration_pairs(args.calibrate)
        res = calibrate_linear(pairs)

        print("\n=======================================================")
        print("          PULSESHIFT RPM CALIBRATION ENGINE")
        print("=======================================================")
        print(f"Model Type       : {res['type']}")
        print(f"Reference Points : {res['pairs_count']}")
        for idx, (raw_val, rpm_val) in enumerate(pairs, 1):
            print(f"  Point {idx}        : Raw {raw_val:6.1f} -> {rpm_val:7.1f} RPM")
        print("-------------------------------------------------------")
        print(f"Calculated Slope : {res['slope']:.4f} RPM / raw count")
        print(f"Calculated Offset: {res['intercept']:.2f} RPM")
        print(f"Equation         : RPM = ({res['slope']:.4f} * raw) + ({res['intercept']:.2f})")
        if "r_squared" in res:
            print(f"R-Squared (Fit)  : {res['r_squared']:.4f}")
        if "rmse" in res:
            print(f"RMSE Error       : {res['rmse']:.2f} RPM")
            print(f"Max Residual     : {res['max_residual']:.2f} RPM")
        print("-------------------------------------------------------")
        print("Firmware C++ Constants (Vehicle Profile):")
        print(f"  scaleNumerator   = {res['scaleNumerator']};")
        print(f"  scaleDenominator = {res['scaleDenominator']};")
        print(f"  offsetNumerator  = {res['offsetNumerator']};")
        print("-------------------------------------------------------")
        print(f"[CAUTION] {res['warning']}")
        print("Keep RPM_MAPPED=NO until confirmed with independent physical tachometer.")
        print("=======================================================\n")

        if not args.csv_file:
            return

    if not args.csv_file:
        print("[INFO] No CSV file specified. Run with -h for usage instructions.")
        return

    csv_path = Path(args.csv_file)
    print(f"[INFO] Loading CAN capture: {csv_path.name}...")
    records = load_can_csv(csv_path)
    print(f"[INFO] Loaded {len(records):,} valid records.")

    can_id_filter = parse_can_id(args.id) if args.id else None
    stage_filter = [int(s.strip()) for s in args.stage.split(",") if s.strip()] if args.stage else None

    filtered = filter_records(
        records,
        can_id=can_id_filter,
        stage=stage_filter,
        crc_only=not args.all_frames,
        start_ms=args.start_ms,
        end_ms=args.end_ms
    )

    id_str = f"0x{can_id_filter:03X}" if can_id_filter is not None else "ALL"
    print(f"[INFO] Matching records for CAN ID {id_str}: {len(filtered):,} frames")

    if not filtered:
        print("[WARN] No matching CAN frames found for specified filter.")
        return

    stats = analyze_signal(filtered, byte_index=args.byte)

    print("\n=======================================================")
    print(f"   PULSESHIFT SIGNAL SUMMARY (CAN ID {id_str}, BYTE {args.byte})")
    print("=======================================================")
    print(f"Total Frames Analyzed : {stats['count']:,}")
    print(f"Recording Span        : {stats['duration_ms'] / 1000.0:.3f} seconds")
    print(f"Estimated Rate        : {stats['estimated_hz']:.1f} Hz (frames / sec)")
    print("-------------------------------------------------------")
    print("Payload Byte Value Statistics (0..255):")
    print(f"  Minimum Observed    : {stats['min']}")
    print(f"  Maximum Observed    : {stats['max']}")
    print(f"  Median Value        : {stats['median']:.1f}")
    print(f"  Mean Value          : {stats['mean']:.2f}")
    print(f"  Standard Deviation  : {stats['stddev']:.2f}")
    print("-------------------------------------------------------")
    print("Inter-Frame Arrival Timing (Delta t):")
    print(f"  Min Interval        : {stats['dt_min']} ms")
    print(f"  Max Interval        : {stats['dt_max']} ms")
    print(f"  Median Interval     : {stats['dt_median']:.1f} ms")
    print(f"  Mean Interval       : {stats['dt_mean']:.2f} ms")
    print(f"  Interval StdDev     : {stats['dt_stddev']:.2f} ms")
    print("-------------------------------------------------------")
    print("Stage Distribution:")
    for stg, cnt in sorted(stats["stages"].items()):
        pct = (cnt / stats['count']) * 100.0
        stg_name = {1: "Key-On/Off", 2: "Idle", 3: "Gentle Sweep"}.get(stg, f"Stage {stg}")
        print(f"  Stage {stg} ({stg_name:12s}): {cnt:6,} frames ({pct:5.1f}%)")
    print("=======================================================\n")

    if args.timeline or args.timeline_csv:
        timeline = generate_timeline(filtered, byte_index=args.byte, bin_size_sec=args.bin_size)
        print(f"[INFO] Generated {len(timeline)} timeline bins ({args.bin_size}s windows).")

        if args.timeline:
            print("\nTimeline Sample (First 20 and Last 10 windows):")
            print(f"{'Time(s)':>8} | {'Stage':>5} | {'Count':>6} | {'Median':>6} | {'Min':>4} | {'Max':>4}")
            print("-" * 45)
            sample_rows = timeline[:20] + ([None] if len(timeline) > 30 else []) + (timeline[-10:] if len(timeline) > 30 else [])
            for r in sample_rows:
                if r is None:
                    print(f"{'...':>8} | {'...':>5} | {'...':>6} | {'...':>6} | {'...':>4} | {'...':>4}")
                else:
                    print(f"{r['sec']:8.2f} | {int(r['stage']):5d} | {r['count']:6d} | {r['median']:6.1f} | {r['min']:4d} | {r['max']:4d}")

        if args.timeline_csv:
            out_p = Path(args.timeline_csv)
            export_timeline_csv(timeline, out_p)
            print(f"[INFO] Timeline written to {out_p.resolve()}")


if __name__ == "__main__":
    main()
