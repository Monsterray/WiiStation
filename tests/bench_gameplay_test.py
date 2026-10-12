"""Regression checks for the gameplay measurement gates and replay exit status."""
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import bench_gameplay

class MeasurementTests(unittest.TestCase):
    def test_read_retry_is_bounded(self):
        client=Mock(HBCError=RuntimeError)
        operation=Mock(side_effect=[ConnectionResetError(), b'collected'])
        with patch.object(bench_gameplay.time,'sleep'):
            self.assertEqual(bench_gameplay.read_result(client,'wii',operation,'path'),b'collected')
            client.hbc_wait.assert_called_once_with('wii',30)
            operation=Mock(side_effect=ConnectionResetError())
            with self.assertRaises(ConnectionResetError):bench_gameplay.read_result(client,'wii',operation,'path')
            self.assertEqual(operation.call_count,3)

    def test_freeze_defers_debug_dump_without_changing_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp)
            for name in ('boot.dol','boot.elf','hbc.py','disc.cue'): (root/name).write_text('fixture')
            recording='1 0000\n5 4000\n6 0000\n'
            (root/'play.txt').write_text(recording)
            profile=dict(serial='SCUS94228',directory='sd:/wiistation/isos/Spyro',
                         cue='Spyro.cue',definitions=128,start=4,stop=10,arms=[0,1])
            (root/'profile.json').write_text(json.dumps(profile))
            script=Path(bench_gameplay.__file__)
            subprocess.run([sys.executable,str(script),'prepare','--dol',str(root/'boot.dol'),
                            '--client',str(root/'hbc.py'),'--profile',str(root/'profile.json'),
                            '--input',str(root/'play.txt'),'--cue',str(root/'disc.cue'),
                            '--bundle',str(root/'frozen')],check=True,capture_output=True)
            self.assertEqual((root/'frozen/play.txt').read_text(),'dump 11\n'+recording)
            manifest=json.loads((root/'frozen/manifest.json').read_text())
            self.assertEqual(manifest['recording_sha256'],bench_gameplay.bench_smoke.digest(root/'play.txt'))
            for name,entry in manifest['inputs'].items():
                self.assertEqual(entry['sha256'],bench_gameplay.bench_smoke.digest(root/'frozen'/name))

    def test_profile_rejects_path_escape(self):
        profile=dict(serial='SCUS94228',directory='sd:/wiistation/isos/Spyro',
                     cue='Spyro.cue',definitions=128,start=6000,stop=13200,arms=[0,1,0])
        bench_gameplay.validate_profile(profile)
        for key,value in (('serial','../card'),('directory','sd:/wiistation/isos/../saves'),
                          ('cue','../card'),('definitions',129),('arms',[2])):
            with self.assertRaises(ValueError): bench_gameplay.validate_profile(dict(profile,**{key:value}))

    def test_exact_boundary_and_rejected_bad_evidence(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            log = ('--- perf frames=3 ---\nachperf: calls=9 total_us=90 max_us=12 vsig=0\n'
                   'wall: wall_us=100000 vblanks=9\n'
                   'ram: mem2=100/1000KB peak=200KB fails=0 heap_ok=1\n'
                   'achievement-bench: game=TEST00001 frames=10 count=128 earned=0 enabled=1\n'
                   '=== chain 1/1 end vblanks=10 rom=test.cue ===\n')
            (folder/'perf.log').write_text(log)
            timeline = struct.pack('>III', 0x56544c32, 9, 8) + struct.pack('>7H', 1000, 900, 0, 0, 0, 1, 0)*8
            (folder/'vtl_01.bin').write_bytes(timeline)
            check = lambda: bench_gameplay.summarize(folder, 4, 10, 1, 128, 'TEST00001')
            self.assertEqual(check()['frames'], 6)
            for wrong in (log.replace('vblanks=9', 'vblanks=8'),
                          log.replace('calls=9', 'calls=8'),
                          log.replace('vsig=0', 'vsig=1'),
                          log.replace('peak=200', 'peak=2000'),
                          log.replace('earned=0', 'earned=1'),
                          log.replace('=== chain', 'incomplete')):
                (folder/'perf.log').write_text(wrong)
                with self.assertRaises(RuntimeError): check()
            (folder/'perf.log').write_text(log)
            (folder/'vtl_01.bin').write_bytes(struct.pack('>III', 0x56544c32, 9, 7)+timeline[12:-14])
            with self.assertRaisesRegex(RuntimeError, 'Truncated'): check()
            (folder/'vtl_01.bin').write_bytes(timeline[:12]+struct.pack('>7H',65535,0,0,0,0,0,0)*8)
            with self.assertRaisesRegex(RuntimeError, 'saturated'): check()

    def test_replay_all_still_fails_on_difference(self):
        with tempfile.TemporaryDirectory() as tmp:
            a,b = Path(tmp)/'a',Path(tmp)/'b'
            text='wall: wall_us=1000 vblanks=20\nirq: sio=3\ngpupres: x=0\n'
            a.write_text(text); b.write_text(text.replace('sio=3','sio=4'))
            script=Path(bench_gameplay.__file__).with_name('replay_check.py')
            run=lambda: subprocess.run([sys.executable,str(script),str(a),str(b),'--all'],capture_output=True).returncode
            self.assertEqual(run(),1)
            b.write_text(text); self.assertEqual(run(),0)
            b.write_text(text.replace('vblanks=20','vblanks=21')); self.assertEqual(run(),2)

if __name__=='__main__': unittest.main()
