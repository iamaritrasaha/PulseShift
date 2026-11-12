# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Configuration compatibility and naming regressions, without hardware access."""
from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
@unittest.skipUnless(shutil.which("g++"), "g++ compiler not found in PATH")
class RebrandConfiguration(unittest.TestCase):
    def compile(self,flags=(),legacy=False,assertions="",standard="c++17"):
        with tempfile.TemporaryDirectory() as td:
            work=Path(td);(work/"include").mkdir()
            headers_dir = ROOT/"PulseShift" if (ROOT/"PulseShift").exists() else ROOT/"include"
            for source in headers_dir.glob("*.h"):
                shutil.copyfile(source,work/"include"/source.name)
            cfg_path = ROOT/"PulseShift/UserConfig.h" if (ROOT/"PulseShift/UserConfig.h").exists() else ROOT/"UserConfig.h"
            source=ROOT/("test/fixtures/legacy_UserConfig.h" if legacy else cfg_path)
            shutil.copyfile(source,work/"UserConfig.h")
            compat_path = ROOT/"PulseShift/ConfigCompat.h" if (ROOT/"PulseShift/ConfigCompat.h").exists() else ROOT/"ConfigCompat.h"
            shutil.copyfile(compat_path,work/"ConfigCompat.h")
            text='#include "include/config.h"\n'+assertions
            return subprocess.run(["g++",f"-std={standard}","-Wall","-Wextra","-Werror",
                "-pedantic","-I",str(work/"include"),"-x","c++","-fsyntax-only","-",*flags],
                input=text,text=True,cwd=work,capture_output=True)

    def test_saved_legacy_config_and_current_defaults_match(self):
        checks="""static_assert(config::ledCount==6 && config::ledPin==27);
static_assert(config::ledBrightness==40 && config::rxPin==16 && config::txPin==17);
static_assert(!config::oledEnabled && PULSESHIFT_MODE==1 && PULSESHIFT_BITRATE==0);
static_assert(PULSESHIFT_LED_COUNT==REVIX_LED_COUNT);
static_assert(config::startRpm==0 && config::shiftRpm==6000 && config::maxRpm==6500);
"""
        for legacy in (False,True):
            result=self.compile(legacy=legacy,assertions=checks)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_old_and_new_overrides_have_same_values(self):
        cases={"LED_COUNT":"8","LED_DATA_GPIO":"26","LED_BRIGHTNESS":"80",
               "OPERATING_MODE":"3","SHIFT_LIGHT_STYLE":"2","LED_DIRECTION":"2",
               "STARTUP_ANIMATION_ENABLED":"0","SMOOTH_TRANSITIONS_ENABLED":"0",
               "SHIFT_ALERT_MODE":"3","DEMO_START_RPM":"1600","DEMO_SHIFT_RPM":"5900",
               "DEMO_MAX_RPM":"6600","ALERT_HYSTERESIS_RPM":"100",
               "ANIMATION_INTERVAL_MS":"50","IDLE_EFFECT_ENABLED":"1","IDLE_MAX_RPM":"1200",
               "OLED_ENABLED":"1","OLED_SDA_GPIO":"25","OLED_SCL_GPIO":"26",
               "CAN_RX_GPIO":"32","CAN_TX_GPIO":"33","CAN_BITRATE":"250000",
               "SERIAL_DIAGNOSTICS":"0","SERIAL_BAUD":"57600","MODE":"4",
               "DASHBOARD":"0","BITRATE":"500000"}
        for suffix,value in cases.items():
            for prefix in ("REVIX_","PULSESHIFT_"):
                with self.subTest(suffix=suffix,prefix=prefix):
                    result=self.compile([f"-D{prefix}{suffix}={value}"],
                        assertions=f"static_assert(PULSESHIFT_{suffix}=={value});\n")
                    self.assertEqual(result.returncode,0,result.stderr)

    def test_arduino_cpp11_and_maximum_palettes(self):
        palette=",".join(["{0xFF,0xFF,0xFF}"]*32)
        spaced=", ".join(["{0xFF, 0xFF, 0xFF}"]*32)
        for flags in ([],["-DREVIX_CUSTOM_COLORS="+palette],
                      ["-DREVIX_CUSTOM_COLORS="+palette,"-DPULSESHIFT_CUSTOM_COLORS="+spaced]):
            result=self.compile(flags,standard="gnu++11")
            self.assertEqual(result.returncode,0,result.stderr)

    def test_legacy_symbolic_mode_choices(self):
        for old,new in (("REVIX_REPLAY","PULSESHIFT_REPLAY"),
                        ("REVIX_TWAI_TEST","PULSESHIFT_TWAI_TEST"),
                        ("REVIX_CAPTURE","PULSESHIFT_CAPTURE"),
                        ("REVIX_BIKE","PULSESHIFT_BIKE"),
                        ("REVIX_VEHICLE","PULSESHIFT_VEHICLE")):
            result=self.compile([f"-DREVIX_MODE={old}"],
                assertions=f"static_assert(PULSESHIFT_MODE=={new});")
            self.assertEqual(result.returncode,0,result.stderr)

    def test_conflicting_numeric_definitions_are_rejected(self):
        for suffix in ("LED_COUNT","LED_BRIGHTNESS","OPERATING_MODE","OLED_ENABLED",
                       "SERIAL_DIAGNOSTICS","MODE","BITRATE","DEMO_MAX_RPM"):
            with self.subTest(suffix=suffix):
                result=self.compile([f"-DREVIX_{suffix}=1",f"-DPULSESHIFT_{suffix}=2"])
                self.assertNotEqual(result.returncode,0)
                self.assertIn("Conflicting",result.stderr)
        self.assertEqual(self.compile(["-DREVIX_LED_COUNT=6","-DPULSESHIFT_LED_COUNT=6"]).returncode,0)

    def test_legacy_style_and_palette_compatibility(self):
        cases=[["-DREVIX_SHIFT_LIGHT_STYLE=REVIX_STYLE_RACING"],
               ["-DREVIX_CUSTOM_COLORS={1,2,3}"],
               ["-DREVIX_CUSTOM_COLORS={1,2,3}","-DPULSESHIFT_CUSTOM_COLORS={1, 2, 3}"]]
        for flags in cases:
            result=self.compile(flags)
            self.assertEqual(result.returncode,0,result.stderr)
        result=self.compile(["-DREVIX_CUSTOM_COLORS={1,2,3}","-DPULSESHIFT_CUSTOM_COLORS={3,2,1}"])
        self.assertNotEqual(result.returncode,0)
        self.assertIn("Conflicting legacy/current custom palettes",result.stderr)

    def test_old_mode_flags_cannot_link_replay_into_receive_only(self):
        with tempfile.TemporaryDirectory() as td:
            for mode in (3,4):
                obj=Path(td)/f"legacy-{mode}.o"
                headers_dir = ROOT/"PulseShift" if (ROOT/"PulseShift").exists() else ROOT/"include"
                src_file = ROOT/"PulseShift/simulated_ecu.cpp" if (ROOT/"PulseShift/simulated_ecu.cpp").exists() else ROOT/"src/simulated_ecu.cpp"
                subprocess.run(["g++","-std=c++17","-Wall","-Wextra","-Werror",
                    f"-DREVIX_MODE={mode}","-I",str(headers_dir),"-c",
                    str(src_file),"-o",str(obj)],check=True)
                symbols=subprocess.check_output(["nm","-C",str(obj)],text=True)
                self.assertNotIn("ReplayTransport",symbols)
                self.assertNotIn("demonstrationFrame",symbols)

# RebrandPackage tests removed: package_arduino.py and verify_arduino_package.py
# scripts removed in Arduino IDE simplification. The sketch itself is the package.

if __name__=="__main__":unittest.main()
