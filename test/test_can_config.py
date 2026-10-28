# Copyright (c) 2026 Aritra Saha. All rights reserved.
import subprocess,tempfile,re,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
class ConfigurationTests(unittest.TestCase):
    def compile_config(self,changes):
        with tempfile.TemporaryDirectory() as td:
            path=Path(td);source=(ROOT/"UserConfig.h").read_text()
            for key,value in changes.items():
                source,count=re.subn(rf"(?m)^(#define {key} )[^\n]*",rf"\g<1>{value}",source)
                self.assertEqual(count,1)
            (path/"UserConfig.h").write_text(source)
            return subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror","-x","c++","-fsyntax-only","-"],input='#include "UserConfig.h"\n',text=True,cwd=path,capture_output=True)
    def test_valid_modes_keep_pin_defaults(self):
        for mode in ("REVIX_USER_MODE_DEMO","REVIX_USER_MODE_CAN_CAPTURE","REVIX_USER_MODE_BENCH_LISTEN_ONLY","REVIX_USER_MODE_VEHICLE_RPM"):
            with self.subTest(mode=mode):
                self.assertEqual(self.compile_config({"REVIX_OPERATING_MODE":mode}).returncode,0)
    def test_conflicts_and_invalid_settings_rejected(self):
        cases=[{"REVIX_LED_DATA_GPIO":"16"},{"REVIX_LED_DATA_GPIO":"17"},
           {"REVIX_LED_DATA_GPIO":"20"},{"REVIX_LED_COUNT":"0"},{"REVIX_LED_COUNT":"33"},
           {"REVIX_CAN_RX_GPIO":"17"},{"REVIX_CAN_BITRATE":"123456"},
           {"REVIX_OLED_ENABLED":"1","REVIX_OLED_SDA_GPIO":"27"},
           {"REVIX_OPERATING_MODE":"2"},{"REVIX_LED_BRIGHTNESS":"256"}]
        for changes in cases:
            with self.subTest(changes=changes):self.assertNotEqual(self.compile_config(changes).returncode,0)

    def test_uart0_pin_conflicts(self):
        for key in ("REVIX_LED_DATA_GPIO","REVIX_CAN_RX_GPIO","REVIX_CAN_TX_GPIO",
                    "REVIX_OLED_SDA_GPIO","REVIX_OLED_SCL_GPIO"):
            for pin in ("1","3"):
                for diagnostics in ("0","1"):
                    changes={key:pin,"REVIX_SERIAL_DIAGNOSTICS":diagnostics,"REVIX_OLED_ENABLED":"1"}
                    with self.subTest(changes=changes):
                        result=self.compile_config(changes)
                        if diagnostics=="1":
                            self.assertNotEqual(result.returncode,0)
                            self.assertIn("reserve UART0",result.stderr)
                        else:self.assertEqual(result.returncode,0,result.stderr)
        for key in ("REVIX_OLED_SDA_GPIO","REVIX_OLED_SCL_GPIO"):
            self.assertEqual(self.compile_config({key:"1","REVIX_OLED_ENABLED":"0"}).returncode,0)

    def test_synthetic_ecu_is_not_even_compiled_in_receive_only_modes(self):
        with tempfile.TemporaryDirectory() as td:
            for mode in (3,4):
                obj=Path(td)/f"replay-{mode}.o"
                subprocess.run(["g++","-std=c++17",f"-DREVIX_MODE={mode}","-I",str(ROOT/"include"),"-c",str(ROOT/"src/simulated_ecu.cpp"),"-o",str(obj)],check=True)
                symbols=subprocess.check_output(["nm","-C",str(obj)],text=True)
                self.assertNotIn("ReplayTransport",symbols)
                self.assertNotIn("demonstrationFrame",symbols)
