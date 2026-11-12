#!/usr/bin/env python3
"""
PulseShift - Offline Diagnostics & Raw CAN Log Retrieval Tool
Retrieves stored offline diagnostic logs and raw CAN frames from ESP32 over USB Serial.
"""

import sys
import os
import time
import json
import argparse
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[ERROR] pyserial is not installed. Install via: pip install pyserial")
    print("Or use the native PowerShell script: .\\export-logs.ps1")
    sys.exit(1)


def detect_com_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("[ERROR] No serial ports found. Connect ESP32 via USB.")
        sys.exit(1)
    if len(ports) == 1:
        print(f"[INFO] Automatically detected port: {ports[0].device} ({ports[0].description})")
        return ports[0].device
    print("\nAvailable COM ports:")
    for idx, p in enumerate(ports):
        print(f"  [{idx+1}] {p.device} - {p.description}")
    sel = input(f"\nSelect port (1-{len(ports)}) or enter port name: ").strip()
    if sel.isdigit() and 1 <= int(sel) <= len(ports):
        return ports[int(sel)-1].device
    return sel


def send_command(ser, cmd, end_marker=None, timeout_sec=10):
    ser.reset_input_buffer()
    ser.write(f"{cmd}\r\n".encode('utf-8'))
    lines = []
    start = time.time()
    while time.time() - start < timeout_sec:
        line = ser.readline().decode('utf-8', errors='replace').rstrip('\r\n')
        if line:
            lines.append(line)
            if end_marker and end_marker in line:
                break
    return lines


def main():
    parser = argparse.ArgumentParser(description="PulseShift Offline Log & CAN Retrieval")
    parser.add_argument("-p", "--port", help="Serial COM port (e.g. COM3)")
    parser.add_argument("-b", "--baud", type=int, default=115200, help="Baud rate (default: 115200)")
    parser.add_argument("-o", "--output", help="Output directory")
    parser.add_argument("--clear", action="store_true", help="Prompt to clear logs after export")
    args = parser.parse_args()

    port = args.port or detect_com_port()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_dir = args.output or os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs", f"export_{timestamp}")
    os.makedirs(out_dir, exist_ok=True)

    print(f"[INFO] Connecting to {port} at {args.baud} baud...")
    ser = serial.Serial(port=port, baudrate=args.baud, timeout=2.0, dsrdtr=False, rtscts=False)
    time.sleep(0.3)
    ser.reset_input_buffer()

    try:
        # Step 1: STATUS
        print("\n[STEP 1/3] Querying System Status (STATUS)...")
        status_lines = send_command(ser, "STATUS", end_marker="================================", timeout_sec=5)
        status_path = os.path.join(out_dir, "status.txt")
        with open(status_path, "w", encoding="utf-8") as f:
            f.write("\n".join(status_lines))
        print(f"  -> Saved status: {status_path}")

        meta = {
            "ExportTimestamp": datetime.now().isoformat(),
            "Port": port,
            "Baud": args.baud,
            "Status": status_lines
        }
        with open(os.path.join(out_dir, "metadata.json"), "w", encoding="utf-8") as f:
            json.dump(meta, f, indent=2)

        # Step 2: LOGS
        print("\n[STEP 2/3] Retrieving Diagnostic Events (LOGS)...")
        log_lines = send_command(ser, "LOGS", end_marker="=== END PULSESHIFT DIAGNOSTIC LOGS ===", timeout_sec=8)
        log_path = os.path.join(out_dir, "diagnostics.log")
        with open(log_path, "w", encoding="utf-8") as f:
            f.write("\n".join(log_lines))
        print(f"  -> Saved diagnostic events ({len(log_lines)} lines): {log_path}")

        # Step 3: DUMP
        print("\n[STEP 3/3] Downloading Raw CAN Capture (DUMP)...")
        dump_lines = send_command(ser, "DUMP", end_marker="=== END PULSESHIFT RAW CAN CAPTURE", timeout_sec=30)
        csv_lines = []
        frames = []
        in_csv = False
        valid_crc = 0
        corrupt_crc = 0

        for line in dump_lines:
            if line.startswith("timestamp_ms,"):
                in_csv = True
                csv_lines.append(line)
                continue
            if "=== END PULSESHIFT RAW CAN CAPTURE" in line:
                in_csv = False
                continue
            if in_csv and line.strip():
                csv_lines.append(line)
                parts = line.split(",")
                if len(parts) >= 8:
                    crc_ok = (parts[7].strip() == "1")
                    if crc_ok: valid_crc += 1
                    else: corrupt_crc += 1
                    frames.append({
                        "TimestampMs": int(parts[0]),
                        "CanId": "0x" + parts[1],
                        "Extended": parts[2] == "1",
                        "Remote": parts[3] == "1",
                        "DLC": int(parts[4]),
                        "DataHex": parts[5],
                        "Stage": int(parts[6]),
                        "CrcValid": crc_ok
                    })

        csv_path = os.path.join(out_dir, "can_capture.csv")
        with open(csv_path, "w", encoding="utf-8") as f:
            f.write("\n".join(csv_lines))

        json_path = os.path.join(out_dir, "can_capture.json")
        with open(json_path, "w", encoding="utf-8") as f:
            json.dump(frames, f, indent=2)

        print(f"  -> Saved CAN CSV: {csv_path}")
        print(f"  -> Saved CAN JSON: {json_path}")
        print(f"  -> Frames: {len(frames)} (Valid CRC: {valid_crc}, Corrupted: {corrupt_crc})")

        print("\n==========================================================")
        print("                 EXPORT COMPLETED SUCCESSFULLY            ")
        print("==========================================================")
        print(f"Destination: {out_dir}")

        if args.clear:
            conf = input("\n[CONFIRMATION] Clear stored logs on ESP32 now? (type YES): ").strip()
            if conf == "YES":
                resp = send_command(ser, "CLEAR YES", end_marker="[OK]", timeout_sec=5)
                print("\n".join(resp))
                print("[OK] ESP32 offline storage cleared.")
            else:
                print("[INFO] Data preserved on ESP32.")

    finally:
        ser.close()
        print(f"[INFO] Port {port} closed.")


if __name__ == "__main__":
    main()
