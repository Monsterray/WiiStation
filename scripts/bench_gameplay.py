#!/usr/bin/env python3
"""Frozen real-game pilot through the shared bench lease; private cards/progress."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import shutil
import statistics
import time
import uuid
import bench_smoke
from wii_targets import bench_wii
import chain_table
import vtl_view


def read_result(client, wii, operation, *args):
    # HBC may reset a connection while returning from the emulator. Retry only
    # reads, with a bounded wait; never repeat a launch or a destructive request.
    for attempt in range(3):
        try:
            return operation(wii, *args)
        except (OSError, client.HBCError):
            if attempt == 2: raise
            time.sleep(1)
            client.hbc_wait(wii, 30)  # Returns only for HBC, never an app agent.


def validate_profile(profile):
    if (not re.fullmatch(r'[A-Z]{4}\d{5}', profile['serial']) or
        not profile['directory'].startswith('sd:/wiistation/isos/') or
        '..' in profile['directory'] or '\n' in profile['directory'] or
        any(c in profile['cue'] for c in '/\\\r\n') or
        not 1 <= profile['definitions'] <= 128 or
        not 2 <= profile['start'] < profile['stop'] <= 65536 or
        not profile['arms'] or any(x not in (0, 1) for x in profile['arms'])):
        raise ValueError('Invalid private gameplay profile')


def summarize(folder, start, stop, enabled, definitions, serial):
    log = (folder / 'perf.log').read_text(errors='replace')
    rows = re.findall(r'achievement-bench: game=(\w+) frames=(\d+) count=(\d+) earned=(\d+) enabled=(\d+)', log)
    if rows != [(serial, str(stop), str(definitions if enabled else 0), '0', str(enabled))]:
        raise RuntimeError('Wrong game/completion or unexpected unlock: ' + str(rows))
    if not re.search(rf'=== chain 1/1 end vblanks={stop}\b', log):
        raise RuntimeError('Missing complete chain marker')
    games = chain_table.games(folder / 'perf.log')
    # HLE boot completes guest frame 1 before go() resets profiler counters.
    executed = stop - 1
    if len(games) != 1 or games[0][3].get('wall', {}).get('vblanks') != executed:
        raise RuntimeError('Missing independent executed frame total')
    b = games[0][3]
    ram=b.get('ram',{})
    used,total=ram.get('mem2',(0,0))
    if (ram.get('fails') != 0 or ram.get('heap_ok') != 1 or
        not used <= ram.get('peak',0) <= total or total == 0):
        raise RuntimeError('Invalid memory/profiler evidence; check matching dependency builds')
    calls = b.get('achperf', {}).get('calls')
    if calls != (executed if enabled else 0) or b.get('achperf', {}).get('vsig') != 0:
        raise RuntimeError('Wrong evaluation count or expensive signatures enabled')
    rows = vtl_view.load(folder / 'vtl_01.bin')
    if len(rows) != stop - 2 or start < 2 or start >= stop:
        raise RuntimeError('Truncated timeline')
    # The first profiled VBlank establishes the clock; subsequent ticks record
    # intervals. Sample VBlankStart events start..stop-1 (stop is frame end).
    sample = rows[start - 2:stop - 2]
    if not sample or any(r[0] >= 550 for r in sample):
        raise RuntimeError('Empty or saturated timeline; preserve exact stalls before measuring')
    wall = sorted(r[0] for r in sample)
    def percentile(p): return wall[int((len(wall) - 1) * p)]
    return {'enabled': enabled, 'definitions': definitions, 'scene_start': start, 'scene_stop': stop,
            'frames': len(sample), 'wall_ms': sum(wall), 'median_ms': statistics.median(wall),
            'p95_ms': percentile(.95), 'p99_ms': percentile(.99),
            'over_budget': sum(x > 1000 / 59.94 for x in wall),
            'ram': b.get('ram', {}), 'evaluation': b.get('achperf', {}),
            'guest_state': re.findall(r'^state:.*$', log, re.M)[-1:]}


def execute(client, wii, bundle, inventory=False):
    profile = json.loads((bundle / 'profile.json').read_text())
    validate_profile(profile)
    output = bundle / 'results'; output.mkdir(exist_ok=False)
    before = client.status(wii)
    (output / 'before.json').write_text(json.dumps(before, indent=2))
    if before.get('agent'): raise RuntimeError('Expected idle HBC')
    media, cut = client.list_dir(wii, profile['directory'])
    if cut: raise RuntimeError('Incomplete game listing')
    (output / 'media.json').write_text(json.dumps(media, indent=2))
    if profile['cue'] not in {n for _, _, n in media}: raise RuntimeError('Game image is not on bench SD')
    if inventory: return
    # Freeze/compare the small cue bytes; full logical media identity is a separate hash gate.
    cue = client.get_file(wii, profile['directory'] + '/' + profile['cue'])
    (output / 'disc.cue').write_bytes(cue)
    if cue != (bundle / 'disc.cue').read_bytes(): raise RuntimeError('Bench CUE differs from frozen input')
    if 'usb' in before.get('inserted', []): raise RuntimeError('First pilot requires SD-only settings precedence')
    remote = 'sd:/wiistation/bench-' + uuid.uuid4().hex[:12]
    (output / 'remote.txt').write_text(remote)
    names = ['autoboot.txt','perf.log','tty.log','emu.log','ptrace.log','atrace.log','vramio.log',
             'padtrace.csv','crumb.log','hb.log','vram_01.bin','vtl_01.bin','vsig_01.bin']
    entries, cut = client.list_dir(wii, 'sd:/wiistation')
    if cut: raise RuntimeError('Incomplete preflight listing')
    existing = {n for _, _, n in entries}
    backups = {}
    paths = ['sd:/wiistation/' + n for n in names]
    settings = 'sd:/wiistation/settings/' + profile['serial'] + '.ini'
    if 'settings' not in existing: client.file_request(wii, 'M', 'sd:/wiistation/settings')
    configs, cut = client.list_dir(wii, 'sd:/wiistation/settings')
    if cut: raise RuntimeError('Incomplete settings listing')
    if profile['serial'] + '.ini' in {n for _, _, n in configs}: paths.append(settings)
    for path in paths:
        if path == settings or path.rsplit('/',1)[1] in existing:
            backups[path] = client.get_file(wii, path)
    for i,(path,data) in enumerate(backups.items()):
        f=output/f'backup-{i}'; f.write_bytes(data); f.chmod(0o600)
    (output/'backup-paths.json').write_text(json.dumps(list(backups)))
    client.file_request(wii, 'M', remote)
    client.file_request(wii, 'M', remote + '/cards')
    launched=False
    try:
        client.put_file(wii, remote + '/play.txt', (bundle/'play.txt').read_bytes())
        all_results=[]
        for run_index, enabled in enumerate(profile['arms']):
            result=output/f'run-{run_index+1:02d}'; result.mkdir()
            # Definition ID/name changes each arm; an empty journal always begins the arm.
            defs='Game='+profile['serial']+'\n'
            for i in range(profile['definitions']):
                address=0x180000+i*8
                defs+=f'{i+1}|Local capacity {i+1}|0xH{address:06x}=1_0xH{address+1:06x}=2_0xX{address+4:06x}=305419896\n'
            client.put_file(wii, remote+'/set.txt', defs.encode())
            logpath=remote+f'/unlocks-{run_index}.txt'
            config=(f'Core=0\ngpuPlugin=2\nBiosDevice=0\nBootThruBios=0\nAutoSave=1\n'
                    f'NativeSaveDevice=0\nLimitFrames=0\nSkipFrames=0\nFPS=0\n'
                    'PadAutoAssign=1\nControllerType=0\nMemcard0=1\nMemcard1=1\n'
                    'smbipaddr=""\nsmbsharename=""\nsmbusername=""\nsmbpassword=""\nRomDirSMB=""\n'
                    f'Achievements={enabled}\nAchievementFile="{remote}/set.txt"\n'
                    f'AchievementLog="{logpath}"\nAchievementBenchRoot="{remote}"\n')
            client.put_file(wii, settings, config.encode())
            chain=(f'CHAIN\n{profile["stop"]} {remote}/play.txt\n{profile["directory"]}\n{profile["cue"]}\n')
            client.put_file(wii,'sd:/wiistation/autoboot.txt',chain.encode())
            launched=True
            client.send(wii,str(bundle/'boot.dol'),['AutoSave=0'],name='WiiStation gameplay pilot')
            deadline=time.monotonic()+profile.get('timeout',600)
            observed=False; shots=0
            while time.monotonic()<deadline:
                try:
                    state=client.status(wii)
                    if state.get('agent'):
                        if state.get('app')!='WiiStation': raise RuntimeError('Foreign app')
                        observed=True
                        # A few in-run screens establish actual play rather than title-only replay.
                        if profile.get('screens', False) and shots<3 and time.monotonic()>deadline-profile.get('timeout',600)+30+shots*45:
                            width,height,pixels=client.screen(wii)
                            (result/f'screen-{shots}.png').write_bytes(client.yuyv_png(width,height,pixels)); shots+=1
                    elif observed:
                        if state.get('crash') and state['crash']!=before.get('crash'): raise RuntimeError('New crash')
                        break
                except (OSError, client.HBCError): pass
                time.sleep(2)
            else: raise RuntimeError('Gameplay did not complete')
            client.hbc_wait(wii,90)
            listing,cut=read_result(client,wii,client.list_dir,'sd:/wiistation')
            if cut: raise RuntimeError('Incomplete results listing')
            for name in names:
                if name not in ('autoboot.txt','emu.log') and name in {n for _,_,n in listing}:
                    (result/name).write_bytes(read_result(client,wii,client.get_file,'sd:/wiistation/'+name))
            all_results.append(summarize(result,profile['start'],profile['stop'],enabled,profile['definitions'],profile['serial']))
            (result/'metrics.json').write_text(json.dumps(all_results[-1],indent=2))
            print(f'Completed arm {run_index+1}/{len(profile["arms"])} enabled={enabled}',flush=True)
        (output/'measurements.json').write_text(json.dumps(all_results,indent=2))
        if any(m['guest_state'] != all_results[0]['guest_state'] or not m['guest_state']
               for m in all_results):
            raise RuntimeError('Guest checkpoints differ; timing comparison is inconclusive')
        if any(m['ram'].get('fails') != 0 or m['ram'].get('heap_ok') != 1 for m in all_results):
            raise RuntimeError('Allocation or heap failure')
    finally:
        if launched:
            state=client.status(wii)
            if state.get('agent'):
                if state.get('app')!='WiiStation': raise RuntimeError('Foreign app; refusing cleanup')
                client.exit_app(wii,90)
            client.hbc_wait(wii,90)
        listing,cut=client.list_dir(wii,'sd:/wiistation')
        if cut: raise RuntimeError('Incomplete cleanup listing')
        present={n for _,_,n in listing}
        for name in names:
            path='sd:/wiistation/'+name
            if path in backups:
                client.put_file(wii,path,backups[path])
                if client.get_file(wii,path)!=backups[path]: raise RuntimeError('Restore verification failed')
            elif name in present: client.file_request(wii,'D',path)
        if settings in backups:
            client.put_file(wii,settings,backups[settings])
            if client.get_file(wii,settings)!=backups[settings]: raise RuntimeError('Config restore failed')
        else: client.file_request(wii,'D',settings)
        client.remove_tree(wii,remote,out=lambda _:None)
        for i in range(len(backups)): (output/f'backup-{i}').unlink()
        (output/'cleanup.json').write_text(json.dumps({'restored':True,'status':client.status(wii)},indent=2))


def main():
    p=argparse.ArgumentParser(description=__doc__); sub=p.add_subparsers(dest='command',required=True)
    prep=sub.add_parser('prepare'); prep.add_argument('--dol',type=Path,required=True); prep.add_argument('--client',type=Path,required=True)
    prep.add_argument('--profile',type=Path,required=True); prep.add_argument('--input',type=Path,required=True); prep.add_argument('--cue',type=Path,required=True); prep.add_argument('--bundle',type=Path,required=True)
    for command in ('run','inventory'):
        command_parser=sub.add_parser(command);command_parser.add_argument('--bundle',type=Path,required=True)
    a=p.parse_args()
    if a.command=='prepare':
        bench_smoke.prepare(a.dol,a.client,a.bundle)
        sources={'profile.json':a.profile,'play.txt':a.input,'disc.cue':a.cue,'bench_gameplay.py':Path(__file__),
                 'chain_table.py':Path(__file__).with_name('chain_table.py'),'vtl_view.py':Path(__file__).with_name('vtl_view.py')}
        m=json.loads((a.bundle/'manifest.json').read_text())
        for name,src in sources.items():
            shutil.copy2(src,a.bundle/name);m['inputs'][name]={'sha256':bench_smoke.digest(a.bundle/name)}
        profile=json.loads((a.bundle/'profile.json').read_text())
        validate_profile(profile)
        playback=(a.bundle/'play.txt').read_text()
        if re.search(r'^dump\s',playback,re.M):p.error('Input already schedules a debug VRAM dump')
        # The profiler otherwise writes a large automatic VRAM dump at frame
        # 6000, causing a saturated artificial stall in the measured window.
        # Keep every recorded input timestamp; defer only that debug dump.
        (a.bundle/'play.txt').write_text(f'dump {profile["stop"]+1}\n'+playback)
        m['inputs']['play.txt']['sha256']=bench_smoke.digest(a.bundle/'play.txt')
        m['recording_sha256']=bench_smoke.digest(a.input)
        (a.bundle/'manifest.json').write_text(json.dumps(m,indent=2));return
    wii = bench_wii("bench_gameplay.py")
    m=json.loads((a.bundle/'manifest.json').read_text())
    for name,entry in m['inputs'].items():
        if bench_smoke.digest(a.bundle/name)!=entry['sha256']:p.error('Frozen input changed: '+name)
    spec=importlib.util.spec_from_file_location('bench_client',a.bundle/'hbc.py'); client=importlib.util.module_from_spec(spec);spec.loader.exec_module(client)
    execute(client,wii,a.bundle,a.command=='inventory')
if __name__=='__main__':main()
