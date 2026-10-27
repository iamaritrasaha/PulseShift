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
        subprocess.run(["python3",str(ROOT/"scripts/package_arduino.py"),"--version","v0.2.0",
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
        cls.text=(ROOT/"RELEASE_NOTES.md").read_text()

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def invoke(self,tag="v0.2.0",text=None,assets=None,exit_code=0):
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
        self.assertEqual(notes.validate("v0.2.0",self.text),self.text)

    def test_nonpreview_and_malformed_tags_rejected(self):
        for tag in ("v1.0.0","v0.2.0-rc1","v0.02.0","0.2.0","v0.x.0","../v0.2.0"):
            with self.subTest(tag=tag):
                result,call=self.invoke(tag=tag)
                self.assertNotEqual(result.returncode,0)
                self.assertIsNone(call)

    def test_wrong_version_notes_rejected(self):
        result,call=self.invoke(text=self.text.replace("REVIX LINK v0.2.0 |","REVIX LINK v0.1.0 |",1))
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
        with (corrupt/"REVIX-LINK-Arduino-v0.2.0.zip").open("ab") as out: out.write(b"tampered")
        result,call=self.invoke(assets=corrupt)
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(call)

    def test_extra_checksum_record_rejected(self):
        corrupt=self.base/"extra-checksum"
        corrupt.mkdir(exist_ok=True)
        for path in self.assets.iterdir(): (corrupt/path.name).write_bytes(path.read_bytes())
        checksum=corrupt/"REVIX-LINK-Arduino-v0.2.0.sha256"
        checksum.write_text(checksum.read_text()+"0"*64+"  another-file.zip\n")
        result,call=self.invoke(assets=corrupt)
        self.assertNotEqual(result.returncode,0)
        self.assertIsNone(call)

    def test_publisher_uses_exact_notes_verified_tag_and_prerelease(self):
        result,call=self.invoke()
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertEqual(call["args"][:3],["release","create","v0.2.0"])
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

    def test_readme_patch_matches_current_or_postpublication_state(self):
        readme=(ROOT/"README.md").read_text()
        args=["git","apply","--unidiff-zero","--check"]
        if "releases/tag/v0.1.0" in readme:
            self.assertNotIn("releases/tag/v0.2.0",readme)
        else:
            self.assertIn("releases/tag/v0.2.0",readme)
            args.append("--reverse")
        args.append("scripts/release/README-v0.2.0.patch")
        result=subprocess.run(args,cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)

    def test_postpublication_patch_roundtrip_in_isolated_directory(self):
        work=self.base/"readme-roundtrip"
        work.mkdir(exist_ok=True)
        subprocess.run(["git","init","-q",str(work)],check=True)
        original=(ROOT/"README.md").read_bytes()
        (work/"README.md").write_bytes(original)
        patch=str(ROOT/"scripts/release/README-v0.2.0.patch")
        forward="releases/tag/v0.1.0" in original.decode()
        args=["git","apply","--unidiff-zero"]+([] if forward else ["--reverse"])+[patch]
        subprocess.run(args,cwd=work,check=True,capture_output=True)
        changed=(work/"README.md").read_text()
        self.assertIn("releases/tag/v0.2.0" if forward else "releases/tag/v0.1.0",changed)
        args=["git","apply","--unidiff-zero"]+(["--reverse"] if forward else [])+[patch]
        subprocess.run(args,cwd=work,check=True,capture_output=True)
        self.assertEqual((work/"README.md").read_bytes(),original)

if __name__=="__main__":
    unittest.main()
