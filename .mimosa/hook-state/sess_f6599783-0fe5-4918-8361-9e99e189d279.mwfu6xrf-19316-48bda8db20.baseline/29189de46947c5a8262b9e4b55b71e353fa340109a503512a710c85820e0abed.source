# Copyright (c) 2026 Aritra Saha. All rights reserved.
"""All protocols and RPM values in this file are synthetic."""
import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import can_investigate as can

class DiscoveryTests(unittest.TestCase):
    def session(self, directory, name, reverse=False, corrupt=False):
        log, reference = directory / (name+'.log'), directory / (name+'.csv')
        rpms = [0, 1200, 2000, 3000, 4000, 2000, 1200, 0]*3
        if reverse: rpms = list(reversed(rpms))
        lines = ['PulseShift mode=CAN_CAPTURE SDK=SYNTHETIC']
        rows = ['timestamp_ms,rpm']
        for i, rpm in enumerate(rpms):
            raw = (i*100 if corrupt else rpm)*4
            payload = bytes([i])+raw.to_bytes(2,'little')+bytes([i%3])
            # Different session times and different order prevent duplicated evidence.
            t = 1000+i*100+(10 if reverse else 0)+(20 if corrupt else 0)
            lines.append(f'RX t={t} id=612 ext=0 rtr=0 dlc=4 data={payload.hex()} UNMAPPED')
            rows.append(f'{t+50},{rpm}')
        log.write_text('\n'.join(lines)+'\n')
        reference.write_text('\n'.join(rows)+'\n')
        return log, reference

    def test_independent_session_scores_frozen_fit_and_rejects_false_signal(self):
        with tempfile.TemporaryDirectory() as td:
            d = Path(td)
            train, ref = self.session(d,'train')
            hold, href = self.session(d,'hold',True)
            bad, bref = self.session(d,'bad',True,True)
            result, vectors = can.investigate(train,ref,offset=-50,skew=0,
                validation=[(hold,href,-50),(bad,bref,-50)])
            self.assertEqual(result['status'],'UNVERIFIED')
            first = result['independent_sessions'][0]['scores'][0]
            second = result['independent_sessions'][1]['scores'][0]
            self.assertEqual(first['rmse_rpm'],0)
            self.assertGreater(second['rmse_rpm'],500)
            self.assertEqual(first['missing_reference_samples'],0)
            c = result['candidates'][0]
            self.assertEqual((c['byte_offset'],c['width_bits'],c['scale']),(1,16,.25))
            self.assertTrue(all(v['status']=='UNVERIFIED' for v in vectors))
            can.write_report(d/'out',result,vectors)
            self.assertIn('Independent sessions',(d/'out/report.md').read_text())
            self.assertEqual(json.loads((d/'out/candidates.json').read_text())['status'],'UNVERIFIED')

    def test_holdout_does_not_select_scale(self):
        # Training fits .25 exactly; later samples fit .5. Never refit using them.
        frames = [can.Frame(i,i,1,False,False,1,bytes([i*8]),i) for i in range(18)]
        refs = [(i,i*2 if i<12 else i*4) for i in range(18)]
        field = next(c for c in can.rank_fields(frames,refs,0) if c['width_bits']==8)
        self.assertEqual(field['scale'],.25)
        self.assertGreater(field['validation_rmse_rpm'],0)

    def test_duplicate_sessions_and_copies_rejected(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);log,ref=self.session(d,'train')
            with self.assertRaisesRegex(ValueError,'reuse'):
                can.investigate(log,ref,validation=[(log,ref,0)])
            copy=d/'copy.log';copy.write_bytes(log.read_bytes())
            other=d/'other.csv';other.write_text(ref.read_text()+'9999,0\n')
            with self.assertRaisesRegex(ValueError,'duplicates'):
                can.investigate(log,ref,validation=[(copy,other,0)])

    def test_missing_samples_offsets_and_limits(self):
        with tempfile.TemporaryDirectory() as td:
            d=Path(td);log,ref=self.session(d,'train');hold,href=self.session(d,'hold',True)
            result,_=can.investigate(log,ref,offset=-50,skew=0,validation=[(hold,href,100000)])
            score=result['independent_sessions'][0]['scores'][0]
            self.assertIsNone(score['rmse_rpm'])
            self.assertIn('insufficient_samples',score['quality_flags'])
            with self.assertRaises(ValueError):can.read_reference(ref,0,2)
            with self.assertRaises(ValueError):can.investigate(log,None,validation=[(hold,href,0)])
            malformed=d/"malformed.csv";malformed.write_text("timestamp_ms,rpm\n100\n")
            with self.assertRaisesRegex(ValueError,"Incomplete"):can.read_reference(malformed,0)
            with self.assertRaises(ValueError):can.investigate(log,ref,skew=float('nan'))
            with self.assertRaises(ValueError):can.investigate(log,ref,validation=[(hold,href,0)]*17)
            noise=d/'noise.log';noise.write_text('ignored\n'*17)
            with self.assertRaisesRegex(ValueError,'Total log'):can.read_log(noise,1)
            noise.write_text('CAPTURE_SESSION t=0\nRX t=100 id=100 ext=0 rtr=0 dlc=1 data=00 VALID\nCAPTURE_SESSION t=200\n')
            with self.assertRaisesRegex(ValueError,'session boundary'):can.read_log(noise)

    def test_structural_hints_are_unverified(self):
        frames=[]
        for i in range(20):
            data=bytes([i,i%2,(i+i%2)&255])
            frames.append(can.Frame(i,i,1,False,False,3,data,i))
        hints=can.payload_hints(frames)
        self.assertEqual(hints['status'],'UNVERIFIED')
        self.assertIn(0,hints['counter_like_bytes'])
        self.assertIn(1,hints['small_domain_bytes'])
        self.assertIn({'byte':2,'kind':'sum8','seed':0},hints['checksum_hypotheses'])
        self.assertEqual(can.payload_hints([can.Frame(0,0,1,False,True,0,b'',0)]),{})

    def test_unsigned_32_bit_big_endian_is_ranked_without_vehicle_constants(self):
        rpms = [0,1201,6003,407,8009,2511]*4
        frames = [can.Frame(i,i,0x1ABCDE,True,False,4,
                  ((rpm+100)*40000).to_bytes(4,'big'),i) for i,rpm in enumerate(rpms)]
        fields = can.rank_fields(frames,list(enumerate(rpms)),0)
        best = fields[0]
        self.assertEqual((best['width_bits'],best['byte_order'],best['extended']),(32,'big',True))
        self.assertAlmostEqual(best['scale'],1/40000)
        self.assertAlmostEqual(best['offset_rpm'],-100)
        self.assertLess(best['validation_rmse_rpm'],1e-8)
        self.assertEqual(best['status'],'UNVERIFIED')
