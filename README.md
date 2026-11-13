# PulseShift

Customizable ESP32 motorcycle shift-light and real CAN bus acquisition firmware. The firmware boots directly into passive CAN capture mode to acquire, stream, and record motorcycle CAN traffic from the Bajaj Pulsar NS125 ABS (2025), enabling exact identification and calibration of the motorcycle's RPM message. Once the vehicle profile is calibrated against an independent reference tachometer, six WS2812B LEDs light up progressively with engine RPM, flashing red at redline/shift RPM as the shift alert.

## What it does

- **CAN Capture Mode (Default Production Mode)**: Boots directly into passive listen-only mode at 500,000 bps, receiving real motorcycle CAN frames, streaming rate-limited candidate telemetry, and recording frames with CRC16 checksums to internal flash (LittleFS).
- **Physical CAN Acquisition Verified**: Real-world motorcycle acquisition successfully verified on a **2025 Bajaj Pulsar NS125 ABS** at 500,000 bps (Session 2: 22,542 exported records across 3 stages, 18 distinct CAN IDs, 0 corrupt records, ~82.5s duration).
- **Live RPM Candidate Monitor (CAN ID 0x301)**: Continuously monitors the provisional engine-speed signal identified on the Pulsar NS125 (arbitration ID `0x301`, payload byte 0). Calculates estimated RPM using a configurable provisional multiplier (default 40.0), tracks signal freshness with a 250 ms timeout, detects signal anomalies, and provides instant telemetry via `RPM STATUS` and `RPM MONITOR ON`.
- **Bench-Testable Shift Light Engine**: The 6-LED WS2812B shift-light engine can be bench tested with simulated RPM inputs (`RPM TEST <val>`), verifying color transitions and shift alerts without vehicle operation. In vehicle operation, physical engine-triggered shift lights remain strictly disabled (`RPM_MAPPED=NO`) until calibration against an independent physical tachometer is complete.
- **Optimized CAN Reliability & High-Throughput Buffering**: Flash operations in CanRecorder are batched into 512-byte blocks with an expanded 256-frame ring buffer and multi-frame drain loop. Telemetry counters independently track ring drops (`rec_dropped`), TWAI driver queue drops (`missed`), and hardware FIFO overruns (`overrun`).
- **Intelligent Diagnostics & Error Logging**: Real-time diagnostic events (CAN overflows, bus errors, storage full alerts, reset reasons) are streamed over USB and safely recorded to flash without circular wipes.
- **Understandable LED Hardware Status**: In CAN capture mode, the six LEDs display controller health, frame arrival heartbeat, active recording stage (1, 2, or 3), and flash storage status.

> [!IMPORTANT]
> **Production Integrity Policy**: The production firmware contains no simulation, artificial RPM generation, or synthetic ECU traffic in vehicle capture mode. If no motorcycle CAN frames arrive, the system truthfully reports `NO_CAN_TRAFFIC`. Mock CAN frames are strictly restricted to isolated host automated unit tests.

## Parts

![Gather these parts](assets/readme/hardware-overview.svg)

| Part | Notes |
|---|---|
| ESP32 DevKit V1 (ESP32-WROOM-32) | 30-pin board with micro-USB data cable |
| 6x WS2812B LED strip (5V, GRB, 800kHz) | 6-LED addressable segment |
| 330-470 ohm resistor | In series with LED data line (close to DIN) |
| 1000 uF 10V electrolytic capacitor | Across LED power rails at the strip (+ to 5V, - to GND) |
| Regulated 5V supply, >= 1A (or DC-DC buck) | Powers LEDs (and ESP32 VIN when on motorcycle) |
| SN65HVD230 3.3V CAN transceiver breakout | Breakout pins labeled `CTX` (Pin 1 / TXD) and `CRX` (Pin 4 / RXD). Verify termination with multimeter; desolder R2 only if ~120 ohms is measured (do not alter if unpopulated) |
| Direct wire (CTX to 3V3) | Breakout CTX connects directly to ESP32 3V3 (no 10k resistor). Keeps transmit driver permanently recessive |
| Physical air gap (GPIO17) | ESP32 GPIO17 has no wire inserted; completely disconnected from transceiver |
| Optional: 74AHCT125 level shifter | For reliable 5V logic signaling over longer harness runs |
| Optional: SSD1306 128x64 OLED display | I2C display (disabled by default in UserConfig.h) |

---

## Hardware Wiring

PulseShift is designed for quick bench verification using interactive serial test commands (`RPM TEST <val>`), followed by clean, passive in-vehicle installation for CAN capture and shift-light operation. Production builds strictly exclude synthetic demo modes to prevent unverified traffic on the motorcycle.

### Complete System Circuit

![PulseShift complete wiring](assets/readme/pulseshift-wiring.svg)

### ESP32 DevKit V1 Pinout

![ESP32 DevKit V1 pinout](assets/readme/esp32-pinout.svg)

### Quick Reference Wiring Table

| Component | Pin / Net | Connects To | Electrical Specification | Purpose |
|---|---|---|---|---|
| **WS2812B Strip** | DIN | ESP32 GPIO27 | 3.3V logic through 330-470 ohm resistor | High-speed 800 kHz pixel data |
| **WS2812B Strip** | +5V | Regulated 5V Rail | 5.0V DC (with 1000 uF bulk cap) | LED power (never connect to 3.3V) |
| **WS2812B Strip** | GND | Common Ground | Ground plane | Power and signal return |
| **WS2812B Strip** | DOUT | *Unconnected* | Open circuit | DOUT on 6th LED remains unused |
| **SN65HVD230 Breakout** | 3V3 (Pin 3 / VCC) | ESP32 3V3 Pin | Regulated 3.3V DC strictly | Transceiver logic power (never 5V or 12V) |
| **SN65HVD230 Breakout** | GND (Pin 2) | Common Ground | Ground plane | Logic and differential reference |
| **SN65HVD230 Breakout** | CRX (Pin 4 / RXD) | ESP32 GPIO16 | 3.3V digital logic | Received CAN bitstream to TWAI controller |
| **SN65HVD230 Breakout** | CTX (Pin 1 / TXD) | ESP32 3V3 Pin | Directly tied to 3.3V (NO resistor) | Hardware safety: forces driver recessive |
| **ESP32** | GPIO17 (TX2) | *Disconnected* | Physical air gap | Never connect GPIO17 to transceiver (software cannot detect wiring) |
| **SN65HVD230 Breakout** | Rs (Pin 8) | Common Ground | Connected to GND (often hardwired on PCB) | Normal high-speed receiver mode (up to 1 Mbps) |
| **SN65HVD230 Breakout** | Vref (Pin 5) | *Unconnected* | Leave open (NC) | Nominal VCC/2 reference output |
| **SN65HVD230 Breakout** | CANH (Pin 7) | Motorcycle CAN-H | ISO 11898-2 differential high | High-speed differential bus tap |
| **SN65HVD230 Breakout** | CANL (Pin 6) | Motorcycle CAN-L | ISO 11898-2 differential low | High-speed differential bus tap |
| **SN65HVD230 Breakout** | 120 ohm Resistor | *Verification Required* | Multimeter test across unpowered CANH-CANL | Desolder R2 only if ~120 ohms is measured; leave unmodified if open circuit |
| **OLED (Optional)**| SDA | ESP32 GPIO21 | 3.3V I2C Data | Optional diagnostic screen (disabled by default) |
| **OLED (Optional)**| SCL | ESP32 GPIO22 | 3.3V I2C Clock | Optional diagnostic screen (disabled by default) |
| **OLED (Optional)**| VCC / GND | ESP32 3V3 / GND | 3.3V DC / Ground | Optional screen power |

---

### WS2812B LED Shift Light Circuit

![WS2812B LED circuit](assets/readme/ws2812b-circuit.svg)

1. **Series Resistor**: Place a 330 to 470 ohm resistor in series with the data line as close to the first LED's `DIN` terminal as possible. This resistor damps high-frequency ringing and protects the first LED's input circuitry from voltage spikes.
2. **Bulk Capacitor**: Place a 1000 uF / 10V (or 16V) electrolytic capacitor across +5V and GND at the strip power entry. Observe polarity (+ lead to 5V, striped - lead to GND).
3. **Data Direction**: Connect to `DIN` (Data In), following the printed directional arrow. Leave `DOUT` on the 6th LED completely unconnected.
4. **Optional Level Shifter**: While direct 3.3V drive works on the bench over short wires, the WS2812B datasheet specifies minimum logic HIGH as `0.7 * VDD = 3.5V` when powered at 5.0V. An SN74AHCT125 level shifter powered at 5V provides true 5.0V signaling for vehicle installations.

![LED data direction](assets/readme/led-direction.svg)

---

### Passive CAN Transceiver Circuit (SN65HVD230)

![SN65HVD230 CAN circuit](assets/readme/sn65hvd230-circuit.svg)

The PulseShift firmware operates strictly as a passive CAN listener. It never transmits frames, sends acknowledgments (ACK), or injects electrical dominant bits onto the motorcycle network.

#### Mandatory Hardware Safety Safeguards

1. **GPIO17 Physical Air Gap**: ESP32 GPIO17 (TWAI TX) must remain completely disconnected. Do not insert a wire into GPIO17. Software listen-only mode alone cannot guarantee electrical isolation against bootloader glitches or firmware misconfiguration, and software cannot detect whether an external wire is physically present.
2. **CTX / TXD Forced Recessive via Direct 3.3V Connection**: Transceiver breakout pin `CTX` (Pin 1 / TXD) is connected directly to the ESP32 3V3 rail (NO 10k resistor was used in the verified working configuration). On ISO 11898 transceivers, a constant HIGH input clamps the differential driver permanently into the recessive (idle) state.
3. **Transceiver Rs to GND**: SN65HVD230 Pin 8 (Rs) connects to GND to select normal high-speed operation (up to 1 Mbps). On standard breakout modules, Rs is often tied to GND on the PCB; if broken out, connect to GND. Rs LOW does NOT enable transmission when CTX is held high and GPIO17 is disconnected.
4. **Check Onboard 120 Ohm Termination Resistor**: Many commercial breakout boards include an SMD 120 ohm terminating resistor (often labeled `R2` or `1200`). The motorcycle CAN bus is already terminated with 120 ohm resistors at each physical end (total bus resistance = 60 ohms). Adding a third 120 ohm resistor drops the bus load to 40 ohms, which can trigger ECU bus error warnings. Test with a multimeter across unpowered CANH-CANL terminals: if ~120 ohms is measured, desolder resistor R2 (or cut the trace jumper). If open circuit, no desoldering is needed.

---

### Power Safety and Ground Rules

![Power safety](assets/readme/power-safety.svg)

- **Common Ground is Mandatory**: The negative terminal of the 5V power supply, ESP32 GND, LED strip GND, transceiver GND, and motorcycle chassis/signal ground must all be securely bonded together.
- **Bench Power Order**: When using USB and an external 5V power supply on the bench:
  1. Power ON: Switch on the external 5V LED supply first, then plug in the ESP32 USB cable.
  2. Power OFF: Disconnect the ESP32 USB cable first, then turn off the external 5V supply.
- **Motorcycle Power Conversion**: Never connect raw motorcycle +12V battery or ignition power directly to the ESP32 or SN65HVD230. Always use an automotive-grade DC-DC step-down buck converter (12V to regulated 5.0V, minimum 2A rating) protected by an inline 1A fuse.

---

## How the Circuit Works

PulseShift decodes engine speed and displays progressive shift-light animations through a multi-stage hardware and software pipeline:

```text
Motorcycle ECU 
  -> Differential CAN bus (CAN-H / CAN-L)
  -> SN65HVD230 Transceiver (differential to 3.3V digital bitstream)
  -> ESP32 GPIO16 (TWAI controller extracts frames)
  -> PulseShift RPM Decoder (payload bytes decoded to integer RPM)
  -> Shift Light Logic (computes progressive fill & 6000 RPM flash alert)
  -> ESP32 GPIO27 (800 kHz precision timed NRZ pulses)
  -> WS2812B LED Strip (Pixels 1 to 6)
```

### Understanding the Signal Path

1. **Differential CAN Electrical Signals**:
   The motorcycle ECU broadcasts information by modulating the voltage difference between CAN-H and CAN-L lines:
   - Recessive state (logical 1): CAN-H and CAN-L both rest near 2.5V (difference ~0.0V).
   - Dominant state (logical 0): CAN-H is pulled to ~3.5V and CAN-L is pulled to ~1.5V (difference ~2.0V).
2. **Physical Translation (SN65HVD230)**:
   The transceiver measures the differential voltage across the bus and converts it into a single-ended 3.3V digital logic signal output on Pin 4 (RXD), feeding directly into ESP32 GPIO16.
3. **Packet Extraction (ESP32 TWAI Controller)**:
   The ESP32's built-in Two-Wire Automotive Interface (TWAI) peripheral handles bit timing, synchronization, and CRC verification. In listen-only mode, it captures incoming CAN frames without acknowledging them or transmitting error frames.
4. **RPM Decoding Framework**:
   Each received CAN frame has an identifier (CAN ID) and up to 8 payload bytes. When the motorcycle's specific RPM frame arrives, the decoder extracts the two data bytes representing engine speed, combines them into a 16-bit integer, and applies scaling:
   $$\text{Engine RPM} = (\text{Raw Payload Integer} \times \text{Multiplier}) + \text{Offset}$$
5. **Shift-Light Threshold Engine**:
   The firmware compares the decoded RPM to the configured thresholds in `UserConfig.h` (0 to 6000 RPM range, 1000 RPM progressive increments). When RPM reaches 6000, all six LEDs flash red in unison at 10 Hz.
6. **Pixel Output Generation (GPIO27)**:
   The ESP32 converts the color values into high-speed 800 kHz non-return-to-zero (NRZ) pulses on GPIO27, shifting serial data through pixels 1 to 6.

> [!NOTE]
> **Key Distinctions:**
> - **CAN electrical signals**: Raw physical bus voltages on twisted-pair copper wire.
> - **CAN frame identifiers**: Packet addresses indicating the topic of the message.
> - **CAN payload bytes**: Raw binary bytes carrying sensor and telemetry values.
> - **Engine RPM values**: Scaled physical speed in revolutions per minute.
> - **Shift-light thresholds**: User-configured trip points determining LED illumination.
>
> **Status Disclosure**: The Bajaj Pulsar NS125 RPM CAN ID and mathematical scaling formula are currently unverified. The firmware uses placeholder settings until real CAN capture data is analyzed.

For extended electrical theory, TVS protection schematics, and oscilloscope verification notes, see [docs/HARDWARE.md](docs/HARDWARE.md).

---

## What happens after upload

![Expected LED behavior](assets/readme/expected-behavior.svg)

### LED behavior: 0 to 6000 RPM

| RPM | LEDs | Color / Effect |
|---|---|---|
| 0 | All off | Standby / dark |
| 1-1000 | LED 1 | Green |
| 1001-2000 | LED 1-2 | Green |
| 2001-3000 | LED 1-3 | Green |
| 3001-4000 | LED 1-4 | Green / Yellow |
| 4001-5000 | LED 1-5 | Yellow / Orange |
| 5001-5999 | LED 1-6 | Orange / Red |
| >= 6000 | All 6 LEDs | **Flashing Red: Shift Alert!** |

In bench test mode (`RPM TEST <val>`) or once vehicle RPM calibration is verified, thresholds can be adjusted in `UserConfig.h` using `PULSESHIFT_RPM_REV_START` (default 5000), `PULSESHIFT_RPM_SHIFT` (default 8000), and `PULSESHIFT_RPM_MAX` (default 10500).

---

## Arduino IDE: Board Settings

| Setting | Value |
|---|---|
| Board | **ESP32 Dev Module** |
| Upload Speed | **921600** |
| Flash Size | **4MB (32Mb)** |
| Partition Scheme | **Default 4MB with spiffs** |
| PSRAM | **Disabled** |

> [!IMPORTANT]
> The "Default 4MB with spiffs" partition scheme includes a LittleFS-compatible data partition used for automatic diagnostic logging and CAN capture. PulseShift formats this partition on first boot if it is blank. It never erases an existing partition that already contains data.

### Required libraries (install via Library Manager)

- **Adafruit NeoPixel** >= 1.12.3
- **Adafruit SSD1306** >= 2.5.13 *(only needed if OLED is enabled)*
- **Adafruit GFX Library** >= 1.11.11 *(only needed if OLED is enabled)*
- **Adafruit BusIO** >= 1.17.0 *(only needed if OLED is enabled)*

### ESP32 board support

Add this URL to **File -> Preferences -> Additional board manager URLs**:

```text
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then install **esp32 by Espressif Systems** (version 3.3.12 recommended).

## How to upload

1. Open `PulseShift/PulseShift.ino` in Arduino IDE.
2. Select **ESP32 Dev Module** in Tools -> Board.
3. Set partition scheme to **Default 4MB with spiffs**.
4. Select your COM port.
5. Click **Upload** (Ctrl+U).

That's it. No scripts, no command line.

---

## Automatic error logging

PulseShift automatically logs errors to internal flash (LittleFS) with no action needed:

- CAN initialization failures and bus errors
- Driver queue overruns and hardware FIFO overruns
- Unexpected reset reasons
- Storage mount failures
- Stale/missing RPM events

Logs survive power-off and ESP32 restarts. They are never erased on reconnection.

## Real Motorcycle CAN Acquisition Workflow (Arduino IDE-Only)

PulseShift provides a completely self-contained workflow requiring **only the Arduino IDE**. No Python, pyserial, uv, standalone applications, or command-line scripts are required.

### 1. The Eight-Step Field Workflow

1. **Open Sketch**: Open `PulseShift/PulseShift.ino` in Arduino IDE 2.3.10.
2. **Upload Firmware**: Verify board is **ESP32 Dev Module**, partition scheme is **Default 4MB with spiffs**, and click **Upload** (Ctrl+U).
3. **Open Serial Monitor**: Open Serial Monitor in Arduino IDE (bottom panel or Tools -> Serial Monitor) and set the baud rate to **115200 baud**.
4. **Connect Verified Hardware**: Connect the CAN transceiver to the motorcycle diagnostic connector (CAN-H and CAN-L) and verify 3.3V power, ground, and GPIO16 (RXD).
5. **Automatic Flash Recording**:
   - On boot, the ESP32 automatically mounts LittleFS flash storage and begins recording real CAN frames immediately.
   - Concise live summaries are printed every 2 seconds without flooding Serial Monitor:
     ```text
     [LIVE] Bus: OK (500000 bps) | Traffic: ACTIVE | Stage 1 (KEY_ON_ENGINE_OFF) | RX: 1450 | Rec: 1450 | Drops: 0 | Flash: 29 KB
     ```
   - Raw frame streaming over serial is disabled by default to keep Arduino IDE smooth and responsive. If desired, type `STREAM ON` to view raw live frames.
6. **Execute the Three Recording Stages**:
   - **Stage 1 (Key ON, Engine OFF)**: Ignition on, engine stopped. Captures cluster wake-up and baseline sensor frames.
     Type `STAGE 1` (or `1`) in Serial Monitor.
   - **Stage 2 (Engine Idling)**: Start motorcycle and idle steady (~1300-1500 RPM).
     Type `STAGE 2` (or `2`) in Serial Monitor.
   - **Stage 3 (Gentle RPM Sweeps)**: Gently rev engine across 3000-6000 RPM.
     Type `STAGE 3` (or `3`) in Serial Monitor.
   - *Optional button control*: You can also short-press the ESP32 physical BOOT button (GPIO0) to cycle stages, or long-press (> 1.5s) to pause/resume recording.
7. **Export Stored Frames using DUMP**:
   - Type `DUMP` in Serial Monitor.
   - The ESP32 safely flushes all pending records, pauses active recording, and exports frames in clean **500-record pages**:
     ```text
     === BEGIN PULSESHIFT RAW CAN CAPTURE [PAGE 1/3: RECORDS 1-500 OF 1450] ===
     [HEADER] magic=PULSECAP ver=1 recSize=20 sess=1 boot=1 bitrate=500000 startMs=1200 mode=CAN_CAPTURE vehicle=NS125 ABS 2025
     timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok
     1205,00000120,0,0,8,0102030405060708,1,1
     ...
     === END PULSESHIFT PAGE 1/3 (RECORDS 1-500 OF 1450) [Next: 'DUMP NEXT' or 'DUMP ALL'] ===
     ```
   - Type `DUMP NEXT` (or simply `DUMP`) to export subsequent pages.
   - To export all records continuously in a single uninterrupted stream, type `DUMP ALL`.
   - To check export progress, type `DUMP STATUS`. To reset back to Page 1, type `DUMP CANCEL`.
8. **Save Output to File for Analysis**:
   - In Arduino IDE 2.3.10, click the **Copy Output** button located at the top-right corner of the Serial Monitor tab (or right-click in the monitor pane and choose **Copy**).
   - > [!IMPORTANT]
     > Do **not** use `Ctrl+A` in Arduino IDE 2.3.x, as it can fail to select lines outside the visible virtual scrollback window. Always use the dedicated **Copy Output** button.
   - Open Windows **Notepad**, paste the copied text (Ctrl+V), and save the file as `capture.txt` or `capture.csv`.
   - **Verify Host-Side Completeness and Integrity**:
     - > [!WARNING]
       > The ESP32 `expected=N exported=M` counters confirm only what the ESP32 successfully read from flash and transmitted over USB serial. They do **not** guarantee that Arduino Serial Monitor retained every line in its virtual scrollback buffer or that every line was copied to your clipboard.
     - **Check 1: Copied Page Boundaries**:
       For paginated exports (`DUMP` / `DUMP NEXT`), verify that every page from Page 1 to Page N is present in your saved file with contiguous record numbers:
       - Page 1: `=== BEGIN PULSESHIFT RAW CAN CAPTURE [PAGE 1/3: RECORDS 1-500 OF 1450] ===` ... `=== END PULSESHIFT PAGE 1/3 ===`
       - Page 2: `=== BEGIN PULSESHIFT PAGE 2/3: RECORDS 501-1000 OF 1450 ===` ... `=== END PULSESHIFT PAGE 2/3 ===`
       - Page 3: `=== BEGIN PULSESHIFT PAGE 3/3: RECORDS 1001-1450 OF 1450 ===` ... `=== END PULSESHIFT RAW CAN CAPTURE expected=1450 exported=1450 valid=1450 corrupt=0 totalBytes=29076 ===`
       Verify that no pages were skipped, duplicated, or cut off prematurely.
     - **Check 2: CSV Data Record Count**:
       Count the actual data rows in your saved text or CSV file. In Windows PowerShell:
       ```powershell
       (Get-Content capture.csv | Where-Object { $_ -match "^\d+," }).Count
       ```
       Confirm that this count matches the `expected` count printed in the final marker.
     - **Check 3: Record CRC Verification**:
       Confirm that `corrupt=0` in the final summary marker, and that every CSV row has `1` in the last column (`crc_ok`).

---

### Storage Safety Guarantees and Operational Limitations

#### Storage Safety Guarantees

- **Power-Failure-Safe Truncation**: When cleaning up partial trailing bytes at end-of-file, the firmware never deletes the original file before a replacement is committed. It creates `/temp_cap.bin`, checks all write return codes, verifies file size, record alignment, header fields, and record integrity, renames the original to `/backup_cap.bin`, and only commits the replacement when fully verified. If power is interrupted at any step, the original capture is automatically restored from backup on the next boot. Rollback rename results are verified, and the last recoverable copy is never deleted.
- **Damaged Capture Preservation**: If a capture file is damaged, has an invalid header, or contains fewer than 76 bytes, the firmware preserves it untouched (`DAMAGED_CAPTURE_PRESERVED`) and halts recording. It will never overwrite damaged files without an explicit `CLEAR YES` command.
- **Stop-on-Full Policy**: If internal flash storage reaches capacity (1,000,000 bytes / ~50,000 frames) or free flash drops below the 32 KB safety headroom, recording stops safely (`STORAGE_FULL`). Older records are never overwritten or circularly truncated.
- **Reboot Preservation**: When disconnecting the ESP32 from the motorcycle and plugging it into a PC, the prior session is automatically preserved (`FLASH_CAPTURE_PRESERVED`). Recording is locked to protect field data until exported or reinitialized with `CLEAR YES`.
- **Cumulative Multi-Page Integrity Accounting**: The final export marker tracks cumulative valid and corrupt counts across all pages, and reports expected vs exported record counts so any missing or skipped export page is detected immediately.

#### Operational Limitations

- **Host Serial Monitor Buffer Limits**: The ESP32 export summary reflects microcontroller transmission, but the ESP32 cannot detect if the PC operating system, USB-to-UART driver buffer, or Arduino Serial Monitor pruned earlier scrollback lines. For large captures, use paginated `DUMP` (500 records per page) and copy each page individually to avoid terminal buffer truncation.
- **Flash Storage Quota**: Capture capacity is limited to 1,000,000 bytes (~50,000 frames, or ~5.5 minutes at 150 frames/second). Once full, new frames are dropped while existing recordings are safely kept.
- **No Physical Flash Hardware Repair**: The firmware detects and quarantines logical corruption via CRC16 checksums and header validation, but cannot repair physically worn or degraded SPI flash sectors.
- **Serial Export Throughput**: At 115200 baud, dumping 50,000 frames takes approximately 90-120 seconds. Paginated export (500 records / ~10 KB per page) is provided so users can export and copy manageable blocks without overwhelming Serial Monitor scrollback buffers.

---

### LED Hardware Status in CAN Capture Mode

When operating in CAN Capture mode, the six WS2812B LEDs provide real-time hardware status feedback:

| LED | Status / Condition | Visual Indication |
|---|---|---|
| **LED 0** | Waiting for CAN Traffic | Gentle Yellow breathing pulse (0.5 Hz) |
| **LED 0** | Active CAN Reception | Solid Cyan/Green |
| **LED 0** | Fatal Controller Failure | Flashing Red alert (10 Hz) |
| **LED 1** | CAN Frame Arrival | Crisp Green heartbeat pulse on every packet |
| **LED 2** | Stage 1 (Key ON, Engine OFF) | Amber (LED 2 lit, LEDs 3-4 dark) |
| **LED 3** | Stage 2 (Engine Idling) | Amber (LEDs 2-3 lit, LED 4 dark) |
| **LED 4** | Stage 3 (Gentle RPM Sweeps) | Amber (LEDs 2-4 lit) |
| **LEDs 2-4** | Recording Paused | Slow blinking Blue (1 Hz) across all stage LEDs |
| **LED 5** | LittleFS Storage Error | Flashing Magenta alert |
| **LED 5** | Storage Full / Preserved | Flashing Amber warning |
| **LED 5** | Storage Normal | Dark / Off |
| **All 6** | Stage Transition Feedback | 200ms white confirmation flash |

---

## PC USB Connection Safety and Power Isolation

When testing on an operating motorcycle with a PC connected via USB:

1. **Prevent Ground Loops**: Use a **laptop running on its internal battery** (unplugged from AC mains). Connecting an AC-mains-earthed desktop computer to a vehicle 12V battery system creates hazardous ground loop currents through the USB shield that can destroy the PC motherboard, ESP32, or motorcycle ECU.
2. **Isolate 5V Power Rails**:
   - The ESP32 and SN65HVD230 transceiver are powered directly by the PC USB 5V rail.
   - The WS2812B LED strip is powered by the vehicle 5V buck converter (connected to the strip +5V terminal).
   - **DO NOT connect the vehicle 5V buck converter output to the ESP32 VIN pin while USB is connected to a PC.**
   - All ground lines (ESP32 GND, PC USB ground, LED strip GND, buck converter GND, motorcycle chassis GND) must be securely bonded together.

---

## Serial Communication & Commands (115200 baud)

## Serial Communication & Commands (115200 baud)

PulseShift features an interactive serial console operating at **115200 baud** (newline: `Both NL & CR` or `Newline` in Arduino Serial Monitor).

### Serial Monitor Command Reference

| Command | Syntax & Parameters | Description & Operational Bounds | Example Console Output / Usage |
|---|---|---|---|
| `HELP` | `HELP` | Displays the complete list of available commands and active syntax | Lists all 22 commands with usage tips |
| `STATUS` | `STATUS` | Comprehensive system health snapshot: CAN bus state, controller stats, LittleFS storage, and candidate monitor | `[STATUS] Bus: OK (500000 bps) | RX: 22542 | Drops: 0 | Flash: 450 KB free` |
| `LOGS` | `LOGS` | Dumps persistent diagnostic event log stored in LittleFS (`/diag_events.log`) | `[LOGS] Total events: 4 ...` |
| `RPM STATUS` | `RPM STATUS` | Candidate 0x301 engine-speed snapshot: raw byte 0, provisional estimated RPM, freshness, range, and calibration status | `[RPM] Candidate: 0x301 Byte[0] | Raw: 37 | Est RPM: 1480 | Fresh: YES | Calibration: UNVERIFIED` |
| `RPM MONITOR ON` | `RPM MONITOR ON` | Enables rate-limited (10 Hz) live streaming of candidate engine speed | `CAND,12450,0x301,37,1480,1` |
| `RPM MONITOR OFF` | `RPM MONITOR OFF` | Disables live candidate engine speed streaming | `[RPM] Monitor stream disabled` |
| `RPM MONITOR` | `RPM MONITOR` | Prints a single instantaneous candidate telemetry line | `CAND,12500,0x301,37,1480,1` |
| `RPM TEST <val>` | `RPM TEST <0..20000>` | Bench tests 6-LED shift lights with simulated RPM for 10s (e.g. `RPM TEST 4500`). Auto-reverts after 10s. | `[TEST] Simulating 4500 RPM on LEDs for 10s` (LEDs 1-4 illuminate) |
| `RPM TEST OFF` | `RPM TEST OFF` | Immediately cancels shift light bench test and restores field status LEDs | `[TEST] RPM test ended. Returning to status LEDs.` |
| `RPM MULTIPLIER <val>` | `RPM MULTIPLIER <0.01..1000.0>` | Updates candidate provisional multiplier. Rejects NaN, Inf, <=0, >1000. | `[RPM] Multiplier updated: 42.1 (Calibration remains UNVERIFIED)` |
| `STAGE <1\|2\|3>` | `STAGE 1`, `STAGE 2`, `STAGE 3` | Sets active field capture stage (1=KeyOn, 2=Idle, 3=GentleSweeps) | `[STAGE] Active stage set to 2 (ENGINE_IDLE)` |
| `1`, `2`, `3` | `1`, `2`, `3` | Single-character shortcuts for rapid field stage switching | Sets stage; triggers 200ms white LED confirmation flash |
| `CAPTURE STOP` | `CAPTURE STOP` | Pauses flash recording and flushes in-memory buffers to LittleFS | `[RECORDER] Capture paused. Pending records flushed.` |
| `CAPTURE RESUME` | `CAPTURE RESUME` | Resumes active flash recording | `[RECORDER] Capture resumed.` |
| `STREAM ON` | `STREAM ON` | Enables real-time raw CAN frame streaming over USB serial | `FRAME,1420,1,00000301,0,0,8,25000000000000C9` |
| `STREAM OFF` | `STREAM OFF` | Disables raw frame streaming (default mode: concise 2s summaries only) | `[STREAM] Raw frame streaming disabled` |
| `DUMP` | `DUMP` | Exports stored CAN frames in 500-record pages (resumes next page if active) | Outputs Page Header, CSV rows, Page Footer |
| `DUMP NEXT` | `DUMP NEXT` | Exports the next 500-record page of stored CAN frames | Advances cursor and outputs next 500 rows |
| `DUMP ALL` | `DUMP ALL` | Exports all stored records in a single uninterrupted stream | Exports all records with cumulative CRC validation |
| `DUMP STATUS` | `DUMP STATUS` | Displays current export cursor, records exported, and total page count | `[DUMP] Cursor: 1000 / 22542 (Page 2 of 46)` |
| `DUMP CANCEL` | `DUMP CANCEL` | Resets export cursor back to Page 1 | `[DUMP] Export cursor reset to Page 1` |
| `DUMP PAGE <n>` | `DUMP PAGE <1..total_pages>` | Exports specific page number directly (e.g. `DUMP PAGE 3`) | Outputs records for requested page |
| `CLEAR` | `CLEAR` | Requests instructions for erasing flash recording and diagnostic logs | `[SAFETY] Type 'CLEAR YES' to confirm format and start fresh session` |
| `CLEAR YES` | `CLEAR YES` | Confirms erase, formats LittleFS partition, and increments session ID | `[STORAGE] Flash formatted. Session ID: 2` |

### Example Serial Monitor Sessions

#### 1. Checking Live Candidate RPM (`RPM STATUS` and `RPM MONITOR ON`)
```text
> RPM STATUS
=== PulseShift Candidate RPM Monitor ===
Candidate CAN ID : 0x301
Byte Index       : 0
Raw Value        : 38
Estimated RPM    : 1520
Multiplier       : 40.0
Signal Fresh     : YES
Last Update      : 42 ms ago
Calibration      : UNVERIFIED
LED Shift Mode   : DISABLED
CAN TX           : SOFTWARE MUTED (Verify CTX->3V3 & GPIO17 air-gap)

--- Candidate Diagnostics ---
Frames Received  : 1840
Frames Rejected  : 0
Observed Range   : Min: 0 | Max: 240
Freshness Window : 250 ms (~10-12 frames at 45 Hz)

--- CAN Controller & Bus ---
Controller State : HEALTHY (RUNNING)
CAN Bitrate      : 500000 bps
Bus Traffic      : ACTIVE

> RPM MONITOR ON
[RPM] Rate-limited live candidate streaming ENABLED (10 Hz). Type 'RPM MONITOR OFF' to stop.
CAND,15420,0x301,38,1520,1
CAND,15520,0x301,37,1480,1
CAND,15620,0x301,39,1560,1
```

#### 2. Bench-Testing the 6-LED Shift Lights (`RPM TEST 5500`)
```text
> RPM TEST 5500
[TEST] 6-LED shift-light bench test active: 5500 RPM for 10s (Physical CAN shift lights remain DISABLED)
```

#### 3. Paginated CAN Capture Export (`DUMP`)
```text
> DUMP
=== BEGIN PULSESHIFT RAW CAN CAPTURE [PAGE 1/46: RECORDS 1-500 OF 22542] ===
[HEADER] magic=PULSECAP ver=1 recSize=20 sess=2 boot=1 bitrate=500000 startMs=1200 mode=CAN_CAPTURE vehicle=NS125 ABS 2025
timestamp_ms,can_id,extended,rtr,dlc,data,stage,crc_ok
1205,00000120,0,0,8,0102030405060708,1,1
1208,00000301,0,0,8,00000000000000C9,1,1
...
=== END PULSESHIFT PAGE 1/46 (RECORDS 1-500 OF 22542) [Next: 'DUMP NEXT' or 'DUMP ALL'] ===
```

### Machine-Readable Streaming Format

When live streaming is enabled via `STREAM ON`, frames are formatted as:

```text
FRAME,timestamp_ms,stage,can_id,extended,rtr,dlc,payload_hex
```

Example: `FRAME,10452,2,00000123,0,0,8,0102030405060708`

- `timestamp_ms`: Milliseconds since ESP32 boot.
- `stage`: 1 (KeyOn), 2 (Idle), 3 (GentleRPM).
- `can_id`: Standard 3-digit or Extended 8-digit hex arbitration ID.
- `extended`: 0 for 11-bit standard ID, 1 for 29-bit extended ID.
- `rtr`: 0 for data frame, 1 for remote transmission request.
- `dlc`: Data length code (0 to 8).
- `payload_hex`: Hexadecimal payload string.

Periodic telemetry lines (`STAT,timestamp,rx_total,accepted,missed,overrun,bus_errors,serial_dropped,rec,rec_dropped`), stage events (`STAGE,...`), and diagnostic alerts (`DIAG,timestamp,level,tag,message`) are streamed continuously.

---

### Optional Advanced Tooling (`tools/pulseshift_capture.py`)

For advanced desktop workflows or automated CI pipelines, `tools/pulseshift_capture.py` provides an optional Python script for capturing streamed frames directly to PC CSV files. **This tool is completely optional and is not required for normal PulseShift use.** The Arduino IDE workflow documented above is the primary, self-contained method.

---

## Configuration (`UserConfig.h`)

Edit `PulseShift/UserConfig.h` before uploading:

| Setting | Default | Description |
|---|---|---|
| `PULSESHIFT_OPERATING_MODE` | `CAN_CAPTURE` | Production mode: passive listen-only CAN acquisition |
| `PULSESHIFT_CAN_BITRATE` | `500000` | Manual CAN bitrate in bits/s (500000 bps default) |
| `PULSESHIFT_LED_BRIGHTNESS` | `255` | Master LED brightness (0-255). Full scale 255 default |
| `PULSESHIFT_LED_DATA_GPIO` | `27` | ESP32 GPIO connected to WS2812B DIN (via 330-470 ohm resistor) |
| `PULSESHIFT_LED_COUNT` | `6` | Number of addressable LEDs (1-32) |
| `PULSESHIFT_CAN_RX_GPIO` | `16` | ESP32 GPIO connected to SN65HVD230 Pin 4 (RXD) |
| `PULSESHIFT_CAN_TX_GPIO` | `17` | Reserved; disconnected from transceiver (air gap) |
| `PULSESHIFT_BOOT_BUTTON_GPIO`| `0` | Physical button for stage cycling |
| `PULSESHIFT_SERIAL_BAUD` | `115200` | USB Serial baud rate |

> [!NOTE]
> **Power Budget Note**: Six WS2812B LEDs at full scale white (brightness 255) draw ~360mA total. Combined with the ESP32 (~90-120mA) and transceiver (~15mA), worst-case power demand is ~465mA (peak ~600mA) on the 5V rail. A regulated 5V power supply rated for at least 1A is recommended.

## RPM Calibration Procedure and Signal Verification

From confirmed motorcycle testing (Session 2, 22,542 frames at 500,000 bps):
- Candidate arbitration ID: `0x301` (standard 11-bit CAN identifier, DLC=8, trailing byte `0xC9`).
- Candidate signal: payload byte 0 (`data[0]`).
- Observed behavior:
  - Stage 1 (Key ON, Engine OFF): Byte 0 is constant 0.
  - Stage 2 (Idling): Starts with 0-55 transient, settles into raw 35-40 (median 37).
  - Stage 3 (Gentle RPM Sweep): Sweeps smoothly up to raw 240, falls back to 35-40, then drops to 0 when engine stops.
- Provisional conversion hypothesis: `RPM = raw * 40.0` (Raw 35 -> 1400 RPM, Raw 240 -> 9600 RPM).

> [!IMPORTANT]
> **Calibration Status is UNVERIFIED**:
> Nominal owner manual idle specification (1400 +/- 100 RPM) is manufacturer guidance, not a measured RPM reading from this specific motorcycle.
> `RPM_MAPPED=NO` and `Calibration: UNVERIFIED` remain in effect until verified against an independent physical reference.

### Two-Point Calibration Procedure

To independently calibrate the candidate signal without risky road tests:
1. Setup Hardware:
   - Ensure motorcycle is stationary, in neutral, on center stand, with adequate ventilation.
   - Connect ESP32 in passive listen-only mode using a battery-powered laptop (unplugged from AC mains).
2. Connect Reference Tachometer:
   - Connect an independent RPM measurement instrument (an OBD-II scan tool displaying live engine speed or an optical/inductive workshop tachometer).
3. Sample Point 1 (Stable Idle):
   - With engine idling at operating temperature, observe `RPM STATUS` or `RPM MONITOR ON` on the ESP32.
   - Record simultaneous values: `(raw_idle, ref_rpm_idle)`, e.g. `(37, 1450)`.
4. Sample Point 2 (Moderate Stationary Hold):
   - Gently hold throttle stationary at approximately 3000 to 4000 RPM in neutral.
   - Record simultaneous values: `(raw_hold, ref_rpm_hold)`, e.g. `(75, 3050)`.
5. Compute Two-Point Linear Model:
   - Calculate slope (m) and intercept (c) from the two verified sample pairs (x1, y1) and (x2, y2):
     $$\Delta x = x_2 - x_1, \quad \Delta y = y_2 - y_1$$
     $$m = \frac{\Delta y}{\Delta x}$$
     $$c = y_1 - (m \times x_1)$$
   - Convert to integer rational fraction constants for fixed-point integer calculation in ESP32 firmware:
     $$\text{scaleNumerator} = y_2 - y_1$$
     $$\text{scaleDenominator} = x_2 - x_1$$
     $$\text{offsetNumerator} = c$$
   - Example calculation using hypothetical pairs (37, 1450) and (75, 3050):
     $$\Delta x = 75 - 37 = 38$$
     $$\Delta y = 3050 - 1450 = 1600$$
     $$m = \frac{1600}{38} \approx 42.105$$
     $$c = 1450 - (42.105 \times 37) \approx -108$$
6. Update Vehicle Profile:
   - Once verified with independent physical test data across multiple sessions, enter the calibrated rational constants into `PulseShift/src/vehicle_profile.cpp` and set `RPM_MAPPED = YES` in the profile definition.

---

## Safety notes

- **GPIO17 Physical Air Gap**: ESP32 GPIO17 (TWAI TX) must remain physically isolated (air gap with no wire inserted) from the transceiver at all times during motorcycle use. Software listen-only mode alone cannot guarantee electrical isolation against bootloader glitches or firmware misconfiguration, and microcontroller software cannot detect whether a physical wire is inserted.
- **Forced Recessive Clamping**: The SN65HVD230 transceiver breakout's `CTX` pin (Pin 1 / TXD) connects directly to the ESP32 3V3 rail (NO 10k resistor in the verified working configuration). This clamps the driver permanently into the recessive (idle) state.
- **Listen-Only TWAI Operation**: PulseShift never transmits CAN frames or ACK bits. The TWAI controller is initialized strictly with `TWAI_MODE_LISTEN_ONLY`.
- **Termination Resistor Verification**: Check resistance across unpowered transceiver CANH-CANL terminals with a multimeter. Desolder SMD resistor R2 only if ~120 ohms is measured, avoiding loading down the motorcycle's factory 60 ohm bus to 40 ohms. If the module measures open circuit, leave it unmodified.
- **Safe Power Sequencing**: Do not change wiring while powered. Switch on the external 5V power supply before connecting USB.
- **Prevent Ground Loops on Vehicle**: When connecting via USB to a motorcycle, always use a laptop running on its internal battery (unplugged from AC mains) to prevent dangerous ground loops between building mains and vehicle chassis.
- **Optional Level Shifter**: Some 5V WS2812B strips require a 74AHCT125 level shifter for reliable 3.3V signal recognition over longer automotive wiring harness runs.

## Directory structure

```text
PulseShift-Firmware/
├── PulseShift/           ← Open PulseShift.ino here in Arduino IDE
│   ├── PulseShift.ino    ← Main sketch entry point (setup and loop)
│   ├── UserConfig.h      ← Edit this to change settings
│   └── src/              ← Implementation compiled recursively (no tab clutter)
│       ├── config.h          ← Internal config
│       ├── can_transport.*   ← TWAI driver wrapper (listen-only)
│       ├── can_recorder.*    ← Raw CAN capture to LittleFS
│       ├── shift_light.*     ← WS2812B LED control
│       ├── rpm_candidate.*   ← Live 0x301 candidate engine-speed monitor
│       ├── rpm_decoder.*     ← CAN RPM decoding framework
│       ├── diag_logger.*     ← Automatic error logging
│       ├── storage_manager.* ← LittleFS and NVS management (safe formatting)
│       ├── dashboard.*       ← OLED display (optional)
│       └── ...               ← Supporting headers and sources
├── assets/readme/        ← Clean SVG circuit and wiring diagrams
│   ├── generate.py       ← Standard-library script generating all SVGs
│   ├── pulseshift-wiring.svg
│   ├── esp32-pinout.svg
│   ├── ws2812b-circuit.svg
│   └── sn65hvd230-circuit.svg
├── test/                 ← Host-side C++ and Python verification tests
├── tools/                ← Optional capture scripts (pulseshift_capture.py, can_investigate.py)
├── docs/                 ← Hardware and CAN investigation notes
└── README.md
```

## Copyright

Copyright (c) 2026 Aritra Saha. All rights reserved. See [COPYRIGHT](COPYRIGHT).
