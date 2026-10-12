from pathlib import Path
import json
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import gameplay_results

class PairedTests(unittest.TestCase):
    def test_pairs_and_rejected_checkpoint_or_build(self):
        with tempfile.TemporaryDirectory() as tmp:
            bundles=[]
            for i in range(5):
                b=Path(tmp)/str(i);(b/'results').mkdir(parents=True);bundles.append(b)
                arms=[0,1] if i%2==0 else [1,0]
                profile=dict(arms=arms,start=10,stop=20,media_hash='fixture')
                for name in ('boot.dol','boot.elf'):(b/name).write_text(name)
                manifest=dict(inputs={name:{'sha256':gameplay_results.bench_smoke.digest(b/name)} for name in ('boot.dol','boot.elf')},recording_sha256='input')
                rows=[dict(enabled=e,guest_state=['identical'],frames=10,
                           ram={'fails':0,'heap_ok':1},wall_ms=100+e*2,
                           p99_ms=11+e,over_budget=0,evaluation={'total_us':100,'calls':10}) for e in arms]
                for name,data in (('profile.json',profile),('manifest.json',manifest),
                                  ('results/measurements.json',rows),('results/cleanup.json',{'restored':True})):
                    (b/name).write_text(json.dumps(data))
            result=gameplay_results.compare(bundles)
            self.assertAlmostEqual(result['overhead_mean_percent'],2)
            self.assertEqual(result['provisional_one_percent_gate'],'above target')
            last=bundles[-1]/'results/measurements.json'
            rows=json.loads(last.read_text());rows[0]['guest_state']=['different'];last.write_text(json.dumps(rows))
            with self.assertRaisesRegex(ValueError,'checkpoint'):gameplay_results.compare(bundles)
            rows[0]['guest_state']=['identical'];last.write_text(json.dumps(rows))
            path=bundles[-1]/'manifest.json';m=json.loads(path.read_text());m['inputs']['boot.dol']['sha256']='changed';path.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'Frozen input'):gameplay_results.compare(bundles)
            with self.assertRaisesRegex(ValueError,'five'):gameplay_results.compare(bundles[:4])

if __name__=='__main__':unittest.main()
