# PulseShift Hardware Reference

Comprehensive hardware specification, circuit diagrams, and electrical guidelines for the PulseShift ESP32 shift-light and CAN diagnostic logger.

Target vehicle context: **Bajaj Pulsar NS125 (2025 ABS model)**. The physical diagnostic connector cavity layout, CAN bus availability on the connector, bitrate, and RPM CAN ID remain unverified until physical packet capture is performed.

---

## 1. Complete System Wiring

The full PulseShift hardware system consists of four functional blocks:
1. **Power Subsystem**: Regulated 5V buck converter from motorcycle 12V switched ignition.
2. **Core Microcontroller**: DOIT ESP32 DevKit V1 (ESP-WROOM-32).
3. **Passive CAN Interface**: SN65HVD230 3.3V transceiver with physical transmission inhibitors.
4. **Shift Light Display**: Six WS2812B RGB LEDs with series resistor, bulk capacitor, and optional level shifter.

![PulseShift Complete Wiring](../assets/readme/pulseshift-wiring.svg)

---

## 2. ESP32 DevKit V1 Pinout

The firmware targets the standard 30-pin DOIT ESP32 DevKit V1 board.

![ESP32 DevKit V1 Pinout](../assets/readme/esp32-pinout.svg)

### Pin Mapping Table

| Board Pin | ESP32 GPIO | Net / Function | Connection Destination | Electrical Rules |
|---|---|---|---|---|
| D27 | GPIO27 | WS2812B Data | LED strip DIN (via 330-470 ohm resistor) | 3.3V logic (800 kHz NRZ); keep lead short |
| RX2 | GPIO16 | TWAI RX | SN65HVD230 Pin 4 (RXD / R) | 3.3V digital logic input from transceiver |
| TX2 | GPIO17 | TWAI TX | **PHYSICALLY DISCONNECTED** | Air gap; DO NOT connect to transceiver TXD |
| 3V3 | 3.3V Rail | Logic VCC | SN65HVD230 Pin 3 (VCC), OLED VCC | Regulated 3.3V output from onboard LDO |
| VIN | 5V Rail | Power Input | Regulated 5V output from DC-DC converter | 5.0V input; do not exceed 5.5V |
| GND | Ground | Common GND | Power GND, LED GND, Transceiver GND, Bike GND | All system grounds must be securely bonded |
| D21 | GPIO21 | OLED SDA | Optional SSD1306 OLED SDA | 3.3V I2C data (disabled by default) |
| D22 | GPIO22 | OLED SCL | Optional SSD1306 OLED SCL | 3.3V I2C clock (disabled by default) |

---

## 3. WS2812B Shift Light Circuit

PulseShift drives six WS2812B addressable LEDs in series.

![WS2812B LED Circuit](../assets/readme/ws2812b-circuit.svg)

### Direct Drive vs Level Shifter

1. **Direct-Drive (Bench & Short Wire Runs)**:
   - ESP32 GPIO27 outputs 3.3V logic signals.
   - A **330 to 470 ohm series resistor** is installed as close as possible to the first LED's DIN pin. This resistor damps transmission line reflections, limits inrush current into the input protection diodes, and prevents ringing.
   - Most WS2812B strips accept 3.3V logic when powered from 5V, provided wire length is kept under 20 cm.

2. **Optional 74AHCT125 Level Shifter (Recommended for Vehicle Installations)**:
   - The WS2812B datasheet specifies minimum input high voltage as `Vih(min) = 0.7 * VDD = 3.5V` when powered at 5.0V.
   - Over longer automotive wiring harnesses or under temperature variations, 3.3V logic may cause flickering or dropped data frames.
   - An SN74AHCT125 powered at 5V reliably translates 3.3V logic to true 5.0V TTL levels (`Vih = 2.0V`, `Voh >= 3.8V`).
   - Pin 1 (1/OE) is grounded (active LOW buffer enable). Pin 2 (1A) connects to GPIO27 with an optional 10k ohm pull-down to ground. Pin 3 (1Y) outputs 5V data through the 330 ohm resistor to DIN. Pin 14 is decoupled to Pin 7 (GND) with a 100nF ceramic capacitor. Unused inputs are tied to 5V or GND.

### Power & Polarity Rules

- A **1000 uF / 10V (or 16V) electrolytic capacitor** must be installed directly across the LED strip +5V and GND power terminals. This prevents voltage dips during sudden current surges when all LEDs illuminate simultaneously.
- Always observe capacitor polarity: the positive (+) lead connects to +5V, and the negative striped (-) lead connects to GND.
- **Never connect external 5V to the ESP32 3.3V pin.**
- **Leave DOUT on the 6th LED completely unconnected.**

![LED Data Direction](../assets/readme/led-direction.svg)

---

## 4. SN65HVD230 Passive CAN Circuit

PulseShift is strictly a passive CAN listener. It never transmits frames, acknowledges packets, or injects traffic onto the motorcycle's bus.

![SN65HVD230 CAN Circuit](../assets/readme/sn65hvd230-circuit.svg)

### Connection Requirements

| SN65HVD230 Pin | Label | Connection | Purpose & Safety Rule |
|---|---|---|---|
| Pin 1 | D / TXD | 3.3V (via 10k ohm pull-up) | **Forced recessive**. The transmitter cannot drive dominant bits onto the vehicle bus. |
| Pin 2 | GND | Common GND | Logic ground and reference for differential signals. |
| Pin 3 | VCC | ESP32 3V3 Pin | Powered strictly from 3.3V. **Never connect to 5V or 12V.** |
| Pin 4 | R / RXD | ESP32 GPIO16 | Drives 3.3V digital logic (reconstructed CAN bits) into ESP32 TWAI RX. |
| Pin 5 | Vref | Not Connected (NC) | Nominal VCC/2 reference output. Leave open. |
| Pin 6 | CANL | Motorcycle CAN-L | Low-voltage differential bus line. |
| Pin 7 | CANH | Motorcycle CAN-H | High-voltage differential bus line. |
| Pin 8 | Rs | ESP32 GND (Ground) | **Normal high-speed receiver mode**. Enables full 1 Mbps bandwidth. |

### Transceiver Mode: Why Rs Connects to GND

The Texas Instruments SN65HVD230 datasheet specifies the behavior of Pin 8 (Rs):
- **Rs to GND (< 0.33 * VCC)**: Normal high-speed mode. The receiver and driver operate with standard propagation delays, supporting data rates up to 1 Mbps.
- **Rs through resistor to GND (10k to 100k ohm)**: Slope control mode. Limits slew rate to reduce EMI.
- **Rs to VCC (> 0.75 * VCC)**: Low-power standby mode. The driver is disabled, but the receiver enters a low-power listening state with high propagation delay, unsuitable for high-speed vehicle networks (e.g. 500 kbps).

**Important**: Connecting Rs to GND selects normal high-speed receiver operation. It does NOT enable transmission by itself. Passive listen-only safety is achieved through three independent hardware and software safeguards:
1. **Physical Air Gap**: ESP32 GPIO17 is physically disconnected from the transceiver TXD.
2. **Forced Recessive Bias**: Transceiver Pin 1 (TXD) is held permanently HIGH with a 3.3V pull-up resistor. On CAN transceivers, a HIGH TXD input forces the differential driver into recessive (idle) state.
3. **TWAI Listen-Only Mode**: The ESP32 TWAI peripheral is initialized with `TWAI_MODE_LISTEN_ONLY`, which suppresses dominant ACK bits and error frames.

### Critical: 120 Ohm Termination Resistor Removal

Standard ISO 11898-2 CAN buses are terminated at both physical ends with 120 ohm resistors, resulting in a nominal bus impedance of 60 ohms:
$$\frac{1}{R_{total}} = \frac{1}{120\,\Omega} + \frac{1}{120\,\Omega} = \frac{1}{60\,\Omega} \implies R_{total} = 60\,\Omega$$

Commercial SN65HVD230 breakout boards typically include an onboard 120 ohm SMD resistor (often labeled `R2` or `1200`) soldered directly across CANH and CANL.

Because PulseShift connects as a **stub tap** to an existing, already terminated motorcycle bus, leaving this onboard resistor in place adds a third 120 ohm resistor in parallel:
$$\frac{1}{R_{total}} = \frac{1}{120\,\Omega} + \frac{1}{120\,\Omega} + \frac{1}{120\,\Omega} = \frac{1}{40\,\Omega} \implies R_{total} = 40\,\Omega$$

This drops the total bus resistance to 40 ohms, increasing transceiver current draw, reducing differential voltage swing, and triggering bus errors in the motorcycle ECU and ABS controllers.

**Mandatory action**: Before connecting the breakout board to the motorcycle, measure the resistance between CANH and CANL using a multimeter with the module unpowered. If it reads approximately 120 ohms, locate and desolder resistor R2 (or cut its PCB jumper trace). Resistance between CANH and CANL on the disconnected module must read open circuit (infinity).

---

## 5. Power Architecture & Automotive Protection

![Power Safety](../assets/readme/power-safety.svg)

### Motorcycle Power Conditioning

A motorcycle electrical system experiences substantial voltage transients, including alternator ripple, inductive spikes from the ignition coil, and load dump surges (up to 40V+ when disconnecting battery under charge).

1. **Inline Fuse**: A 1A fast-blow fuse must be installed immediately downstream of the switched ignition feed (+12V switched).
2. **DC-DC Step-Down Buck Converter**: A high-efficiency automotive-rated buck converter steps +12V down to a stable 5.0V (minimum 2A capacity).
3. **Common Ground**: Motorcycle chassis/battery ground, buck converter ground, ESP32 GND, LED GND, and SN65HVD230 GND must all be bonded to a single common ground plane.
4. **Absolute Prohibition**: NEVER connect raw +12V motorcycle power directly to the ESP32 (VIN, 5V, or 3V3 pins) or to the SN65HVD230.

### Optional Automotive Transient Protection

For long-term reliability on an operating motorcycle:
- **CAN Bus TVS Diodes**: A bidirectional transient voltage suppressor diode array (such as Nexperia PESD1CAN or Littlefuse AQ24CAN) placed between CANH to GND and CANL to GND suppresses electrostatic discharge (ESD) and inductive spikes.
- **Power Rail TVS**: A unidirectional 18V TVS diode (e.g. SMBJ15A) across the DC-DC converter 12V input protects against vehicle voltage surges.

---

## 6. How the Circuit Works: Signal Path & Concepts

The signal flow from engine crankshaft rotation to visual shift light illumination proceeds as follows:

```text
[Motorcycle ECU]
       |
       v (Differential CAN electrical voltages: CAN-H ~3.5V, CAN-L ~1.5V)
[CAN Bus Lines]
       |
       v (Differential bus tap, no extra 120 ohm termination)
[SN65HVD230 Transceiver]
       |
       v (Single-ended 3.3V digital bitstream into GPIO16)
[ESP32 TWAI Controller]
       |
       v (Parsed CAN frames: 11/29-bit ID + 8 payload bytes)
[PulseShift RPM Decoder]
       |
       v (Decoded RPM integer, e.g. 4500 RPM)
[Shift Light Logic Engine]
       |
       v (Active pixel colors, progressive fill, shift alert)
[ESP32 GPIO27]
       |
       v (800 kHz precision timed NRZ pulses through 330 ohm resistor)
[WS2812B LED Strip (Pixels 1 to 6)]
```

### Distinguishing Key CAN & Engine Concepts

To assemble and troubleshoot this system effectively, understand the distinct layers:

1. **CAN Electrical Signals**:
   Physical voltages on the twisted-pair wire. A dominant bit is represented when CAN-H is pulled up to ~3.5V and CAN-L is pulled down to ~1.5V (differential voltage ~2.0V). A recessive bit occurs when both lines float at approximately 2.5V (differential voltage ~0.0V).

2. **CAN Frame Identifiers (CAN ID)**:
   A standard 11-bit (e.g. `0x1F0`) or extended 29-bit header at the start of each packet that defines the message identity and priority. For example, one ID might broadcast engine status, while another broadcasts wheel speeds. On the Pulsar NS125, the exact CAN ID carrying engine RPM is currently unknown.

3. **CAN Payload Bytes**:
   Up to 8 data bytes (`Byte 0` through `Byte 7`) contained within each frame. The RPM data is typically encoded into two contiguous bytes (16 bits), stored either in big-endian (MSB first) or little-endian (LSB first) order.

4. **Engine RPM Values**:
   The calculated rotational speed of the engine crankshaft in revolutions per minute, derived by converting the raw 16-bit integer payload using a mathematical scaling formula:
   $$\text{Engine RPM} = (\text{Raw Integer} \times \text{Multiplier}) + \text{Offset}$$
   For instance, on some systems `RPM = Raw * 0.25`, while on others `RPM = Raw * 1.0`.

5. **Shift-Light Thresholds**:
   Firmware configuration settings in `UserConfig.h` that determine when LEDs light up:
   - `PULSESHIFT_DEMO_START_RPM` (default: 0 RPM): Start of progression.
   - 1000 RPM increments: Each additional LED illuminates as engine speed climbs.
   - `PULSESHIFT_DEMO_SHIFT_RPM` (default: 6000 RPM): Threshold where all six LEDs flash red in unison.

---

## 7. Pre-Connection Physical Verification Checklist

Before connecting PulseShift to any motorcycle:

1. [ ] **Measure 12V Regulator Output**: Verify with a multimeter that the buck converter outputs strictly 5.0V (+-0.2V) before connecting ESP32 or LEDs.
2. [ ] **Check 120 Ohm Bus Resistor**: Verify with a multimeter that CANH-to-CANL resistance on the unpowered SN65HVD230 breakout is open circuit (infinity). If 120 ohms, desolder R2.
3. [ ] **Verify GPIO17 Isolation**: Confirm that ESP32 GPIO17 is disconnected with no wire inserted in its header socket.
4. [ ] **Verify TXD Pull-Up**: Check that SN65HVD230 Pin 1 (TXD) is tied to 3.3V through a 10k ohm resistor.
5. [ ] **Verify Rs Pin**: Check that SN65HVD230 Pin 8 (Rs) is tied directly to GND.
6. [ ] **Verify Common Ground**: Measure 0.0 ohms between ESP32 GND, power supply negative, LED GND, and transceiver GND.
7. [ ] **Verify LED Capacitor**: Confirm 1000 uF capacitor polarity (+ lead to 5V, - lead to GND).
8. [ ] **Identify Bike Pins**: Probe the motorcycle diagnostic connector with a multimeter (key on, engine off) to locate CAN-H (~2.5V to GND) and CAN-L (~2.5V to GND). Do not rely solely on wire color.
