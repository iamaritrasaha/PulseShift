#!/usr/bin/env python3
"""
PulseShift Real Motorcycle CAN Acquisition Tool (Optional Advanced Tooling)
Copyright (c) 2026 Aritra Saha. All rights reserved.

Optional legacy/advanced PC acquisition utility. Not required for normal
PulseShift operation; PulseShift firmware now records directly to onboard flash
and exports via Arduino IDE Serial Monitor without requiring Python.

Captures real-time motorcycle CAN frames and diagnostic telemetry streamed from
the ESP32 via USB Serial (CP2102) to disk.

Note: Live frame streaming is disabled by default in PulseShift firmware to avoid
overwhelming Arduino IDE. This tool sends 'STREAM ON' upon connection to enable
raw frame streaming.

Usage:
  python tools/pulseshift_capture.py [--port COM5] [--baud 115200]
  uv run --with pyserial python tools/pulseshift_capture.py
"""

import sys
import os
import time
import argparse
import datetime
import shutil
import threading
import tempfile

try:
    import serial
except ImportError:
    serial = None


MIN_FREE_DISK_BYTES = 50 * 1024 * 1024  # 50 MB safety headroom


class PulseShiftCapture:
    def __init__(self, port="COM5", baud=115200, output_dir="captures", timeout=1.0):
        self.port = port
        self.baud = baud
        # Contain writes to the requested output directory: reject path
        # traversal components instead of following them out of the tree.
        out_dir = os.path.normpath(output_dir or "captures")
        if any(part == ".." for part in out_dir.replace("\\", "/").split("/") if part):
            raise ValueError("output_dir must not contain '..' path components")
        self.output_dir = out_dir
        self.timeout = timeout

        self.ser = None
        self.running = False
        self.csv_file = None
        self.log_file = None
        self.csv_path = ""
        self.log_path = ""

        self.frames_recorded = 0
        self.start_time = 0
        self.stages_seen = set()
        self.current_stage = 1
        self.esp32_dropped = 0
        self.esp32_serial_dropped = 0
        self.lock = threading.Lock()

    def check_disk_space(self):
        try:
            usage = shutil.disk_usage(self.output_dir)
            if usage.free < MIN_FREE_DISK_BYTES:
                print(f"\n[ALERT] Free disk space ({usage.free // (1024 * 1024)} MB) is below 50 MB limit.")
                print("[ALERT] Stopping capture to preserve files.")
                return False
        except Exception:
            pass
        return True

    def open_serial(self):
        if serial is None:
            print("\n[ERROR] 'pyserial' package is not installed.")
            print("Install it with:")
            print("    pip install pyserial")
            print("Or run using uv:")
            print("    uv run --with pyserial python tools/pulseshift_capture.py\n")
            return False

        try:
            self.ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
            print(f"[OK] Connected to PulseShift ESP32 on {self.port} at {self.baud} baud.")
            time.sleep(0.1)
            self.send_command("STREAM ON")
            return True
        except serial.SerialException as ex:
            err_str = str(ex)
            print(f"\n[ERROR] Failed to open serial port {self.port}: {err_str}")
            if "Access is denied" in err_str or "PermissionError" in err_str:
                print("\n[HINT] The serial port is currently in use by another program.")
                print("      Please close Arduino IDE Serial Monitor, Serial Plotter,")
                print("      or any other open terminal connected to " + self.port + " and try again.\n")
            elif "FileNotFoundError" in err_str or "cannot find the file" in err_str:
                print(f"\n[HINT] Device on port {self.port} not found. Check USB cable and COM port number in Device Manager.\n")
            return False

    def send_command(self, cmd):
        with self.lock:
            if self.ser and self.ser.is_open:
                try:
                    payload = (cmd.strip() + "\r\n").encode("utf-8")
                    self.ser.write(payload)
                    self.ser.flush()
                    print(f"[SENT] >> {cmd.strip()}")
                except Exception as ex:
                    print(f"[ERROR] Failed to send command: {ex}")

    def interactive_input_thread(self):
        print("\nInteractive controls:")
        print("  Press '1', '2', or '3' + Enter to change capture stage")
        print("  Press 's' + Enter for STATUS")
        print("  Press 'd' + Enter to DUMP flash capture")
        print("  Press 'q' + Enter to stop recording and exit\n")

        while self.running:
            try:
                line = sys.stdin.readline()
                if not line:
                    break
                cmd = line.strip().lower()
                if cmd in ("1", "stage 1"):
                    self.send_command("STAGE 1")
                elif cmd in ("2", "stage 2"):
                    self.send_command("STAGE 2")
                elif cmd in ("3", "stage 3"):
                    self.send_command("STAGE 3")
                elif cmd in ("s", "status"):
                    self.send_command("STATUS")
                elif cmd in ("d", "dump"):
                    self.send_command("DUMP")
                elif cmd in ("q", "quit", "exit"):
                    print("[INFO] Stop requested by user.")
                    self.running = False
                    break
                elif cmd:
                    # Send arbitrary command directly to ESP32
                    self.send_command(cmd)
            except Exception:
                break

    def run(self):
        os.makedirs(self.output_dir, exist_ok=True)
        if not self.check_disk_space():
            return 1

        if not self.open_serial():
            return 1

        timestamp_str = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        root = os.path.realpath(self.output_dir)
        os.makedirs(root, exist_ok=True)
        # The OS creates these files with safe random names inside the
        # validated output directory: no path traversal is possible.
        csv_fd, csv_tmp = tempfile.mkstemp(prefix=f"pulseshift_capture_{timestamp_str}_", suffix=".csv", dir=root)
        log_fd, log_tmp = tempfile.mkstemp(prefix=f"pulseshift_capture_{timestamp_str}_", suffix=".log", dir=root)
        self.csv_path = csv_tmp
        self.log_path = log_tmp
        self.csv_file = os.fdopen(csv_fd, "w", encoding="utf-8", buffering=1)
        self.log_file = os.fdopen(log_fd, "w", encoding="utf-8", buffering=1)

        # Write CSV header
        self.csv_file.write("timestamp_ms,stage,can_id,extended,rtr,dlc,data\n")

        self.running = True
        self.start_time = time.time()

        # Start background keyboard listener thread
        input_thread = threading.Thread(target=self.interactive_input_thread, daemon=True)
        input_thread.start()

        print(f"[RECORDING] Streaming frames to: {self.csv_path}")
        print(f"[LOGGING]   Diagnostics to:    {self.log_path}")
        print("Waiting for motorcycle CAN data... (Press Ctrl+C or 'q' to stop)\n")

        last_disk_check = time.time()
        last_flush = time.time()

        try:
            while self.running:
                now_t = time.time()
                if now_t - last_disk_check >= 10.0:
                    last_disk_check = now_t
                    if not self.check_disk_space():
                        self.running = False
                        break

                raw_line = self.ser.readline()
                if not raw_line:
                    continue

                try:
                    line = raw_line.decode("utf-8", errors="replace").strip()
                except Exception:
                    continue

                if not line:
                    continue

                # Process line types
                if line.startswith("FRAME,"):
                    # Format: FRAME,timestamp,stage,id,ext,rtr,dlc,data
                    parts = line.split(",", 7)
                    if len(parts) >= 8:
                        ts, stage, can_id, ext, rtr, dlc, data = parts[1:8]
                        try:
                            st = int(stage)
                            self.stages_seen.add(st)
                            self.current_stage = st
                        except ValueError:
                            pass

                        csv_row = f"{ts},{stage},{can_id},{ext},{rtr},{dlc},{data}\n"
                        self.csv_file.write(csv_row)
                        self.frames_recorded += 1

                        if self.frames_recorded == 1:
                            print(f"[CAN FIRST FRAME] ID=0x{can_id} DLC={dlc} DATA={data} Stage={stage}")
                        elif self.frames_recorded % 100 == 0:
                            print(f"[CAN CAPTURE] {self.frames_recorded} frames logged | Stage {self.current_stage}")

                        if now_t - last_flush >= 1.0:
                            last_flush = now_t
                            self.csv_file.flush()

                elif line.startswith("DIAG,"):
                    # Diagnostic event: DIAG,timestamp,level,tag,message
                    self.log_file.write(line + "\n")
                    parts = line.split(",", 4)
                    if len(parts) >= 5:
                        ts, lvl, tag, msg = parts[1:5]
                        print(f"[{lvl}] {tag}: {msg}")
                    else:
                        print(f"[DIAG] {line}")

                elif line.startswith("STAGE,"):
                    # Stage change event: STAGE,timestamp,stage,name
                    self.log_file.write(line + "\n")
                    parts = line.split(",", 3)
                    if len(parts) >= 4:
                        print(f"\n>>> [STAGE SWITCH] Stage {parts[2]} ({parts[3]}) <<<\n")
                    else:
                        print(f"\n>>> [STAGE SWITCH] {line} <<<\n")

                elif line.startswith("META,"):
                    self.log_file.write(line + "\n")
                    print(f"[ESP32 META] {line[5:]}")

                elif line.startswith("STAT,"):
                    # Telemetry: STAT,ts,rx_total,accepted,missed,overrun,bus_err,serial_dropped,rec,rec_dropped
                    self.log_file.write(line + "\n")
                    parts = line.split(",")
                    if len(parts) >= 10:
                        try:
                            self.esp32_dropped = int(parts[4]) + int(parts[5])
                            self.esp32_serial_dropped = int(parts[7])
                        except ValueError:
                            pass

                else:
                    # General status, boot logs, banner
                    self.log_file.write(line + "\n")
                    if "CAN" in line or "STORAGE" in line or "STATUS" in line or "TARGET" in line:
                        print(f"[ESP32] {line}")

        except KeyboardInterrupt:
            print("\n[INFO] Capture interrupted by user (Ctrl+C).")

        finally:
            self.running = False
            if self.ser and self.ser.is_open:
                try:
                    self.ser.close()
                except Exception:
                    pass
            if self.csv_file:
                self.csv_file.flush()
                self.csv_file.close()
            if self.log_file:
                self.log_file.flush()
                self.log_file.close()

            duration = time.time() - self.start_time
            self.print_summary(duration)

        return 0

    def print_summary(self, duration):
        print("\n" + "=" * 55)
        print("          PULSESHIFT CAPTURE SUMMARY")
        print("=" * 55)
        print(f"  Duration:              {duration:.1f} seconds")
        print(f"  Frames Recorded to PC: {self.frames_recorded}")
        stages_str = ", ".join(f"Stage {s}" for s in sorted(self.stages_seen)) if self.stages_seen else "None"
        print(f"  Stages Covered:        {stages_str}")
        print(f"  ESP32 Dropped Frames:  {self.esp32_dropped}")
        print(f"  Serial Buffer Dropped: {self.esp32_serial_dropped}")
        print(f"  CSV Output:            {self.csv_path}")
        print(f"  Diagnostic Log:        {self.log_path}")
        print("=" * 55 + "\n")


def parse_args():
    parser = argparse.ArgumentParser(
        description="PulseShift Real Motorcycle CAN Acquisition Tool"
    )
    parser.add_argument(
        "--port",
        default="COM5",
        help="Serial port connected to ESP32 (default: COM5)"
    )
    parser.add_argument(
        "--baud",
        type=int,
        default=115200,
        help="Serial baud rate (default: 115200)"
    )
    parser.add_argument(
        "--output-dir",
        default="captures",
        help="Directory to save CSV and log captures (default: captures)"
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=1.0,
        help="Serial read timeout in seconds (default: 1.0)"
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    tool = PulseShiftCapture(
        port=args.port,
        baud=args.baud,
        output_dir=args.output_dir,
        timeout=args.timeout
    )
    sys.exit(tool.run())
