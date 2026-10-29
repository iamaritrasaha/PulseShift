# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Exercise real verification scripts with recorded-tool fixtures, never real hardware."""
import hashlib,json,os,shutil,subprocess,sys,tempfile,unittest,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

class IsolationChecks(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        scripts=self.root/"scripts";scripts.mkdir()
        for name in ("verify_firmware_isolation.py","verify_arduino_matrix.py",
                     "verify_arduino_package.py","package_arduino.py"):
            shutil.copyfile(ROOT/"scripts"/name,scripts/name)
        self.pio=self.root/"pio"
        nm=self.pio/"packages/toolchain-xtensa-esp32/bin/xtensa-esp32-elf-nm"
        nm.parent.mkdir(parents=True)
        nm.write_text("""#!/usr/bin/env python3
import os,sys
path=sys.argv[-1];case=os.environ.get("ISOLATION_CASE","valid")
symbols="000 T setup"
if "esp32dev_twai_test" in path and case!="missing-test":
    symbols+="\\n000 T twai_transmit\\n000 T CanTransport::sendTest"
if case=="forbidden" and "esp32dev_bike" in path:
    symbols+="\\n000 T twai_transmit"
if case=="tx-queue" and "esp32dev_vehicle" in path:
    symbols+="\\n000 T twai_clear_transmit_queue"
if case=="recovery" and "esp32dev_bike" in path:
    symbols+="\\n000 T twai_initiate_recovery"
if case=="forbidden-matrix":symbols+="\\n000 T twai_transmit"
print(symbols)
""");nm.chmod(0o755)
        for name in ("esp32dev_wokwi","esp32dev_bike","esp32dev_vehicle","esp32dev_twai_test"):
            elf=self.root/".pio/build"/name/"firmware.elf"
            elf.parent.mkdir(parents=True);elf.write_bytes(b"fixture only")
        self.env=dict(os.environ,PLATFORMIO_CORE_DIR=str(self.pio))
        self.cli=self.root/"arduino-cli"
        self.cli.write_text("""#!/usr/bin/env python3
import os,sys
from pathlib import Path
build=Path(sys.argv[sys.argv.index("--build-path")+1]);build.mkdir(parents=True)
case=os.environ.get("ISOLATION_CASE","valid")
if case!="missing-elf":(build/"PulseShift.ino.elf").write_bytes(b"fixture only")
if case=="extra-elf":(build/"extra.elf").write_bytes(b"fixture only")
""");self.cli.chmod(0o755)

    def run_script(self,name,optimized,case,args=()):
        env=dict(self.env,ISOLATION_CASE=case)
        return subprocess.run([sys.executable]+(["-O"] if optimized else [])+
            [str(self.root/"scripts"/name),*map(str,args)],env=env,text=True,capture_output=True)

    def package(self,config_case="valid"):
        assets=self.root/"assets"
        subprocess.run([sys.executable,str(ROOT/"scripts/package_arduino.py"),
            "--version","v0.3.0","--output-dir",str(assets)],check=True,capture_output=True)
        archive=assets/"PulseShift-Arduino-v0.3.0.zip"
        if config_case!="valid":
            with zipfile.ZipFile(archive) as z:files={name:z.read(name) for name in z.namelist()}
            key="PulseShift/UserConfig.h"
            value=files[key].decode()
            definition="#define PULSESHIFT_OLED_ENABLED 0"
            self.assertEqual(value.count(definition),1)
            value=value.replace(definition,"" if config_case=="missing-config" else definition+"\n"+definition)
            files[key]=value.encode()
            with zipfile.ZipFile(archive,"w") as z:
                for name,content in files.items():z.writestr(name,content)
            archive.with_suffix(".sha256").write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+"  "+archive.name+"\n")
        return archive

    def test_pio_checks_cannot_be_optimized_away(self):
        for optimized in (False,True):
            for case,error in (("valid",None),("forbidden","Forbidden symbol"),("tx-queue","Forbidden symbol"),("recovery","Forbidden symbol"),("missing-test","Self-test lost")):
                with self.subTest(optimized=optimized,case=case):
                    result=self.run_script("verify_firmware_isolation.py",optimized,case)
                    if error:
                        self.assertNotEqual(result.returncode,0)
                        self.assertIn(error,result.stderr)
                    else:
                        self.assertEqual(result.returncode,0,result.stderr)
                        self.assertTrue(all(row["passed"] for row in json.loads(result.stdout)))

    def test_arduino_matrix_checks_cannot_be_optimized_away(self):
        for optimized in (False,True):
            for case,error in (("valid",None),("forbidden-matrix","mode isolation failed"),
                    ("missing-elf","Expected one linked"),("extra-elf","Expected one linked"),
                    ("missing-config","configuration definition"),("duplicate-config","configuration definition")):
                with self.subTest(optimized=optimized,case=case):
                    archive=self.package(case if "config" in case else "valid")
                    work=self.root/f"work-{optimized}-{case}"
                    result=self.run_script("verify_arduino_matrix.py",optimized,case,
                        ("--zip",archive,"--version","v0.3.0","--work-dir",work,"--arduino-cli",self.cli))
                    if error:
                        self.assertNotEqual(result.returncode,0)
                        self.assertIn(error,result.stderr)
                    else:
                        self.assertEqual(result.returncode,0,result.stderr)
                        rows=json.loads((work/"results.json").read_text())
                        self.assertEqual(len(rows),15)
                        self.assertTrue(all(row["binary_isolation"] for row in rows))

if __name__=="__main__":unittest.main()
