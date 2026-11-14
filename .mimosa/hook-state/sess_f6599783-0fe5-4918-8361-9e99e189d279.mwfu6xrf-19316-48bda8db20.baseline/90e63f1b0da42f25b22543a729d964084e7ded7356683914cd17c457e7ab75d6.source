# Copyright (c) 2026 Aritra Saha. All rights reserved.
import sys, unittest, tempfile, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/"tools"))
import can_investigate as can
class InvestigationTests(unittest.TestCase):
    def test_fixture_analysis_and_regression_export(self):
        result,vectors=can.investigate(ROOT/"test/fixtures/synthetic_capture.log",
            ROOT/"test/fixtures/synthetic_rpm.csv",skew=30,top=5)
        self.assertEqual(result["status"],"UNVERIFIED")
        self.assertTrue(result["synthetic_input"])
        self.assertEqual(result["frames"],253)
        self.assertEqual(len(result["invalid_log_records"]),1)
        top=result["candidates"][0]
        self.assertEqual((top["id"],top["extended"],top["byte_offset"],top["width_bits"],top["byte_order"]),("00000612",False,2,16,"little"))
        self.assertAlmostEqual(top["scale"],.25)
        self.assertAlmostEqual(top["offset_rpm"],-100)
        self.assertLess(top["validation_rmse_rpm"],1e-8)
        self.assertEqual(top["aligned_reference_samples"],84)
        self.assertEqual(top["validation_samples"],28)
        self.assertEqual(top["max_alignment_skew_ms"],20)
        self.assertEqual(len(vectors),420)
        with tempfile.TemporaryDirectory() as td:
            can.write_report(Path(td),result,vectors)
            self.assertIn("UNVERIFIED",(Path(td)/"report.md").read_text())
            exported=json.loads((Path(td)/"regression_vectors.json").read_text())
            self.assertEqual(exported["status"],"UNVERIFIED")
            self.assertEqual(exported["vectors"][0]["reference_rpm"],0)
    def test_regression_export_is_bounded_and_covers_the_recording(self):
        result,vectors=can.investigate(ROOT/"test/fixtures/synthetic_capture.log",
            ROOT/"test/fixtures/synthetic_rpm.csv",skew=30,top=3,vector_limit=2)
        self.assertEqual(len(vectors),6)
        first=[v for v in vectors if v["candidate_rank"]==1]
        self.assertEqual([v["timestamp_ms"] for v in first],[1000,9300])
        self.assertEqual(result["candidates"][0]["exported_vector_count"],2)
    def test_invalid_envelopes_and_malformed_records(self):
        text="\n".join(["RX t=1 id=00000800 ext=0 rtr=0 dlc=1 data=00 UNMAPPED",
           "RX t=2 id=20000000 ext=1 rtr=0 dlc=1 data=00 UNMAPPED",
           "RX t=3 id=00000100 ext=0 rtr=0 dlc=9 data=000000000000000000 UNMAPPED",
           "RX t=4 id=00000100 ext=0 rtr=0 dlc=2 data=00 UNMAPPED",
           "RX t=5 id=00000100 ext=0 rtr=0 dlc=1 data=XY UNMAPPED",
           "RX t=6 id=00000100 ext=0 rtr=1 dlc=2 data= RTR"])
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"input.log";p.write_text(text)
            frames,issues,_,_=can.read_log(p)
            self.assertEqual(len(frames),1);self.assertEqual(len(issues),5)
    def test_wrap_and_reset_are_distinct(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"input.log"
            p.write_text("RX t=4294967290 id=100 ext=0 rtr=0 dlc=1 data=00 VALID\nRX t=4 id=100 ext=0 rtr=0 dlc=1 data=01 VALID\n")
            frames,*_=can.read_log(p)
            self.assertEqual(frames[1].timestamp_ms-frames[0].timestamp_ms,10)
            p.write_text("RX t=100 id=100 ext=0 rtr=0 dlc=1 data=00 VALID\nRX t=4 id=100 ext=0 rtr=0 dlc=1 data=01 VALID\n")
            with self.assertRaises(ValueError):can.read_log(p)
    def test_startup_banner_splits_sessions_even_near_wrap(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"input.log"
            banner="PulseShift mode=CAN_CAPTURE SDK=v4.4.7-dirty\n"
            frame="RX t={} id=100 ext=0 rtr=0 dlc=1 data=00 UNMAPPED\n"
            p.write_text(banner+frame.format(1))
            self.assertEqual(len(can.read_log(p)[0]),1)
            for before,after in ((4294967200,10),(100,10),(100,200)):
                p.write_text(banner+frame.format(before)+banner+frame.format(after))
                with self.subTest(before=before,after=after):
                    with self.assertRaisesRegex(ValueError,"session boundary"):can.read_log(p)

    def test_both_brands_and_mixed_session_boundaries(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"input.log"
            frame="RX t={} id=100 ext=0 rtr=0 dlc=1 data=00 UNMAPPED\n"
            banners=("PulseShift mode=CAN_CAPTURE SDK=test\n",
                     "REVIX LINK mode=CAN_CAPTURE SDK=test\n")
            for first in banners:
                p.write_text(first+frame.format(10))
                self.assertEqual(len(can.read_log(p)[0]),1)
                for second in banners:
                    p.write_text(first+frame.format(4294967200)+second+frame.format(10))
                    with self.subTest(first=first,second=second):
                        with self.assertRaisesRegex(ValueError,"session boundary"):can.read_log(p)

    def test_rx_budget_includes_invalid_records(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"input.log"
            valid="RX t=1 id=100 ext=0 rtr=0 dlc=1 data=00 UNMAPPED\n"
            invalid="RX malformed\n"
            for text in (invalid*3, valid+invalid*2):
                p.write_text(text)
                with self.assertRaisesRegex(ValueError,"RX records"):can.read_log(p,2)
            p.write_text(valid+invalid)
            frames,issues,*_=can.read_log(p,2)
            self.assertEqual((len(frames),len(issues)),(1,1))

    def test_log_line_length_is_bounded(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"input.log";p.write_text("RX "+("0"*10000))
            with self.assertRaisesRegex(ValueError,"4096"):can.read_log(p)

    def test_alignment_caps_density_and_rejects_large_skew(self):
        frames=[can.Frame(t,t,1,False,False,1,b"\0",i) for i,t in enumerate((90,99,100,101,110,300))]
        aligned=can.align(frames,[(100,1400)],20)
        self.assertEqual(len(aligned),1);self.assertEqual(aligned[0][0].timestamp_ms,100)
        self.assertEqual(can.align(frames,[(1000,1400)],20),[])
        self.assertEqual(can.rank_fields(frames,[(100,0)]*6,100),[])
    def test_reference_validation_and_frame_limit(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td)/"ref.csv"
            for text in ("time,rpm\n1,2\n","timestamp_ms,rpm\n1,nan\n","timestamp_ms,rpm\n2,3\n1,4\n"):
                p.write_text(text)
                with self.assertRaises(ValueError):can.read_reference(p,0)
            with self.assertRaises(ValueError):can.read_log(ROOT/"test/fixtures/synthetic_capture.log",10)
    def test_counter_or_single_direction_correlation_is_not_certification(self):
        result,_=can.investigate(ROOT/"test/fixtures/synthetic_capture.log",None)
        self.assertFalse(result["candidates"])
        frames=[can.Frame(t,t,1,False,False,1,bytes([t]),t) for t in range(20)]
        self.assertEqual(can.rank_fields(frames,[(t,1000) for t in range(20)],0),[])
        refs=[(t,float(t*100)) for t in range(20)]
        candidates=can.rank_fields(frames,refs,0)
        self.assertTrue(candidates[0]["counter_like"])
        self.assertEqual(candidates[0]["status"],"UNVERIFIED")
if __name__=="__main__":unittest.main()
