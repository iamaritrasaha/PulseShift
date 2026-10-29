# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""Publication boundary tests use a fake gh binary and never contact GitHub."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("release_notes",ROOT/"scripts/prepare_release_notes.py")
notes=importlib.util.module_from_spec(spec)
spec.loader.exec_module(notes)

class ReleasePreparation(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        area=ROOT/".pio/release-guard-tests"
        area.mkdir(parents=True,exist_ok=True)
        cls.temp=tempfile.TemporaryDirectory(dir=area)
        cls.base=Path(cls.temp.name)
        cls.assets=cls.base/"assets"
        cls.text=(ROOT/"RELEASE_NOTES.md").read_text()
        cls.tag=cls.text.splitlines()[0].split(" | ",1)[0].split()[-1]
        notes.validate(cls.tag,cls.text)
        subprocess.run(["python3",str(ROOT/"scripts/package_arduino.py"),"--version",cls.tag,
                        "--output-dir",str(cls.assets)],check=True,capture_output=True)
        cls.bin=cls.base/"bin"
        cls.bin.mkdir()
        stub=cls.bin/"gh"
        stub.write_text("""#!/usr/bin/env python3
import json,os,sys
from pathlib import Path
args=sys.argv[1:]
body=Path(args[args.index('--notes-file')+1]).read_text() if '--notes-file' in args else ''
Path(os.environ['STUB_GH_LOG']).write_text(json.dumps({'args':args,'notes':body}))
sys.exit(int(os.environ.get('STUB_GH_EXIT','0')))
""")
        stub.chmod(0o755)
    
    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def invoke(self,tag=None,text=None,assets=None,exit_code=0):
        tag=tag or self.tag
        log=self.base/"gh-log.json"
        log.unlink(missing_ok=True)
        note_file=self.base/"notes.md"
        note_file.write_text(self.text if text is None else text)
        env=dict(os.environ,PATH=str(self.bin)+os.pathsep+os.environ["PATH"],
                 RUNNER_TEMP=str(self.base),STUB_GH_LOG=str(log),STUB_GH_EXIT=str(exit_code))
        result=subprocess.run(["bash",str(ROOT/"scripts/publish_prerelease.sh"),tag,
                    str(assets or self.assets),str(note_file)],cwd=ROOT,env=env,
                    text=True,capture_output=True)
        return result,json.loads(log.read_text()) if log.exists() else None

    def test_validated_notes_preserve_exact_body(self):
        self.assertEqual(notes.validate(self.tag,self.text),self.text)

    def test_nonpreview_and_malformed_tags_rejected(self):
        for tag in ("v1.0.0","v0.2.0-rc1","v0.02.0","0.2.0","v0.x.0","../v0.2.0"):
            with self.subTest(tag=tag):
                result,call=self.invoke(tag=tag)
                self.assertNotEqual(result.returncode,0)
                self.assertIsNone(call)

    def test_wrong_version_notes_rejected(self):
        result,call=self.invoke(text=self.text.replace(f"PulseShift {self.tag} |","PulseShift v0.1.0 |",1))
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(call)

    def test_legacy_brand_cannot_publish_a_new_release(self):
        result,call=self.invoke(text=self.text.replace("PulseShift","REVIX LINK",1))
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(call)

    def test_all_physical_and_twai_limitations_required(self):
        for statement in (*notes.QUALIFICATIONS,"Wokwi TWAI transmission completion remains unverified."):
            with self.subTest(statement=statement):
                result,call=self.invoke(text=self.text.replace(statement,""))
                self.assertNotEqual(result.returncode,0)
                self.assertIsNone(call)

    def test_corrupted_zip_stops_before_github(self):
        corrupt=self.base/"corrupt"
        corrupt.mkdir(exist_ok=True)
        for path in self.assets.iterdir(): (corrupt/path.name).write_bytes(path.read_bytes())
        with (corrupt/f"PulseShift-Arduino-{self.tag}.zip").open("ab") as out: out.write(b"tampered")
        result,call=self.invoke(assets=corrupt)
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(call)

    def test_extra_checksum_record_rejected(self):
        corrupt=self.base/"extra-checksum"
        corrupt.mkdir(exist_ok=True)
        for path in self.assets.iterdir(): (corrupt/path.name).write_bytes(path.read_bytes())
        checksum=corrupt/f"PulseShift-Arduino-{self.tag}.sha256"
        checksum.write_text(checksum.read_text()+"0"*64+"  another-file.zip\n")
        result,call=self.invoke(assets=corrupt)
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(call)

    def test_publisher_uses_exact_notes_verified_tag_and_prerelease(self):
        result,call=self.invoke()
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(call["args"][:3],["release","create",self.tag])
        for flag in ("--verify-tag","--prerelease","--latest=false"):
            self.assertIn(flag,call["args"])
        self.assertEqual(call["notes"],self.text)
        self.assertNotIn("--clobber",call["args"])

    def test_existing_release_failure_is_not_overwritten_or_retried(self):
        result,call=self.invoke(exit_code=1)
        self.assertNotEqual(result.returncode,0)
        self.assertEqual(call["args"][:2],["release","create"])
        self.assertNotIn("edit",call["args"])
        self.assertNotIn("--clobber",call["args"])

    def test_readme_points_to_verified_published_release(self):
        readme=(ROOT/"README.md").read_text()
        self.assertIn("https://github.com/iamaritrasaha/PulseShift/releases)",readme)
        self.assertIn("PulseShift-Arduino-v0.3.0.zip",readme)
        self.assertIn("PulseShift-Arduino-v0.3.0.sha256",readme)
        self.assertIn("PulseShift/PulseShift.ino",readme)
        self.assertIn("v0.3.0 is the current verified software prerelease.",readme)
        self.assertIn("/releases/tag/v0.3.0",readme)
        self.assertIn("/releases/download/v0.3.0/PulseShift-Arduino-v0.3.0.zip",readme)
        self.assertIn("/releases/download/v0.3.0/PulseShift-Arduino-v0.3.0.sha256",readme)
        self.assertNotIn("wait for publication",readme)

    def test_current_readme_has_exclusive_brand_and_preserves_diagrams(self):
        readme=(ROOT/"README.md").read_text()
        self.assertTrue(readme.startswith("# PulseShift\n"))
        self.assertNotRegex(readme,r"(?i)revix(?:[ _-]*link)?")
        for image in ("hardware-overview","main-wiring","led-direction",
                      "power-safety","expected-behavior"):
            path=f"assets/readme/{image}.svg"
            self.assertIn(path,readme)
            self.assertTrue((ROOT/path).is_file())
        for safety in ("GPIO27", "330Ω", "1000µF", "UNVERIFIED", "bitrate **0**"):
            self.assertIn(safety,readme)
        self.assertIn("Copyright (c) 2026 Aritra Saha. All rights reserved.",readme)

    def test_release_notes_are_concise_and_describe_passive_discovery(self):
        self.assertGreaterEqual(len(self.text.split()),100)
        self.assertLessEqual(len(self.text.split()),150)
        self.assertIn("independent-session",self.text)
        self.assertIn("profile evidence gates",self.text)
        self.assertNotIn("proposed",self.text.lower())

    def test_historical_v0_2_0_patch_roundtrip_in_isolated_directory(self):
        # Preserve the historical release-patch regression without assuming the
        # *current* README still contains the old versioned link.
        work=self.base/"readme-roundtrip"
        work.mkdir(exist_ok=True)
        patch=ROOT/"scripts/release/README-v0.2.0.patch"
        removed=[line[1:] for line in patch.read_text().splitlines()
                 if line.startswith("-") and not line.startswith("---")]
        self.assertEqual(len(removed),2)
        original=("".join(f"Historical README fixture line {i}\n" for i in range(1,9))
                  +removed[0]+"\n\n"+removed[1]+"\n").encode()
        (work/"README.md").write_bytes(original)
        args=["git","apply","--unidiff-zero",str(patch)]
        subprocess.run(args,cwd=work,check=True,capture_output=True)
        changed=(work/"README.md").read_text()
        self.assertIn("releases/tag/v0.2.0",changed)
        self.assertIn("REVIX-LINK-Arduino-v0.2.0.zip",changed)
        subprocess.run(["git","apply","--unidiff-zero","--reverse",str(patch)],
                       cwd=work,check=True,capture_output=True)
        self.assertEqual((work/"README.md").read_bytes(),original)

if __name__=="__main__":
    unittest.main()
