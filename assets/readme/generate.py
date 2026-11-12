#!/usr/bin/env python3
# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Rebuild all PulseShift README and hardware documentation SVGs:
python assets/readme/generate.py (or uv run python assets/readme/generate.py).

Standard-library only. SVGs are scalable, legible, connection diagrams.
Inspect actual component markings. Colors supplement explicit net labels.
"""
from pathlib import Path
from html import escape
import xml.etree.ElementTree as ET

OUT = Path(__file__).resolve().parent

# Color palette: high contrast, accessible in both GitHub light and dark modes
INK = '#142b3b'
MUTED = '#425c6d'
EDGE = '#c5d4dd'
BOARD_BG = '#1e293b'
BOARD_EDGE = '#334155'
CARD_BG = '#f8fafc'
BLUE = '#0969aa'
CYAN = '#0284c7'
RED = '#ba2e34'
DARK_RED = '#991b1b'
GROUND = '#334155'
PURPLE = '#7c3aed'
GREEN = '#16a34a'
YELLOW = '#ca8a04'
ORANGE = '#ea580c'
OFF = '#41554d'
GOLD = '#d97706'


class Svg:
    def __init__(self, name, width, height, title, description):
        self.name = name
        self.width = width
        self.height = height
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}" role="img" aria-labelledby="title desc">',
            f'<title id="title">{escape("PulseShift: " + title)}</title>',
            f'<desc id="desc">{escape(description)}</desc>',
            '<style>',
            'text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}',
            'path,line,rect,circle{stroke-linecap:round;stroke-linejoin:round}',
            '.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace}',
            '</style>',
            f'<rect width="{width}" height="{height}" fill="#ffffff"/>'
        ]
        self.text(28, 44, "PulseShift: " + title, 24, bold=True)

    def text(self, x, y, s, size=24, color=INK, bold=False, anchor='start', mono=False):
        cls = ' class="mono"' if mono else ''
        weight = 700 if bold else 400
        self.parts.append(
            f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" '
            f'font-weight="{weight}" text-anchor="{anchor}"{cls}>{escape(s)}</text>'
        )

    def rect(self, x, y, w, h, fill=CARD_BG, stroke=EDGE, r=10, stroke_width=2, dash=None):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ''
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{r}" '
            f'fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width}"{dash_attr}/>'
        )

    def circle(self, cx, cy, r, fill=CARD_BG, stroke=EDGE, stroke_width=2):
        self.parts.append(
            f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{stroke_width}"/>'
        )

    def line(self, points, color=GROUND, width=4, dash=None):
        dash_attr = f' stroke-dasharray="{dash}"' if dash else ''
        d = 'M ' + ' L '.join(f'{x},{y}' for x,y in points)
        self.parts.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}"{dash_attr}/>')

    def dot(self, x, y, color=GROUND, r=5):
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{color}"/>')

    def arrow(self, x, y, direction='right', color=BLUE):
        pts = {
            'right': [(x - 12, y - 8), (x, y), (x - 12, y + 8)],
            'left': [(x + 12, y - 8), (x, y), (x + 12, y + 8)],
            'down': [(x - 8, y - 12), (x, y), (x + 8, y - 12)],
            'up': [(x - 8, y + 12), (x, y), (x + 8, y + 12)]
        }[direction]
        self.line(pts, color, width=3)

    def ground(self, x, y):
        self.line([(x, y), (x, y + 12)], color=GROUND, width=3)
        for dy, half in [(12, 16), (19, 10), (26, 4)]:
            self.line([(x - half, y + dy), (x + half, y + dy)], color=GROUND, width=3)

    def resistor(self, x, y, label="330Ω", vertical=False):
        if vertical:
            self.line([(x, y), (x, y + 12)], color=GROUND, width=3)
            self.rect(x - 12, y + 12, 24, 46, fill='#fef3c7', stroke=GROUND, r=3, stroke_width=2)
            self.line([(x, y + 58), (x, y + 70)], color=GROUND, width=3)
            if label:
                self.text(x + 18, y + 42, label, 16, INK, bold=True)
        else:
            self.line([(x, y), (x + 15, y)], color=GROUND, width=3)
            self.rect(x + 15, y - 12, 54, 24, fill='#fef3c7', stroke=GROUND, r=3, stroke_width=2)
            self.line([(x + 69, y), (x + 84, y)], color=GROUND, width=3)
            if label:
                self.text(x + 42, y - 18, label, 16, INK, bold=True, anchor='middle')

    def strip(self, x, y, colors, step=67, size=45):
        self.rect(x - 12, y - 12, step * 5 + size + 24, size + 24, fill='#244238', stroke='#244238', r=8)
        for i, c in enumerate(colors):
            self.rect(x + i * step, y, size, size, fill=c, stroke='#9eb7aa', r=6)

    def cap(self, x, y, bulk=False, label="1000µF"):
        self.line([(x, y), (x, y + 20)], color=RED, width=3)
        self.line([(x - 18, y + 20), (x + 18, y + 20)], color=GROUND, width=3)
        self.line([(x - 18, y + 30), (x + 18, y + 30)], color=GROUND, width=6 if bulk else 3)
        self.line([(x, y + 30), (x, y + 50)], color=GROUND, width=3)
        if bulk:
            self.text(x - 30, y + 18, '+', 20, RED, bold=True)
            self.text(x + 22, y + 38, '-', 22, GROUND, bold=True)
        if label:
            self.text(x + 26, y + 25, label, 14, INK, bold=True)

    def save(self):
        content = '\n'.join(self.parts + ['</svg>']) + '\n'
        # Validate XML structure
        ET.fromstring(content)
        # Verify no em-dash or en-dash characters are present
        assert '\u2014' not in content, f"Em-dash detected in {self.name}"
        assert '\u2013' not in content, f"En-dash detected in {self.name}"
        target = OUT / self.name
        target.write_text(content, encoding='utf-8')
        print(f"Generated and validated {self.name}")


# ---------------------------------------------------------------------------
# 1. ESP32 DevKit V1 Pinout Diagram
# ---------------------------------------------------------------------------
def make_esp32_pinout():
    s = Svg(
        'esp32-pinout.svg', 760, 940,
        'ESP32 DevKit V1 Pinout',
        'Physical 30-pin board mapping showing active pins, power rails, and safety-isolated GPIO17.'
    )
    s.text(28, 80, 'DOIT DevKit V1 (30 pins): PulseShift pin assignments and functions', 20, MUTED)

    # Center ESP32 board representation
    bx, by, bw, bh = 240, 110, 280, 720
    s.rect(bx, by, bw, bh, fill='#1e293b', stroke='#334155', r=14, stroke_width=3)

    # Antenna block at top
    s.rect(bx + 40, by + 10, 200, 26, fill='#0f172a', stroke='#334155', r=4)
    s.text(bx + 140, by + 28, 'PCB ANTENNA', 12, '#64748b', bold=True, anchor='middle')

    # ESP-WROOM-32 metal RF shield
    s.rect(bx + 35, by + 46, 210, 190, fill='#94a3b8', stroke='#64748b', r=8, stroke_width=2)
    s.text(bx + 140, by + 120, 'ESP-WROOM-32', 20, '#1e293b', bold=True, anchor='middle')
    s.text(bx + 140, by + 150, '32-bit Dual Core / 240MHz', 14, '#334155', anchor='middle')
    s.text(bx + 140, by + 175, 'TWAI / CAN Controller Inside', 13, '#1e293b', bold=True, anchor='middle')

    # Status LEDs and Tactile Buttons
    s.circle(bx + 60, by + 655, 6, fill='#ef4444', stroke='#991b1b')
    s.text(bx + 75, by + 660, 'PWR', 11, '#94a3b8')
    s.circle(bx + 220, by + 655, 6, fill='#3b82f6', stroke='#1d4ed8')
    s.text(bx + 185, by + 660, 'D2', 11, '#94a3b8')

    s.rect(bx + 25, by + 675, 45, 26, fill='#334155', stroke='#475569', r=4)
    s.text(bx + 47, by + 693, 'EN', 12, '#f8fafc', bold=True, anchor='middle')
    s.rect(bx + 210, by + 675, 45, 26, fill='#334155', stroke='#475569', r=4)
    s.text(bx + 232, by + 693, 'BOOT', 12, '#f8fafc', bold=True, anchor='middle')

    # Micro-USB socket at bottom
    s.rect(bx + 95, by + 695, 90, 32, fill='#cbd5e1', stroke='#64748b', r=4, stroke_width=2)
    s.text(bx + 140, by + 716, 'Micro-USB', 13, '#1e293b', bold=True, anchor='middle')

    # 15 Pins on left and 15 Pins on right
    left_pins = [
        ("EN", "EN", False, ""),
        ("VP", "GPIO36", False, ""),
        ("VN", "GPIO39", False, ""),
        ("D34", "GPIO34", False, ""),
        ("D35", "GPIO35", False, ""),
        ("D32", "GPIO32", False, ""),
        ("D33", "GPIO33", False, ""),
        ("D25", "GPIO25", False, ""),
        ("D26", "GPIO26", False, ""),
        ("D27", "GPIO27", True, "LED"),     # index 9
        ("D14", "GPIO14", False, ""),
        ("D12", "GPIO12", False, ""),
        ("D13", "GPIO13", False, ""),
        ("GND", "GND", True, "GND"),         # index 13
        ("VIN", "VIN / 5V", True, "5V")      # index 14
    ]

    right_pins = [
        ("D23", "GPIO23", False, ""),
        ("D22", "GPIO22", True, "SCL"),     # index 1: Optional OLED SCL
        ("TX0", "GPIO1", False, "UART0"),
        ("RX0", "GPIO3", False, "UART0"),
        ("D21", "GPIO21", True, "SDA"),     # index 4: Optional OLED SDA
        ("D19", "GPIO19", False, ""),
        ("D18", "GPIO18", False, ""),
        ("D5", "GPIO5", False, ""),
        ("TX2", "GPIO17", True, "DISC"),    # index 8: Reserved TWAI TX - PHYSICALLY DISCONNECTED
        ("RX2", "GPIO16", True, "RXD"),     # index 9: TWAI RX from SN65HVD230
        ("D4", "GPIO4", False, ""),
        ("D2", "GPIO2", False, ""),
        ("D15", "GPIO15", False, ""),
        ("GND", "GND", True, "GND"),        # index 13
        ("3V3", "3V3", True, "3V3")         # index 14: 3.3V Out to transceiver VCC
    ]

    start_y = by + 250
    spacing = 27

    # Render left pins
    for i, (label, gpio, active, role) in enumerate(left_pins):
        py = start_y + i * spacing
        # Pin hole
        fill_col = '#f8fafc'
        stroke_col = '#64748b'
        if role == 'LED':
            fill_col = '#93c5fd'; stroke_col = BLUE
        elif role == 'GND':
            fill_col = '#94a3b8'; stroke_col = GROUND
        elif role == '5V':
            fill_col = '#fca5a5'; stroke_col = RED
        s.rect(bx + 6, py - 9, 20, 18, fill=fill_col, stroke=stroke_col, r=3, stroke_width=1.5)
        s.text(bx + 34, py + 5, label, 13, '#f8fafc', bold=active)

    # Render right pins
    for i, (label, gpio, active, role) in enumerate(right_pins):
        py = start_y + i * spacing
        fill_col = '#f8fafc'
        stroke_col = '#64748b'
        if role == 'RXD':
            fill_col = '#d8b4fe'; stroke_col = PURPLE
        elif role == 'DISC':
            fill_col = '#fecaca'; stroke_col = RED
        elif role == '3V3':
            fill_col = '#fed7aa'; stroke_col = ORANGE
        elif role == 'GND':
            fill_col = '#94a3b8'; stroke_col = GROUND
        elif role in ('SDA', 'SCL'):
            fill_col = '#fef08a'; stroke_col = YELLOW
        s.rect(bx + bw - 26, py - 9, 20, 18, fill=fill_col, stroke=stroke_col, r=3, stroke_width=1.5)
        s.text(bx + bw - 34, py + 5, label, 13, '#f8fafc', bold=active, anchor='end')

    # Left Callout Boxes
    # 1. GPIO27 WS2812B Data
    ly_d27 = start_y + 9 * spacing
    s.line([(bx + 6, ly_d27), (190, ly_d27)], color=BLUE, width=2)
    s.rect(16, ly_d27 - 35, 174, 70, fill='#eff6ff', stroke=BLUE, r=8, stroke_width=2)
    s.text(26, ly_d27 - 14, 'GPIO27 (D27)', 15, BLUE, bold=True)
    s.text(26, ly_d27 + 6, 'WS2812B LED Data', 13, INK, bold=True)
    s.text(26, ly_d27 + 25, 'via 330-470Ω to DIN', 12, MUTED)

    # 2. Common Ground
    ly_gnd = start_y + 13 * spacing
    s.line([(bx + 6, ly_gnd), (190, ly_gnd)], color=GROUND, width=2)
    s.rect(16, ly_gnd - 30, 174, 60, fill='#f1f5f9', stroke=GROUND, r=8, stroke_width=2)
    s.text(26, ly_gnd - 10, 'GND (Common Ground)', 14, GROUND, bold=True)
    s.text(26, ly_gnd + 10, 'Tie to LED, CAN &', 12, INK)
    s.text(26, ly_gnd + 25, 'Power Supply GND', 12, INK)

    # 3. 5V Power In (VIN)
    ly_vin = start_y + 14 * spacing
    s.line([(bx + 6, ly_vin), (190, ly_vin)], color=RED, width=2)
    s.rect(16, ly_vin + 10, 174, 65, fill='#fef2f2', stroke=RED, r=8, stroke_width=2)
    s.text(26, ly_vin + 30, 'VIN (5V Power Input)', 14, RED, bold=True)
    s.text(26, ly_vin + 50, 'Regulated 5V from', 12, INK)
    s.text(26, ly_vin + 65, 'DC-DC step-down converter', 11, MUTED)

    # Right Callout Boxes
    # 1. Optional OLED SCL / SDA
    ry_scl = start_y + 1 * spacing
    ry_sda = start_y + 4 * spacing
    s.line([(bx + bw - 6, ry_scl), (550, ry_scl)], color=YELLOW, width=2)
    s.rect(550, ry_scl - 22, 194, 46, fill='#fefce8', stroke=YELLOW, r=8, stroke_width=2)
    s.text(560, ry_scl - 2, 'GPIO22 (D22): OLED SCL', 13, '#854d0e', bold=True)
    s.text(560, ry_scl + 16, 'Optional I2C clock (UserConfig.h)', 11, MUTED)

    s.line([(bx + bw - 6, ry_sda), (550, ry_sda)], color=CYAN, width=2)
    s.rect(550, ry_sda - 22, 194, 46, fill='#f0f9ff', stroke=CYAN, r=8, stroke_width=2)
    s.text(560, ry_sda - 2, 'GPIO21 (D21): OLED SDA', 13, CYAN, bold=True)
    s.text(560, ry_sda + 16, 'Optional I2C data (UserConfig.h)', 11, MUTED)

    # 2. GPIO17 SAFETY ISOLATION (Critical!)
    ry_tx2 = start_y + 8 * spacing
    s.line([(bx + bw - 6, ry_tx2), (530, ry_tx2)], color=RED, width=2.5, dash='4,3')
    s.line([(530, ry_tx2), (546, ry_tx2)], color=RED, width=2.5, dash='4,3')
    # Red X symbol for air gap
    s.line([(535, ry_tx2 - 7), (545, ry_tx2 + 7)], color=RED, width=3)
    s.line([(535, ry_tx2 + 7), (545, ry_tx2 - 7)], color=RED, width=3)
    s.rect(550, ry_tx2 - 38, 194, 76, fill='#fef2f2', stroke=RED, r=8, stroke_width=2)
    s.text(560, ry_tx2 - 18, 'GPIO17: TWAI TX', 14, RED, bold=True)
    s.text(560, ry_tx2 - 1, 'PHYSICALLY DISCONNECTED!', 12, RED, bold=True)
    s.text(560, ry_tx2 + 16, 'Air gap: never wire to transceiver.', 11, INK)
    s.text(560, ry_tx2 + 31, 'Passive listen-only protection.', 11, MUTED)

    # 3. GPIO16 CAN RX
    ry_rx2 = start_y + 9 * spacing
    s.line([(bx + bw - 6, ry_rx2), (550, ry_rx2)], color=PURPLE, width=2)
    s.rect(550, ry_rx2 - 25, 194, 55, fill='#f5f3ff', stroke=PURPLE, r=8, stroke_width=2)
    s.text(560, ry_rx2 - 5, 'GPIO16 (RX2): CAN RX', 14, PURPLE, bold=True)
    s.text(560, ry_rx2 + 12, 'Connect to SN65HVD230', 12, INK)
    s.text(560, ry_rx2 + 25, 'Pin 4 (RXD) 3.3V logic', 11, MUTED)

    # 4. 3V3 Output to Transceiver VCC
    ry_3v3 = start_y + 14 * spacing
    s.line([(bx + bw - 6, ry_3v3), (550, ry_3v3)], color=ORANGE, width=2)
    s.rect(550, ry_3v3 - 25, 194, 60, fill='#fff7ed', stroke=ORANGE, r=8, stroke_width=2)
    s.text(560, ry_3v3 - 5, '3V3 (3.3V Power Out)', 14, ORANGE, bold=True)
    s.text(560, ry_3v3 + 12, 'Powers SN65HVD230 Pin 3', 12, INK)
    s.text(560, ry_3v3 + 27, 'Do not power from 5V or 12V!', 11, RED, bold=True)

    # Bottom Safety & Legend Card
    s.rect(20, 850, 720, 75, fill='#f8fafc', stroke='#cbd5e1', r=10)
    s.text(32, 872, 'LEGEND & ELECTRICAL SAFETY:', 14, INK, bold=True)
    s.text(32, 894, 'RED = 5V Regulated Rail   ORANGE = 3.3V Supply   BLACK = Common Ground   BLUE = WS2812B Data', 12, MUTED)
    s.text(32, 912, 'PURPLE = CAN Receive   DASHED RED = Safety Disconnect (Air Gap)   YELLOW = Optional I2C', 12, MUTED)

    s.save()


# ---------------------------------------------------------------------------
# 2. ESP32 + WS2812B Circuit Diagram
# ---------------------------------------------------------------------------
def make_ws2812b_circuit():
    s = Svg(
        'ws2812b-circuit.svg', 760, 920,
        'ESP32 to WS2812B LED Circuit',
        'Wiring for six WS2812B LEDs, series resistor, bulk capacitor, common ground, and optional 74AHCT125 level shifter.'
    )
    s.text(28, 80, 'Direct-drive (330-470Ω) and optional 74AHCT125 level-shifter circuit', 20, MUTED)

    # Top Section: Direct Drive Circuit
    s.rect(20, 105, 720, 360, fill='#ffffff', stroke='#cbd5e1', r=12)
    s.text(35, 135, 'Standard Direct-Drive Connection (Short Data Wire, Bench & Demo)', 18, INK, bold=True)

    # ESP32 simplified block
    s.rect(40, 160, 160, 160, fill='#1e293b', stroke='#334155', r=8)
    s.text(120, 195, 'ESP32 DevKit', 16, '#f8fafc', bold=True, anchor='middle')
    s.text(120, 218, 'DOIT V1', 13, '#94a3b8', anchor='middle')
    s.rect(170, 240, 30, 22, fill='#93c5fd', stroke=BLUE, r=3)
    s.text(110, 256, 'GPIO27', 14, '#93c5fd', bold=True)
    s.rect(170, 280, 30, 22, fill='#94a3b8', stroke=GROUND, r=3)
    s.text(125, 296, 'GND', 14, '#cbd5e1', bold=True)

    # 330-470 ohm resistor
    s.line([(200, 251), (230, 251)], color=BLUE, width=3)
    s.resistor(230, 251, label="330-470Ω")
    s.line([(314, 251), (345, 251)], color=BLUE, width=3)
    s.arrow(345, 251, 'right', color=BLUE)
    s.text(272, 280, 'Place close to DIN', 11, MUTED, anchor='middle')

    # WS2812B 6-LED Strip
    sx, sy = 345, 175
    s.rect(sx, sy, 375, 145, fill='#244238', stroke='#1b332b', r=8)
    s.text(sx + 20, sy + 30, 'WS2812B 6-LED Shift Light Strip (5V • GRB • 800kHz)', 14, '#a7f3d0', bold=True)

    # 6 LED pixels
    colors = [GREEN, GREEN, YELLOW, YELLOW, ORANGE, RED]
    for i, c in enumerate(colors):
        px = sx + 22 + i * 58
        s.rect(px, sy + 45, 46, 46, fill=c, stroke='#9eb7aa', r=6)
        s.text(px + 23, sy + 73, f'L{i+1}', 13, '#142b3b', bold=True, anchor='middle')

    # Strip terminals
    s.text(sx + 15, sy + 115, 'DIN', 13, '#93c5fd', bold=True)
    s.text(sx + 15, sy + 133, '+5V', 13, '#fca5a5', bold=True)
    s.text(sx + 85, sy + 133, 'GND', 13, '#cbd5e1', bold=True)

    # Signal direction arrow
    s.line([(sx + 50, sy + 112), (sx + 310, sy + 112)], color='#93c5fd', width=2)
    s.arrow(sx + 310, sy + 112, 'right', color='#93c5fd')
    s.text(sx + 180, sy + 106, 'Data direction: DIN -> DOUT', 12, '#93c5fd', anchor='middle')

    # DOUT callout (unused!)
    s.rect(sx + 270, sy + 120, 95, 22, fill='#334155', stroke='#475569', r=4)
    s.text(sx + 317, sy + 136, 'DOUT: UNUSED', 11, '#fca5a5', bold=True, anchor='middle')

    # Power connections and Bulk Capacitor
    # Regulated 5V Supply block
    s.rect(345, 365, 200, 75, fill='#fef2f2', stroke=RED, r=8)
    s.text(355, 388, 'Regulated 5V Supply', 14, RED, bold=True)
    s.text(355, 406, 'At least 1A capacity', 12, INK)
    s.text(355, 424, 'Size for LED strip load', 11, MUTED)

    # 5V wire to strip
    s.line([(545, 395), (630, 395), (630, 310), (430, 310)], color=RED, width=3)
    s.dot(430, 310, color=RED)
    s.line([(430, 310), (430, 300), (370, 300)], color=RED, width=3)

    # Capacitor across strip power entry
    s.cap(650, 240, bulk=True, label="1000µF/10V")
    s.line([(630, 240), (650, 240)], color=RED, width=3)
    s.line([(650, 290), (650, 345), (460, 345)], color=GROUND, width=3)

    # Common Ground wire
    s.line([(200, 291), (280, 291), (280, 345), (345, 345)], color=GROUND, width=3)
    s.line([(345, 345), (460, 345)], color=GROUND, width=3)
    s.dot(280, 345, color=GROUND)
    s.line([(460, 345), (460, 300), (415, 300)], color=GROUND, width=3)
    s.line([(545, 420), (580, 420), (580, 345)], color=GROUND, width=3)
    s.dot(580, 345, color=GROUND)
    s.text(285, 335, 'Common GND', 13, GROUND, bold=True)

    # Direct drive advisory note
    s.text(35, 452, 'Note: Direct 3.3V GPIO data works with most strips over short leads. Place the 330-470Ω resistor close to DIN.', 12, MUTED)

    # Lower Section: Optional 74AHCT125 Level Shifter Circuit
    s.rect(20, 485, 720, 415, fill=CARD_BG, stroke=EDGE, r=12)
    s.text(35, 515, 'Optional Improvement: 74AHCT125 Level Shifter (Recommended for 5V Signalling)', 18, INK, bold=True)
    s.text(35, 538, 'WS2812B datasheet specifies Vih(min) = 0.7 x VDD = 3.5V. The 74AHCT125 converts 3.3V logic to true 5V TTL levels.', 12, MUTED)

    # 74AHCT125 DIP-14 IC package
    cx, cy = 250, 560
    s.rect(cx, cy, 260, 210, fill='#1e293b', stroke='#334155', r=8, stroke_width=2)
    # IC Notch
    s.circle(cx + 130, cy, 10, fill=CARD_BG, stroke='#334155', stroke_width=2)
    s.text(cx + 130, cy + 40, 'SN74AHCT125N', 18, '#f8fafc', bold=True, anchor='middle')
    s.text(cx + 130, cy + 62, 'Quad Buffer / Level Shifter', 13, '#94a3b8', anchor='middle')

    # Pins left side (1 to 7)
    left_ic = [
        ("1", "1 /OE -> GND (Enable)"),
        ("2", "1A (3.3V Input from GPIO27)"),
        ("3", "1Y (5V Output to 330Ω -> DIN)"),
        ("4", "2 /OE -> tie to +5V"),
        ("5", "2A -> tie to GND"),
        ("6", "2Y (unused)"),
        ("7", "GND -> Common Ground")
    ]
    for i, (pin, desc) in enumerate(left_ic):
        py = cy + 85 + i * 16
        s.text(cx + 12, py, pin, 11, '#93c5fd' if pin in ('1','2','3','7') else '#64748b', bold=True)
        s.text(cx + 30, py, desc, 10, '#f8fafc' if pin in ('1','2','3','7') else '#64748b')

    # Pins right side (14 down to 8)
    right_ic = [
        ("14", "VCC -> +5V (with 100nF decoupling)"),
        ("13", "4 /OE -> tie to +5V"),
        ("12", "4A -> tie to GND"),
        ("11", "4Y (unused)"),
        ("10", "3 /OE -> tie to +5V"),
        ("9", "3A -> tie to GND"),
        ("8", "3Y (unused)")
    ]
    for i, (pin, desc) in enumerate(right_ic):
        py = cy + 85 + i * 16
        s.text(cx + 248, py, pin, 11, '#fca5a5' if pin == '14' else '#64748b', bold=True, anchor='end')

    # External wiring callouts for Level Shifter
    # Input from ESP32 GPIO27 with 10k pull-down
    s.rect(40, 600, 160, 110, fill='#eff6ff', stroke=BLUE, r=8)
    s.text(50, 625, 'Input: GPIO27', 15, BLUE, bold=True)
    s.text(50, 646, 'From ESP32 pin D27', 12, INK)
    s.text(50, 668, 'Add 10kΩ pull-down', 12, INK, bold=True)
    s.text(50, 686, 'to GND to avoid boot flash', 11, MUTED)
    s.line([(200, 640), (cx, 640)], color=BLUE, width=2.5)
    s.arrow(cx, 640, 'right', color=BLUE)

    # Output to WS2812B DIN
    s.rect(550, 600, 170, 110, fill='#eff6ff', stroke=BLUE, r=8)
    s.text(560, 625, 'Output: 5V Logic', 15, BLUE, bold=True)
    s.text(560, 646, 'Through 330Ω resistor', 12, INK)
    s.text(560, 668, 'Connects to WS2812B DIN', 12, INK, bold=True)
    s.text(560, 686, 'True 5.0V signal amplitude', 11, MUTED)
    s.line([(cx + 260, 640), (550, 640)], color=BLUE, width=2.5)
    s.arrow(550, 640, 'right', color=BLUE)

    # Decoupling cap note
    s.rect(40, 785, 680, 50, fill='#fefce8', stroke=YELLOW, r=6)
    s.text(50, 807, 'Decoupling: Place a 100nF ceramic capacitor across 74AHCT125 Pin 14 (VCC) and Pin 7 (GND).', 12, '#854d0e', bold=True)
    s.text(50, 824, 'Tie unused inputs (pins 4, 10, 13 to 5V; pins 5, 9, 12 to GND) to prevent floating gate oscillations.', 11, MUTED)

    # Summary box
    s.rect(20, 850, 720, 50, fill='#f1f5f9', stroke=GROUND, r=8)
    s.text(32, 880, 'Golden Rule: Common Ground binds everything. Never join external 5V to ESP32 3.3V.', 13, INK, bold=True)

    s.save()


# ---------------------------------------------------------------------------
# 3. ESP32 + SN65HVD230 CAN Circuit Diagram
# ---------------------------------------------------------------------------
def make_sn65hvd230_circuit():
    s = Svg(
        'sn65hvd230-circuit.svg', 760, 960,
        'ESP32 to SN65HVD230 CAN Transceiver Circuit',
        'Passive receive-only bus tap: 8-pin connections, RS to GND, TXD pull-up, GPIO17 air gap, and 120 ohm removal.'
    )
    s.text(28, 80, 'Passive receive-only bus tap: electrical connections and safety requirements', 20, MUTED)

    # Visual Schematic Box
    s.rect(20, 105, 720, 390, fill='#ffffff', stroke='#cbd5e1', r=12)

    # Left: ESP32 Block
    s.rect(35, 140, 150, 240, fill='#1e293b', stroke='#334155', r=8)
    s.text(110, 175, 'ESP32 DevKit', 16, '#f8fafc', bold=True, anchor='middle')
    s.text(110, 196, 'TWAI Interface', 13, '#94a3b8', anchor='middle')

    s.rect(145, 220, 40, 22, fill='#fed7aa', stroke=ORANGE, r=3)
    s.text(75, 236, '3V3', 14, ORANGE, bold=True)

    s.rect(145, 260, 40, 22, fill='#94a3b8', stroke=GROUND, r=3)
    s.text(75, 276, 'GND', 14, '#cbd5e1', bold=True)

    s.rect(145, 300, 40, 22, fill='#d8b4fe', stroke=PURPLE, r=3)
    s.text(50, 316, 'GPIO16 (RX2)', 13, PURPLE, bold=True)

    s.rect(145, 340, 40, 22, fill='#fecaca', stroke=RED, r=3)
    s.text(50, 356, 'GPIO17 (TX2)', 13, RED, bold=True)

    # Center: SN65HVD230 Transceiver Module
    tx_x, tx_y, tx_w, tx_h = 280, 130, 220, 260
    s.rect(tx_x, tx_y, tx_w, tx_h, fill='#1e293b', stroke='#334155', r=10, stroke_width=2)
    s.text(tx_x + tx_w//2, tx_y + 30, 'SN65HVD230', 18, '#f8fafc', bold=True, anchor='middle')
    s.text(tx_x + tx_w//2, tx_y + 50, '3.3V CAN Transceiver', 13, '#94a3b8', anchor='middle')

    # Module Pins
    # Pin 3 VCC
    s.line([(185, 231), (tx_x, 231)], color=ORANGE, width=3)
    s.arrow(tx_x, 231, 'right', color=ORANGE)
    s.text(tx_x + 10, 236, '3: VCC (3.3V)', 12, ORANGE, bold=True)

    # Pin 2 GND
    s.line([(185, 271), (tx_x, 271)], color=GROUND, width=3)
    s.dot(230, 271, color=GROUND)
    s.line([(230, 271), (230, 410)], color=GROUND, width=3)
    s.ground(230, 410)
    s.text(tx_x + 10, 276, '2: GND', 12, '#cbd5e1', bold=True)

    # Pin 4 RXD
    s.line([(tx_x, 311), (185, 311)], color=PURPLE, width=3)
    s.arrow(185, 311, 'left', color=PURPLE)
    s.text(tx_x + 10, 316, '4: R / RXD', 12, PURPLE, bold=True)

    # Pin 1 TXD: Pull-up to 3.3V & Air-gap Disconnect
    s.text(tx_x + 10, 356, '1: D / TXD', 12, RED, bold=True)
    # Pull-up resistor to 3.3V
    s.line([(tx_x, 351), (250, 351)], color=RED, width=2.5)
    s.resistor(250, 351, label="10kΩ", vertical=True)
    s.line([(250, 351), (250, 231)], color=ORANGE, width=2.5)
    s.dot(250, 231, color=ORANGE)
    s.text(268, 290, 'Recessive pull-up', 11, ORANGE)

    # GPIO17 wire terminates at air gap
    s.line([(185, 351), (220, 351)], color=RED, width=2.5, dash='4,3')
    # Air-gap indicator
    s.circle(225, 351, 4, fill='#ffffff', stroke=RED, stroke_width=2)
    s.circle(237, 351, 4, fill='#ffffff', stroke=RED, stroke_width=2)
    s.text(195, 380, 'AIR GAP', 11, RED, bold=True)

    # Pin 8 Rs to GND (Normal High-Speed Mode)
    s.text(tx_x + tx_w - 10, 236, 'Rs: 8', 12, '#38bdf8', bold=True, anchor='end')
    s.line([(tx_x + tx_w, 231), (tx_x + tx_w + 30, 231), (tx_x + tx_w + 30, 271), (tx_x + tx_w, 271)], color='#38bdf8', width=2.5)
    s.text(tx_x + tx_w + 36, 255, 'Rs -> GND', 12, '#38bdf8', bold=True)

    # Pin 5 Vref (NC)
    s.text(tx_x + tx_w - 10, 276, 'Vref: 5 (NC)', 11, '#94a3b8', anchor='end')

    # Pin 7 CANH and Pin 6 CANL
    s.text(tx_x + tx_w - 10, 316, 'CANH: 7', 12, GREEN, bold=True, anchor='end')
    s.text(tx_x + tx_w - 10, 356, 'CANL: 6', 12, YELLOW, bold=True, anchor='end')

    # Right: Motorcycle CAN Bus lines
    s.line([(tx_x + tx_w, 311), (580, 311)], color=GREEN, width=3)
    s.line([(tx_x + tx_w, 351), (580, 351)], color=YELLOW, width=3)

    # Motorcycle side box
    s.rect(580, 140, 140, 240, fill='#f8fafc', stroke='#94a3b8', r=8)
    s.text(650, 175, 'Motorcycle', 16, INK, bold=True, anchor='middle')
    s.text(650, 196, 'Diagnostic Port', 13, MUTED, anchor='middle')
    s.rect(590, 295, 120, 28, fill='#dcfce7', stroke=GREEN, r=4)
    s.text(650, 314, 'CAN-H (Differential)', 11, GREEN, bold=True, anchor='middle')
    s.rect(590, 335, 120, 28, fill='#fef9c3', stroke=YELLOW, r=4)
    s.text(650, 354, 'CAN-L (Differential)', 11, '#854d0e', bold=True, anchor='middle')

    # Common ground link to bike
    s.line([(650, 220), (650, 260), (450, 260)], color=GROUND, width=2)
    s.text(650, 245, 'Chassis/Signal GND', 11, GROUND, anchor='middle')

    # Optional TVS Diode Callout
    s.rect(515, 290, 45, 80, fill='#eff6ff', stroke=CYAN, r=4)
    s.text(537, 335, 'TVS', 11, CYAN, bold=True, anchor='middle')
    s.text(537, 385, 'Optional TVS', 10, CYAN, anchor='middle')

    # Callout 1: Mandatory Passive CAN Safety
    s.rect(20, 510, 720, 130, fill='#fef2f2', stroke=RED, r=10, stroke_width=2)
    s.text(35, 535, '1. MANDATORY PASSIVE CAN SAFETY SAFEGUARDS', 15, RED, bold=True)
    s.text(35, 558, '• GPIO17 PHYSICAL ISOLATION: ESP32 GPIO17 must remain completely disconnected (open air gap).', 13, INK, bold=True)
    s.text(50, 576, 'Software listen-only mode alone cannot protect against bootloader pulses or hardware bugs.', 12, MUTED)
    s.text(35, 598, '• TXD FORCED RECESSIVE: Pin 1 (TXD) is pulled up to 3.3V via a 10kΩ resistor, forcing the transmitter recessive.', 13, INK, bold=True)
    s.text(50, 616, 'The SN65HVD230 cannot drive dominant bits onto the motorcycle CAN bus while TXD is held HIGH.', 12, MUTED)
    s.text(35, 634, '• RS PIN TO GND: Pin 8 (Rs) to GND selects normal high-speed mode. Rs LOW does NOT enable TX when TXD is high.', 12, INK)

    # Callout 2: 120 Ohm Termination Removal
    s.rect(20, 655, 720, 130, fill='#fff7ed', stroke=ORANGE, r=10, stroke_width=2)
    s.text(35, 680, '2. REMOVE ONBOARD 120Ω TERMINATION RESISTOR (CRITICAL)', 15, ORANGE, bold=True)
    s.text(35, 703, '• Commercial SN65HVD230 breakout boards typically include an onboard 120Ω SMD resistor (labeled R2 or 1200).', 13, INK)
    s.text(35, 723, '• The motorcycle CAN bus already has 120Ω terminating resistors at each end (total bus impedance = 60Ω).', 13, INK)
    s.text(35, 743, '• Adding an extra 120Ω stub resistor drops bus impedance to 40Ω, causing ECU communication failure and error codes!', 13, RED, bold=True)
    s.text(35, 765, '• Action: Measure CANH-to-CANL resistance with a multimeter. If ~120Ω, desolder resistor R2 or cut the trace.', 13, INK, bold=True)

    # Callout 3: Automotive Protection & Summary Table
    s.rect(20, 798, 720, 145, fill='#f8fafc', stroke=EDGE, r=10)
    s.text(35, 822, '3. CONNECTION REFERENCE & VOLTAGE RULES', 15, INK, bold=True)
    s.text(35, 846, 'Pin 1 (TXD) -> 3.3V via 10kΩ pull-up (forced recessive)     Pin 5 (Vref) -> Leave unconnected (NC)', 12, MUTED, mono=True)
    s.text(35, 866, 'Pin 2 (GND) -> ESP32 GND and Motorcycle Signal GND          Pin 6 (CANL) -> Motorcycle CAN-L', 12, MUTED, mono=True)
    s.text(35, 886, 'Pin 3 (VCC) -> ESP32 3V3 ONLY (never connect 5V or 12V)     Pin 7 (CANH) -> Motorcycle CAN-H', 12, MUTED, mono=True)
    s.text(35, 906, 'Pin 4 (RXD) -> ESP32 GPIO16 (TWAI receive)                  Pin 8 (Rs)   -> GND (Normal high-speed)', 12, MUTED, mono=True)
    s.text(35, 930, 'Recommended Protection: Add bidirectional TVS diodes (e.g. PESD1CAN) from CANH/CANL to GND for automotive ESD protection.', 11, CYAN, bold=True)

    s.save()


# ---------------------------------------------------------------------------
# 4. Complete PulseShift System Circuit Diagram
# ---------------------------------------------------------------------------
def make_pulseshift_complete():
    s = Svg(
        'pulseshift-wiring.svg', 760, 1020,
        'Complete System Hardware Wiring',
        'End-to-end wiring: Motorcycle power & diagnostic connector, DC-DC converter, ESP32, CAN transceiver, LEDs, and optional OLED.'
    )
    s.text(28, 80, 'Full system integration: motorcycle power, passive CAN tap, ESP32, and shift light', 20, MUTED)

    # 1. Power Section (Top-Left)
    s.rect(20, 105, 345, 235, fill='#ffffff', stroke='#cbd5e1', r=10)
    s.text(35, 130, 'POWER SUBSYSTEM (Motorcycle)', 15, DARK_RED, bold=True)

    # Battery & Ignition
    s.rect(35, 145, 130, 65, fill='#fef2f2', stroke=DARK_RED, r=6)
    s.text(100, 172, '12V Switched', 13, DARK_RED, bold=True, anchor='middle')
    s.text(100, 192, 'Ignition Feed', 12, MUTED, anchor='middle')

    # Inline Fuse
    s.line([(165, 177), (195, 177)], color=DARK_RED, width=3)
    s.rect(195, 167, 35, 20, fill='#fef3c7', stroke=DARK_RED, r=4)
    s.text(212, 181, '1A', 11, DARK_RED, bold=True, anchor='middle')
    s.text(212, 202, 'Fuse', 11, MUTED, anchor='middle')

    # DC-DC Step-Down Converter
    s.rect(250, 145, 100, 75, fill='#f8fafc', stroke='#334155', r=6)
    s.text(300, 170, 'DC-DC Buck', 13, INK, bold=True, anchor='middle')
    s.text(300, 188, '12V -> 5V', 12, RED, bold=True, anchor='middle')
    s.text(300, 205, '≥ 2A Rating', 11, MUTED, anchor='middle')
    s.line([(230, 177), (250, 177)], color=DARK_RED, width=3)

    # Ground at power
    s.line([(100, 210), (100, 235), (300, 235), (300, 220)], color=GROUND, width=3)
    s.dot(200, 235, color=GROUND)
    s.ground(200, 235)
    s.text(100, 260, 'Chassis / Battery GND', 11, GROUND)

    # Power Danger Callout
    s.rect(30, 275, 325, 55, fill='#fef2f2', stroke=RED, r=6)
    s.text(40, 295, 'DANGER: NEVER connect raw +12V', 13, RED, bold=True)
    s.text(40, 313, 'directly to ESP32 (VIN, 5V, 3V3) or CAN transceiver!', 11, RED)

    # 2. CAN Interface Subsystem (Bottom-Left)
    s.rect(20, 350, 345, 330, fill='#ffffff', stroke='#cbd5e1', r=10)
    s.text(35, 375, 'CAN INTERFACE (Passive Tap)', 15, PURPLE, bold=True)

    # Motorcycle Diagnostic Port
    s.rect(35, 390, 140, 95, fill='#f8fafc', stroke='#64748b', r=6)
    s.text(105, 412, 'Diagnostic Port', 13, INK, bold=True, anchor='middle')
    s.text(105, 430, 'Euro 5 / OBD-II', 11, MUTED, anchor='middle')
    s.text(45, 452, 'CAN-H', 11, GREEN, bold=True)
    s.text(45, 472, 'CAN-L', 11, YELLOW, bold=True)
    s.text(115, 472, 'GND', 11, GROUND, bold=True)

    # SN65HVD230 Transceiver Module
    s.rect(205, 390, 145, 160, fill='#1e293b', stroke='#334155', r=6)
    s.text(277, 412, 'SN65HVD230', 13, '#f8fafc', bold=True, anchor='middle')
    s.text(277, 428, '3.3V Transceiver', 10, '#94a3b8', anchor='middle')
    s.text(215, 452, 'CANH / CANL', 10, '#f8fafc')
    s.text(215, 472, 'Rs -> GND', 10, '#38bdf8')
    s.text(215, 492, 'TXD -> 3.3V', 10, RED)
    s.text(215, 512, 'RXD -> GPIO16', 10, PURPLE, bold=True)
    s.text(215, 532, 'VCC 3.3V / GND', 10, ORANGE)

    # Connecting lines from port to transceiver
    s.line([(175, 448), (205, 448)], color=GREEN, width=2.5)
    s.line([(175, 468), (205, 468)], color=YELLOW, width=2.5)

    # Passive safety badges
    s.rect(35, 560, 315, 108, fill='#fef2f2', stroke=RED, r=6)
    s.text(45, 582, 'PASSIVE SAFETY REQUIREMENTS:', 12, RED, bold=True)
    s.text(45, 600, '1. GPIO17 Physically Disconnected (Air Gap)', 11, INK, bold=True)
    s.text(45, 618, '2. Transceiver TXD pulled up to 3.3V (Recessive)', 11, INK)
    s.text(45, 636, '3. Onboard 120Ω resistor REMOVED from module', 11, ORANGE, bold=True)
    s.text(45, 654, '4. Transceiver VCC powered from ESP32 3V3 rail', 11, INK)

    # 3. ESP32 DevKit V1 (Center-Right)
    s.rect(385, 105, 355, 395, fill='#1e293b', stroke='#334155', r=12)
    s.text(562, 138, 'ESP32 DevKit V1 (Core Controller)', 17, '#f8fafc', bold=True, anchor='middle')
    s.text(562, 160, 'TWAI Listen-Only • Shift Light Logic • Diag Logging', 12, '#94a3b8', anchor='middle')

    # Pins on ESP32
    # VIN & GND (Power input)
    s.rect(400, 185, 65, 25, fill='#fca5a5', stroke=RED, r=4)
    s.text(432, 202, 'VIN (5V)', 12, RED, bold=True, anchor='middle')
    s.rect(400, 220, 65, 25, fill='#94a3b8', stroke=GROUND, r=4)
    s.text(432, 237, 'GND', 12, GROUND, bold=True, anchor='middle')

    # 3V3 Out
    s.rect(400, 260, 65, 25, fill='#fed7aa', stroke=ORANGE, r=4)
    s.text(432, 277, '3V3 OUT', 12, ORANGE, bold=True, anchor='middle')

    # GPIO16 (TWAI RX)
    s.rect(400, 310, 85, 25, fill='#d8b4fe', stroke=PURPLE, r=4)
    s.text(442, 327, 'GPIO16 (RX)', 12, PURPLE, bold=True, anchor='middle')

    # GPIO17 (TWAI TX) - Safety Disconnect
    s.rect(400, 350, 85, 25, fill='#fecaca', stroke=RED, r=4)
    s.text(442, 367, 'GPIO17 (TX)', 12, RED, bold=True, anchor='middle')
    s.line([(485, 362), (520, 362)], color=RED, width=2.5, dash='4,3')
    s.circle(526, 362, 4, fill='#ffffff', stroke=RED, stroke_width=2)
    s.text(538, 367, 'DISCONNECTED', 11, RED, bold=True)

    # GPIO27 (WS2812B Data)
    s.rect(400, 400, 85, 25, fill='#93c5fd', stroke=BLUE, r=4)
    s.text(442, 417, 'GPIO27', 12, BLUE, bold=True, anchor='middle')

    # GPIO21 / GPIO22 (Optional OLED)
    s.rect(400, 445, 85, 22, fill='#fef08a', stroke=YELLOW, r=4)
    s.text(442, 461, 'GPIO21/22', 11, '#854d0e', bold=True, anchor='middle')
    s.text(495, 461, 'Optional I2C (OLED)', 11, '#94a3b8')

    # Micro-USB port on ESP32
    s.rect(630, 445, 90, 30, fill='#cbd5e1', stroke='#64748b', r=4)
    s.text(675, 465, 'Micro-USB', 11, INK, bold=True, anchor='middle')

    # Wire 5V from DC-DC to ESP32 VIN
    s.line([(350, 182), (375, 182), (375, 197), (400, 197)], color=RED, width=3)
    s.dot(375, 182, color=RED)

    # Wire 3.3V from ESP32 to Transceiver VCC
    s.line([(400, 272), (360, 272), (360, 532), (350, 532)], color=ORANGE, width=2.5)

    # Wire GPIO16 from Transceiver RXD to ESP32
    s.line([(350, 512), (370, 512), (370, 322), (400, 322)], color=PURPLE, width=2.5)

    # 4. WS2812B LED Shift Light (Mid-Right)
    s.rect(385, 515, 355, 190, fill='#244238', stroke='#1b332b', r=10)
    s.text(400, 540, 'WS2812B 6-LED Shift Light (5V • GRB)', 15, '#a7f3d0', bold=True)

    # 6 Pixels
    led_colors = [GREEN, GREEN, YELLOW, YELLOW, ORANGE, RED]
    for i, c in enumerate(led_colors):
        px = 405 + i * 53
        s.rect(px, 555, 42, 42, fill=c, stroke='#9eb7aa', r=6)
        s.text(px + 21, 580, f'L{i+1}', 12, INK, bold=True, anchor='middle')

    # Data line from GPIO27 through 330Ω to DIN
    s.line([(485, 412), (540, 412), (540, 615), (420, 615)], color=BLUE, width=2.5)
    s.resistor(420, 615, label="330Ω")
    s.line([(504, 615), (525, 615), (525, 630)], color=BLUE, width=2.5)
    s.arrow(525, 630, 'down', color=BLUE)
    s.text(400, 650, 'DIN <- Data in', 12, '#93c5fd', bold=True)
    s.text(620, 650, 'DOUT: UNCONNECTED', 11, '#fca5a5', bold=True)

    # 1000uF Cap across LED power
    s.cap(665, 540, bulk=True, label="1000µF")

    # 5. Optional SSD1306 OLED Display (Bottom-Right)
    s.rect(385, 718, 355, 125, fill='#ffffff', stroke=EDGE, r=10)
    s.text(400, 742, 'SSD1306 OLED Display (OPTIONAL)', 14, INK, bold=True)
    s.text(400, 762, 'Disabled by default in UserConfig.h (PULSESHIFT_OLED_ENABLED = 0)', 11, MUTED)
    s.text(400, 785, 'VCC -> ESP32 3V3     GND -> Common Ground', 11, MUTED, mono=True)
    s.text(400, 805, 'SDA -> ESP32 GPIO21  SCL -> ESP32 GPIO22', 11, MUTED, mono=True)
    s.text(400, 825, 'Provide ESP32-safe 4.7kΩ pull-ups to 3.3V if display lacks onboard pull-ups.', 10, MUTED)

    # 6. Bottom Master Legend & Safety Checklist
    s.rect(20, 855, 720, 145, fill='#f8fafc', stroke='#cbd5e1', r=10)
    s.text(35, 880, 'SYSTEM WIRING LEGEND & VERIFICATION CHECKLIST:', 14, INK, bold=True)
    s.text(35, 902, 'DARK RED = 12V Switched Bike Power   RED = Regulated 5V Rail   ORANGE = 3.3V Logic Supply', 11, MUTED)
    s.text(35, 920, 'BLACK = Common Ground (All bonded)   BLUE = WS2812B Data (DIN)   PURPLE = CAN Receive (RX2)', 11, MUTED)
    s.text(35, 938, 'GREEN = Motorcycle CAN-H             YELLOW = Motorcycle CAN-L   DASHED RED = Safety Disconnect', 11, MUTED)
    s.text(35, 962, 'Before connecting to bike:', 12, INK, bold=True)
    s.text(35, 982, '1. Verify 5V regulator output with multimeter before attaching ESP32 or LEDs.', 11, INK)
    s.text(35, 995, '2. Verify GPIO17 is completely disconnected and transceiver TXD is tied to 3.3V.', 11, INK)

    s.save()


# ---------------------------------------------------------------------------
# 5. Hardware Overview Diagram (Parts Kit)
# ---------------------------------------------------------------------------
def make_hardware_overview():
    s = Svg(
        'hardware-overview.svg', 720, 920,
        'Gather these parts',
        'Required parts for PulseShift shift light, bench testing, and passive CAN monitoring.'
    )
    s.text(28, 82, 'Hardware component inventory for demo and CAN capture', 22, MUTED)

    items = [
        ('ESP32 DevKit V1', '30-pin board with USB'),
        ('6 x WS2812B LEDs', '5V • GRB • 800kHz strip'),
        ('330-470Ω resistor', 'Series data resistor for DIN'),
        ('1000µF 10V capacitor', 'Bulk cap across LED power'),
        ('Regulated 5V supply', 'At least 1A capacity (or DC-DC)'),
        ('SN65HVD230 module', '3.3V CAN transceiver (remove 120Ω)'),
        ('Secure wires & breadboard', 'Short data lead + common GND'),
        ('Digital multimeter', 'Verify voltages and bus resistance')
    ]

    for i, (title, note) in enumerate(items):
        x = 28 + (i % 2) * 348
        y = 108 + (i // 2) * 190
        s.rect(x, y, 320, 172)
        if i == 0:
            # ESP32 icon
            s.rect(x + 110, y + 14, 100, 60, fill='#244238', stroke=GROUND, r=4)
            s.rect(x + 135, y + 21, 50, 32, fill='#b7c4cc', stroke=GROUND, r=2)
            for pin in range(6):
                s.line([(x + 102, y + 20 + pin * 8), (x + 110, y + 20 + pin * 8)])
                s.line([(x + 210, y + 20 + pin * 8), (x + 218, y + 20 + pin * 8)])
        elif i == 1:
            # LED strip icon
            s.strip(x + 38, y + 29, [GREEN, GREEN, YELLOW, YELLOW, ORANGE, RED], step=43, size=29)
        elif i == 2:
            # Resistor icon
            s.resistor(x + 117, y + 45, label="330Ω")
        elif i == 3:
            # Capacitor icon
            s.rect(x + 130, y + 15, 55, 50, fill='#263647', stroke=GROUND, r=6)
            s.rect(x + 165, y + 18, 15, 44, fill='#e6edf3', stroke='#e6edf3', r=2)
            s.text(x + 168, y + 47, '-', 17)
            s.line([(x + 140, y + 65), (x + 140, y + 83)])
            s.line([(x + 173, y + 65), (x + 173, y + 76)])
        elif i == 4:
            # Power supply icon
            s.rect(x + 112, y + 16, 98, 58, fill=GROUND, stroke=GROUND, r=8)
            s.text(x + 161, y + 53, '5V', 27, '#fff', bold=True, anchor='middle')
            s.line([(x + 210, y + 45), (x + 246, y + 45)], color=RED)
        elif i == 5:
            # CAN Transceiver icon
            s.rect(x + 110, y + 16, 100, 58, fill='#1e293b', stroke='#334155', r=6)
            s.text(x + 160, y + 45, 'CAN', 16, '#38bdf8', bold=True, anchor='middle')
            s.text(x + 160, y + 62, 'SN65HVD230', 10, '#94a3b8', anchor='middle')
        elif i == 6:
            # Wires icon
            s.line([(x + 106, y + 38), (x + 214, y + 38)], color=BLUE)
            s.line([(x + 106, y + 52), (x + 214, y + 52)], color=RED)
            s.line([(x + 106, y + 66), (x + 214, y + 66)], color=GROUND)
        elif i == 7:
            # Multimeter icon
            s.rect(x + 128, y + 14, 64, 72, fill='#ea580c', stroke=GROUND, r=6)
            s.rect(x + 138, y + 24, 44, 22, fill='#e2e8f0', stroke=GROUND, r=2)
            s.text(x + 160, y + 40, '0.00', 12, INK, bold=True, anchor='middle')
            s.circle(x + 160, y + 62, 8, fill='#cbd5e1', stroke=GROUND)

        s.text(x + 160, y + 122, title, 20, INK, bold=True, anchor='middle')
        s.text(x + 160, y + 148, note, 14, MUTED, anchor='middle')

    s.save()


# ---------------------------------------------------------------------------
# 6. Main Direct Demo Wiring Diagram
# ---------------------------------------------------------------------------
def make_main_wiring():
    s = Svg(
        'main-wiring.svg', 720, 1185,
        'Direct GPIO27 LED wiring',
        'ESP32 GPIO27 at 3.3V through 330 ohms into LED DIN. USB powers ESP32, external regulated 5V powers the six LEDs.'
    )
    s.text(28, 84, 'Assemble and check with BOTH supplies disconnected.', 22, MUTED)
    s.text(28, 116, 'BLUE = 3.3V data   RED = +5V   BLACK = GND', 22, MUTED)
    s.text(235, 151, 'USB power', 25, PURPLE, bold=True)
    s.line([(360, 154), (360, 175)], color=PURPLE)
    s.arrow(360, 175, 'down', color=PURPLE)

    s.rect(220, 175, 280, 145)
    s.text(243, 215, 'ESP32 DevKit V1', 25, bold=True)
    s.text(243, 255, 'GPIO27 · 3.3V data', 24, BLUE)
    s.text(243, 292, 'GND', 24, GROUND)

    s.line([(220, 280), (40, 280), (40, 900), (680, 900)], color=GROUND)
    s.line([(360, 320), (360, 345)], color=BLUE)
    s.resistor(360, 345, label="330Ω", vertical=True)
    s.line([(360, 415), (360, 426), (80, 426), (80, 495), (148, 495)], color=BLUE)
    s.arrow(148, 495, color=BLUE)
    s.text(64, 398, 'Keep the data lead short.', 22, BLUE)

    s.strip(160, 470, [OFF] * 6, step=67, size=45)
    s.text(144, 455, 'DIN', 25, BLUE, bold=True)
    s.line([(552, 495), (630, 495)], color=BLUE)
    s.dot(630, 495, color=BLUE, r=4)
    s.text(495, 455, 'DOUT unused', 24, MUTED)
    s.text(241, 563, 'Six LEDs ->', 24, bold=True)

    s.line([(200, 527), (200, 620), (40, 620)], color=GROUND)
    s.dot(40, 620)
    s.text(118, 575, 'GND', 24, GROUND)

    s.line([(480, 527), (480, 600), (650, 600), (650, 760)], color=RED)
    s.text(492, 575, '+5V', 24, RED, bold=True)
    s.line([(650, 660), (360, 660), (360, 710)], color=RED)
    s.dot(650, 660, color=RED)
    s.cap(360, 710, bulk=True, label="1000µF/10V")
    s.line([(360, 760), (360, 900)], color=GROUND)
    s.dot(360, 900)

    s.text(69, 716, '1000µF / 10V', 25, bold=True)
    s.text(69, 755, '+ lead to LED +5V', 23, RED)
    s.text(69, 793, '- striped side to GND', 23)

    s.rect(440, 760, 240, 100, fill='#fff2f0')
    s.text(459, 792, 'Regulated 5V', 25, RED, bold=True)
    s.text(459, 824, 'At least 1A', 24)
    s.text(628, 751, '+', 27, RED, bold=True)

    s.line([(500, 860), (500, 900)], color=GROUND)
    s.dot(500, 900)
    s.text(517, 888, 'Supply GND', 22)

    s.text(28, 935, 'Common GND: ESP32 + LEDs + supply negative', 23, bold=True)
    s.rect(28, 963, 664, 92, fill='#fff1f0', stroke='#e8b1ac')
    s.text(44, 997, 'DO NOT connect external +5V', 27, RED, bold=True)
    s.text(44, 1031, 'to USB-powered ESP32 5V / VIN / 3V3 pins.', 24, RED)

    s.text(28, 1094, 'CAN module disconnected; GPIO16/17 reserved.', 24, bold=True)
    s.text(28, 1133, 'Direct 3.3V data is strip-dependent.', 25, RED, bold=True)
    s.text(28, 1170, 'A compatible strip must be confirmed by physical testing.', 23, MUTED)

    s.save()


# ---------------------------------------------------------------------------
# 7. LED Data Direction Diagram
# ---------------------------------------------------------------------------
def make_led_direction():
    s = Svg(
        'led-direction.svg', 720, 390,
        'Find the strip INPUT',
        'Direct 3.3V ESP32 data through 330 ohms connects to DIN. Follow the printed arrow toward DOUT, which is unused.'
    )
    s.text(28, 86, 'GPIO27 -> 330Ω -> DIN (3.3V data)', 25, BLUE)
    s.line([(44, 110), (44, 192), (108, 192)], color=BLUE)
    s.arrow(108, 192, color=BLUE)
    s.strip(120, 170, [OFF] * 6, step=82, size=46)
    s.text(112, 260, 'DIN / IN', 26, BLUE, bold=True)
    s.text(512, 260, 'DOUT / OUT', 26, bold=True)
    s.line([(252, 270), (490, 270)], color=BLUE)
    s.arrow(490, 270, color=BLUE)
    s.text(282, 307, 'Printed arrow', 23, MUTED)
    s.text(28, 355, 'Connect DIN. Leave DOUT unused.', 25, bold=True)
    s.save()


# ---------------------------------------------------------------------------
# 8. Power Safety Diagram
# ---------------------------------------------------------------------------
def make_power_safety():
    s = Svg(
        'power-safety.svg', 720, 825,
        'Two supplies. One common ground.',
        'USB powers only the ESP32. Regulated external 5V powers only the LEDs. Grounds join. Check polarity before power.'
    )
    s.text(28, 84, 'Check polarity with all power disconnected.', 25, RED, bold=True)
    s.rect(30, 125, 285, 92, fill='#f3effb')
    s.text(52, 164, 'USB power', 27, PURPLE, bold=True)
    s.text(52, 197, 'ESP32 USB socket only', 22)

    s.rect(405, 125, 285, 92, fill='#fff2f0')
    s.text(425, 164, 'Regulated 5V ≥1A', 25, RED, bold=True)
    s.text(425, 197, 'To LED strip only', 23)

    s.line([(172, 217), (172, 288)], color=PURPLE)
    s.arrow(172, 288, 'down', color=PURPLE)
    s.line([(548, 217), (548, 288)], color=RED)
    s.arrow(548, 288, 'down', color=RED)

    s.rect(65, 288, 250, 94)
    s.text(90, 327, 'ESP32', 28, bold=True)
    s.text(90, 360, 'USB-powered', 23)

    s.rect(405, 288, 285, 94)
    s.text(425, 327, '6 x WS2812B LEDs', 24, bold=True)
    s.text(425, 360, 'External 5V-powered', 22)

    s.line([(172, 382), (172, 434), (548, 434), (548, 382)], color=GROUND, width=5)
    s.dot(360, 434)
    s.ground(360, 434)
    s.text(40, 488, 'Join GND: ESP32 + LEDs + supply negative', 23, bold=True)

    s.rect(30, 515, 660, 122)
    s.rect(66, 537, 66, 77, fill=GROUND, stroke=GROUND, r=7)
    s.rect(108, 541, 18, 69, fill='#e3eaf0', stroke='#e3eaf0', r=2)
    s.text(108, 581, '-', 20)
    s.text(155, 550, '1000µF / 10V electrolytic', 25, bold=True)
    s.text(155, 587, '+ lead -> LED +5V', 24, RED)
    s.text(155, 620, '- striped side -> GND', 24)

    s.text(30, 680, 'ON: LED supply first, then ESP32 USB.', 24, bold=True)
    s.text(30, 720, 'OFF: unplug ESP32 USB first, then LED supply.', 24, bold=True)
    s.text(30, 764, 'Never join the two positive power rails.', 26, RED, bold=True)
    s.text(30, 799, 'No live rewiring. Keep common ground connected.', 23, MUTED)

    s.save()


# ---------------------------------------------------------------------------
# 9. Expected Behavior Diagram
# ---------------------------------------------------------------------------
def make_expected_behavior():
    s = Svg(
        'expected-behavior.svg', 720, 960,
        'What a working demo looks like',
        'Illustrative LED states: startup sweep and pulse, progressive classic colors, flashing red shift alert at 6000 RPM, then deliberate stale-data blanking and recovery.'
    )
    s.text(28, 83, 'Illustrative snapshots: simulated RPM only', 24, MUTED)

    stages = [
        ('1  STARTUP', 'Short sweep forward/back, then a pulse.', [OFF, GREEN, OFF, OFF, OFF, OFF]),
        ('2  PROGRESSIVE FILL', 'More pixels light as the demo RPM rises.', [GREEN, GREEN, YELLOW, OFF, OFF, OFF]),
        ('3  SHIFT ALERT', 'All six flash red at 6000 simulated RPM.', [RED] * 6),
        ('4  TEST PAUSE + RECOVERY', 'A planned dark interval, then lights return.', [OFF] * 6)
    ]

    for i, (title, note, colors) in enumerate(stages):
        y = 113 + i * 196
        s.rect(28, y, 664, 177)
        s.text(49, y + 36, title, 26, bold=True)
        s.text(49, y + 71, note, 23, MUTED)
        s.strip(64, y + 104, colors, step=70, size=40)
        s.text(514, y + 134, 'ON / OFF' if i == 2 else ('-> <-' if i == 0 else ''), 23, RED if i == 2 else BLUE, bold=True)

    s.text(28, 916, 'Dark squares = off. Colors are illustrative.', 23, MUTED)
    s.text(28, 947, 'Default brightness: 40/255. 0-6000 RPM demo range.', 23, MUTED)

    s.save()


if __name__ == '__main__':
    generators = [
        make_esp32_pinout,
        make_ws2812b_circuit,
        make_sn65hvd230_circuit,
        make_pulseshift_complete,
        make_hardware_overview,
        make_main_wiring,
        make_led_direction,
        make_power_safety,
        make_expected_behavior
    ]
    for gen in generators:
        gen()
    print('Successfully generated all 9 SVG diagrams in assets/readme/')
