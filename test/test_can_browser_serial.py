# Copyright (c) 2026 Aritra Saha. All rights reserved.
import json,unittest
from browser_serial import BrowserSerial
class FakeBrowserSerial(BrowserSerial):
    def __init__(self,snapshots):
        super().__init__("/unused","test");self.snapshots=iter(snapshots);self.commands=[]
    def call(self,*args):
        self.commands.append(args)
        return json.dumps(next(self.snapshots)) if args[0]=="--raw" else ""
class BrowserAdapterTests(unittest.TestCase):
    def test_reads_actual_ui_stream_without_inventing_records(self):
        s=FakeBrowserSerial(["RX line\r\nAPP partial","RX line\r\nAPP partial done\r\n"])
        self.assertEqual(s.readline(),b"RX line\n")
        self.assertEqual(s.readline(),b"APP partial done\n")
        s.write(b"rpm 4200\n")
        self.assertIn(":not([type])",s.commands[-1][1])
        self.assertIn("rpm 4200",s.commands[-1][1])
    def test_history_loss_is_a_failure(self):
        s=FakeBrowserSerial(["first\n","unrelated\n"])
        self.assertEqual(s.readline(),b"first\n")
        with self.assertRaises(RuntimeError):s.readline()
