# Copyright (c) 2026 Aritra Saha. All rights reserved.
# Compile-time validation of UserConfig.h variants: pin conflicts, invalid
# values and the removal guarantees for the OLED/mode-matrix settings.
import subprocess,tempfile,re,unittest,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which("g++"), "g++ compiler not found in PATH")
class ConfigurationTests(unittest.TestCase):
    def compile_config(self,changes):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td)
            cfg_path = ROOT/"PulseShift/UserConfig.h" if (ROOT/"PulseShift/UserConfig.h").exists() else ROOT/"UserConfig.h"
            source=cfg_path.read_text()
            for key,value in changes.items():
                source,count=re.subn(rf"(?m)^(#define {key} )[^\n]*",rf"\g<1>{value}",source)
                self.assertEqual(count,1)
            (path/"UserConfig.h").write_text(source)
            return subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-x","c++","-fsyntax-only","-"],input='#include "UserConfig.h"\n',text=True,cwd=path,capture_output=True)
    def test_default_config_compiles(self):
        self.assertEqual(self.compile_config({}).returncode,0)
    def test_conflicts_and_invalid_settings_rejected(self):
        cases=[{"PULSESHIFT_LED_DATA_GPIO":"16"},{"PULSESHIFT_LED_DATA_GPIO":"17"},
           {"PULSESHIFT_LED_DATA_GPIO":"20"},{"PULSESHIFT_LED_COUNT":"0"},
           {"PULSESHIFT_LED_COUNT":"7"},
           {"PULSESHIFT_CAN_RX_GPIO":"17"},{"PULSESHIFT_CAN_BITRATE":"123456"},
           {"PULSESHIFT_SERIAL_DIAGNOSTICS":"2"},{"PULSESHIFT_BLE_ENABLED":"2"}]
        for changes in cases:
            with self.subTest(changes=changes):self.assertNotEqual(self.compile_config(changes).returncode,0)

    def test_uart0_pin_conflicts(self):
        for key in ("PULSESHIFT_LED_DATA_GPIO","PULSESHIFT_CAN_RX_GPIO","PULSESHIFT_CAN_TX_GPIO"):
            for pin in ("1","3"):
                for diagnostics in ("0","1"):
                    changes={key:pin,"PULSESHIFT_SERIAL_DIAGNOSTICS":diagnostics}
                    with self.subTest(changes=changes):
                        result=self.compile_config(changes)
                        if diagnostics=="1":
                            self.assertNotEqual(result.returncode,0)
                            self.assertIn("reserve UART0",result.stderr)
                        else:self.assertEqual(result.returncode,0,result.stderr)

    def test_oled_settings_removed(self):
        """The OLED support surface must be gone from UserConfig.h entirely."""
        source=(ROOT/"PulseShift/UserConfig.h").read_text()
        for symbol in ("PULSESHIFT_OLED_ENABLED","PULSESHIFT_OLED_SDA_GPIO","PULSESHIFT_OLED_SCL_GPIO",
                       "SSD1306","Adafruit_SSD1306"):
            self.assertNotIn(symbol, source)
        # The mode matrix is gone too: one production path.
        for symbol in ("PULSESHIFT_OPERATING_MODE","PULSESHIFT_USER_MODE_DEMO",
                       "PULSESHIFT_USER_MODE_CAN_CAPTURE","PULSESHIFT_USER_MODE_VEHICLE_RPM"):
            self.assertNotIn(symbol, source)
