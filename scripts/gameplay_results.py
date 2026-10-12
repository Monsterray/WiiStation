#!/usr/bin/env python3
"""Summarize five alternating A/B pairs; one pair, not one frame, is a replicate."""
import argparse
import json
from pathlib import Path
import statistics
import math
import bench_smoke


def compare(bundles):
    if len(bundles) != 5:
        raise ValueError('Provide exactly five independent paired bundles')
    identity = None
    checkpoint = None
    pairs = []
    for index, bundle in enumerate(bundles):
        manifest = json.loads((bundle/'manifest.json').read_text())
        for name, entry in manifest['inputs'].items():
            if bench_smoke.digest(bundle/name) != entry['sha256']:
                raise ValueError('Frozen input changed: '+name)
        profile = json.loads((bundle/'profile.json').read_text())
        rows = json.loads((bundle/'results/measurements.json').read_text())
        if not json.loads((bundle/'results/cleanup.json').read_text()).get('restored'):
            raise ValueError('Cleanup not verified')
        expected = [0,1] if index % 2 == 0 else [1,0]
        if profile['arms'] != expected or [r['enabled'] for r in rows] != expected:
            raise ValueError('Expected alternating AB/BA pairs')
        normalized = dict(profile); normalized.pop('arms')
        current = (normalized, manifest['inputs']['boot.dol']['sha256'],
                   manifest['inputs']['boot.elf']['sha256'], manifest['recording_sha256'])
        if identity is not None and current != identity:
            raise ValueError('Different build/media/settings/recording')
        identity = current
        for row in rows:
            if not row['guest_state'] or (checkpoint is not None and row['guest_state'] != checkpoint):
                raise ValueError('Different or missing guest checkpoint')
            checkpoint = row['guest_state']
            if (row['ram'].get('fails') != 0 or row['ram'].get('heap_ok') != 1 or
                row['frames'] != profile['stop'] - profile['start']):
                raise ValueError('Invalid memory/completion evidence')
        a = next(r for r in rows if r['enabled'] == 0)
        b = next(r for r in rows if r['enabled'] == 1)
        pairs.append(dict(bundle=str(bundle),
                          disabled_wall_ms=a['wall_ms'], enabled_wall_ms=b['wall_ms'],
                          added_us_per_interval=(b['wall_ms']-a['wall_ms'])*1000/a['frames'],
                          overhead_percent=100*(b['wall_ms']/a['wall_ms']-1),
                          disabled_p99_ms=a['p99_ms'],enabled_p99_ms=b['p99_ms'],
                          disabled_over_budget=a['over_budget'],enabled_over_budget=b['over_budget'],
                          evaluation_us=b['evaluation']['total_us']/b['evaluation']['calls']))
    values = [p['overhead_percent'] for p in pairs]
    mean=statistics.mean(values)
    # Student t, df=4. Approximate normality across independent paired differences;
    # no inference treats the correlated intervals as independent observations.
    margin=2.776445105*statistics.stdev(values)/math.sqrt(5)
    return dict(pairs=pairs,overhead_mean_percent=mean,
                overhead_median_percent=statistics.median(values),
                overhead_range_percent=[min(values),max(values)],
                paired_mean_approx_95_ci_percent=[mean-margin,mean+margin],
                final_guest_checkpoint=checkpoint,
                provisional_one_percent_gate='above target' if mean-margin>1 else
                    ('below target' if mean+margin<1 else 'inconclusive'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundles',type=Path,nargs='+')
    args=parser.parse_args()
    print(json.dumps(compare(args.bundles),indent=2))

if __name__=='__main__':main()
