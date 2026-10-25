#!/usr/bin/env python3
"""benchdb.py -- every chained run's per-game numbers in one SQLite file (baselines/bench.sqlite,
gitignored), so "where does this version stand" and "what changed" are one query, not a hunt
through .runs/.

  benchdb.py ingest RUN_DIR... [--note TEXT]   add or replace runs (wsx.sh chain and wii_lab.py call it)
  benchdb.py backfill                          ingest every chained run under .runs/ and baselines/
  benchdb.py runs [--platform wii|dolphin] [--version V] [--limit N]
  benchdb.py latest [--platform wii] [--version V] [--preset speed]
                                               each game's most recent result
  benchdb.py compare RUN_A RUN_B               per game, matched on name and chain settings
  benchdb.py history GAME [--platform wii]     one game over time (GAME: a substring)
  benchdb.py sql "SELECT ..."                  anything else

Tables: runs (one per run directory: platform, version, commit, preset, probe gates, DOL hash)
and results (one per chained game: chain_table.py's columns, MEM1/malloc, a fatal: line, and
the game's whole last perf block as JSON for questions nobody has asked yet). Only runs with the
same preset compare: every preset but speed carries the vsig RAM hash, 23.5% of a Wii frame.
"""
import glob
import hashlib
import json
import os
import re
import sqlite3
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import chain_table  # noqa: E402
from cliutil import flag  # noqa: E402

REPO = chain_table.REPO
DB = os.environ.get('WSX_BENCHDB', os.path.join(REPO, 'baselines', 'bench.sqlite'))
COLS = chain_table.COLS
GATES = ('trace', 'gpu', 'cpu', 'vsig', 'gpusplit', 'gte', 'spu', 'pmc', 'hprof', 'texcheck', 'ach', 'play')

SCHEMA = '''
CREATE TABLE IF NOT EXISTS runs (
  id TEXT PRIMARY KEY, path TEXT, started TEXT, build TEXT, version TEXT, version_src TEXT,
  commit_sha TEXT, platform TEXT, preset TEXT, probes TEXT, vsig INTEGER, dol TEXT, dol_sha1 TEXT,
  chain TEXT, settings TEXT, env TEXT, note TEXT, ingested TEXT DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE IF NOT EXISTS results (
  run_id TEXT REFERENCES runs(id) ON DELETE CASCADE, n INTEGER, game TEXT, sets TEXT, rom TEXT,
  vblanks INTEGER, wall_s REAL, %s,
  mem1_min_kb INTEGER, malloc_close_kb INTEGER, mem1_close_kb INTEGER, lr_dyn_close_kb INTEGER,
  fatal TEXT, block TEXT, PRIMARY KEY (run_id, n));
CREATE INDEX IF NOT EXISTS results_game ON results(game);
''' % ', '.join('%s REAL' % c for c in COLS)

PRESETS = {   # scripts/build.sh's presets, by the gates that tell them apart
    'speed': dict(trace=0, gpu=0, vsig=0, gpusplit=0, pmc=0, hprof=0, play=0),
    'light': dict(trace=0, gpu=0, vsig=1, gpusplit=0, pmc=0, hprof=0),
    'deep': dict(trace=0, gpu=0, vsig=1, gpusplit=1, texcheck=0),
    'texcheck': dict(gpusplit=1, texcheck=1),
    'pmc': dict(vsig=0, pmc=1),
    'hprof': dict(vsig=0, hprof=1),
    'play': dict(play=1),
    'all': dict(trace=1, gpu=1, vsig=1),
}


def connect():
    db = sqlite3.connect(DB)
    db.row_factory = sqlite3.Row
    db.execute('PRAGMA foreign_keys = ON')
    db.executescript(SCHEMA)
    return db


def git(*args):
    try:
        return subprocess.run(['git', '-C', REPO] + list(args), capture_output=True, text=True,
                              timeout=30).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ''


def version_at(sha):
    m = re.search(r'WIISTATION_VERSION "([^"]+)"', git('show', sha + ':Gamecube/version.h'))
    return m.group(1) if m else None


def preset_of(g):
    for name, want in PRESETS.items():
        if all(g.get(k) == v for k, v in want.items()):
            return name
    return 'custom'


def sha1(path):
    try:
        with open(path, 'rb') as f:
            return hashlib.sha1(f.read()).hexdigest()
    except OSError:
        return None


def run_info(run):
    info = {}
    try:
        text = open(os.path.join(run, 'run.info'), encoding='utf-8', errors='replace').read()
    except OSError:
        return info
    for key in ('dol', 'chain', 'autoboot', 'wii'):
        m = re.search(r'\b%s=(\S+)' % key, text)
        if m:
            info[key] = m.group(1)
    for key in ('settings', 'env', 'platform'):
        m = re.search(r'^%s: ?(.*)$' % key, text, re.M)
        if m:
            info[key] = m.group(1).strip()
    m = re.search(r'commit: (\w+)(\+dirty)?', text)
    if m:
        info['commit'] = m.group(1) + (m.group(2) or '')
    return info


def log_path(run):
    play = os.path.join(run, 'debuglogs', 'play.log')
    return play if os.path.isfile(play) else os.path.join(run, 'perf.log')


def ingest(db, run, note=None, quiet=False):
    log = log_path(run)
    if not os.path.isfile(log):
        return 0
    gs = chain_table.games(log)
    if not gs:
        return 0
    text = open(log, encoding='utf-8', errors='replace').read()
    info = run_info(run)
    m = re.search(r'^# run started (.*?), build (.*)$', text, re.M)
    started, build = (m.group(1), m.group(2)) if m else (None, None)
    gates, version, src = {}, None, None
    m = re.search(r'^# version (\S+) probes (.*)$', text, re.M)
    if m:
        version, src = m.group(1), 'log'
        gates = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', m.group(2))}
    sha = info.get('commit')
    if not version and sha:
        version, src = version_at(sha.split('+')[0]), 'commit'
    if not version and build:   # older logs: the last commit before the build's date
        guess = git('rev-list', '-1', '--before=' + build, 'HEAD')
        if guess:
            version, src, sha = version_at(guess), 'date', sha or 'date:' + guess[:8]
    vsig = gates.get('vsig')
    if vsig is None:
        vsig = 1 if glob.glob(os.path.join(run, 'vsig_*.bin')) else None
    dol = os.path.join(run, 'boot.dol')
    if not os.path.isfile(dol):
        d = info.get('dol', '')
        dol = d if os.path.isfile(os.path.join(REPO, d)) and d.endswith('.dol') else None
        dol = os.path.join(REPO, dol) if dol else None
    rid = os.path.relpath(os.path.abspath(run), REPO).replace('\\', '/')
    platform = 'wii' if info.get('platform', '').startswith('hardware') or info.get('wii') else 'dolphin'
    db.execute('DELETE FROM runs WHERE id=?', (rid,))
    db.execute('INSERT INTO runs (id, path, started, build, version, version_src, commit_sha, platform, '
               'preset, probes, vsig, dol, dol_sha1, chain, settings, env, note) '
               'VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
               (rid, os.path.abspath(run), started, build, version, src, sha, platform,
                preset_of(gates) if gates else None, json.dumps(gates) if gates else None, vsig,
                info.get('dol'), sha1(dol) if dol else None,
                info.get('chain') or info.get('autoboot'), info.get('settings'), info.get('env'), note))
    closes = [dict(kv.split('=') for kv in c.split())
              for c in re.findall(r'^gameclose: (.*)$', text, re.M)]
    for i, (n, rom, vbl, b) in enumerate(gs):
        base, _, sets = rom.partition(' {')
        name = re.sub(r'\s*[\[(].*$', '', os.path.splitext(base)[0])
        r = chain_table.row(b, vbl) or {}
        c = closes[i] if i < len(closes) else {}
        db.execute('INSERT INTO results VALUES (%s)' % ','.join('?' * (13 + len(COLS))),
                   [rid, n, name, sets.rstrip('}') or None, base, vbl, r.get('wall_s')]
                   + [r.get(col) for col in COLS]
                   + [b.get('mem2map', {}).get('mem1_min_kb'),
                      c.get('malloc_used_kb'), c.get('mem1_free_kb'), c.get('lr_dyn_kb'),
                      fatal_of(b), json.dumps(b, default=list)])
    db.commit()
    if not quiet:
        print('%s: %d games, %s %s %s' % (rid, len(gs), platform, version or '?', preset_of(gates) if gates else ''))
    return len(gs)


def fatal_of(b):
    f = b.get('fatal')
    return json.dumps(f) if f else None


def table(rows, cols):
    rows = [dict(r) for r in rows]
    if not rows:
        print('(nothing)')
        return
    w = {c: max(len(c), *(len(fmt(r[c])) for r in rows)) for c in cols}
    print('  '.join(c.ljust(w[c]) for c in cols))
    for r in rows:
        print('  '.join(fmt(r[c]).ljust(w[c]) for c in cols))


def fmt(v):
    if v is None:
        return '-'
    if isinstance(v, float):
        return '%.2f' % v
    return str(v)


def where(args, prefix='r.'):
    cond, par = [], []
    for key, col in (('--platform', 'platform'), ('--version', 'version'), ('--preset', 'preset')):
        v = flag(args, key)
        if v is not None:
            cond.append('%s%s=?' % (prefix, col))
            par.append(v)
    return (' AND '.join(cond) or '1'), par


def main():
    a = sys.argv[1:]
    if not a or a[0] in ('-h', '--help'):
        sys.exit(__doc__)
    db = connect()
    cmd = a[0]
    if cmd == 'ingest':
        note = flag(a, '--note')
        runs = [x for x in a[1:] if x != '--note' and x != note]
        if not sum(ingest(db, r, note) for r in runs):
            sys.exit('nothing ingested: no chained perf.log in %s' % ' '.join(runs))
    elif cmd == 'backfill':
        n = k = 0
        for perf in sorted(glob.glob(os.path.join(REPO, '.runs', '*', 'perf.log'))
                           + glob.glob(os.path.join(REPO, 'baselines', '*', 'perf.log'))):
            try:
                g = ingest(db, os.path.dirname(perf), quiet=True)
            except Exception as e:   # one damaged log must not stop the rest
                print('skipped %s: %s' % (perf, e))
                continue
            n, k = n + (g > 0), k + g
        print('backfill: %d runs, %d game results in %s' % (n, k, DB))
    elif cmd == 'runs':
        cond, par = where(a, '')
        lim = int(flag(a, '--limit') or 30)
        table(db.execute('SELECT id, started, platform, version, preset, chain, '
                         '(SELECT count(*) FROM results WHERE run_id=id) games FROM runs WHERE %s '
                         'ORDER BY started DESC LIMIT %d' % (cond, lim), par),
              ['id', 'started', 'platform', 'version', 'preset', 'games', 'chain'])
    elif cmd == 'latest':
        cond, par = where(a)
        table(db.execute('SELECT s.game, s.sets, s.vblanks, s.speed, s.load, s.psx, s.gpu, s.spu, s.mdec, '
                         's.mem1_min_kb, r.platform, r.version, r.preset, r.started, r.id FROM results s '
                         'JOIN runs r ON r.id=s.run_id WHERE %s AND s.speed IS NOT NULL AND '
                         'r.started = (SELECT max(r2.started) FROM results s2 JOIN runs r2 ON r2.id=s2.run_id '
                         'WHERE s2.game=s.game AND %s) GROUP BY s.game ORDER BY s.game'
                         % (cond, cond.replace('r.', 'r2.')), par + par),
              ['game', 'speed', 'load', 'psx', 'gpu', 'spu', 'mdec', 'mem1_min_kb', 'platform', 'version',
               'preset', 'id'])
    elif cmd == 'compare':
        ra, rb = a[1].rstrip('/'), a[2].rstrip('/')
        ra, rb = [x if x.startswith('.runs/') or x.startswith('baselines/') else '.runs/' + x for x in (ra, rb)]
        # a game twice in one chain pairs first with first, second with second (k)
        one = ('SELECT *, row_number() OVER (PARTITION BY game, coalesce(sets, "") ORDER BY n) k '
               'FROM results WHERE run_id=?')
        q = ('SELECT a.game, a.sets, a.speed sa, b.speed sb, a.load la, b.load lb, a.gpu ga, b.gpu gb, '
             'a.psx pa, b.psx pb FROM (%s) a JOIN (%s) b ON a.game=b.game AND a.k=b.k AND '
             'coalesce(a.sets,"")=coalesce(b.sets,"") ORDER BY a.n' % (one, one))
        rows = []
        for r in db.execute(q, (ra, rb)):
            r = dict(r)
            d = (r['sb'] or 0) - (r['sa'] or 0)
            r['d_speed'] = d
            r['d_load'] = (r['lb'] or 0) - (r['la'] or 0)
            r['flag'] = 'SLOWER' if d < -0.01 or r['d_load'] > 2 else ('faster' if d > 0.01 else '')
            rows.append(r)
        for x in (ra, rb):
            info = db.execute('SELECT platform, version, preset FROM runs WHERE id=?', (x,)).fetchone()
            print('%s: %s' % (x, dict(info) if info else 'NOT IN THE DB (ingest it)'))
        table(rows, ['game', 'sa', 'sb', 'd_speed', 'la', 'lb', 'd_load', 'ga', 'gb', 'flag'])
    elif cmd == 'history':
        cond, par = where(a)
        table(db.execute('SELECT r.started, r.platform, r.version, r.preset, s.game, s.vblanks, s.speed, '
                         's.load, s.gpu, r.id FROM results s JOIN runs r ON r.id=s.run_id WHERE '
                         's.game LIKE ? AND %s ORDER BY r.started' % cond, ['%' + a[1] + '%'] + par),
              ['started', 'platform', 'version', 'preset', 'game', 'vblanks', 'speed', 'load', 'gpu', 'id'])
    elif cmd == 'sql':
        cur = db.execute(a[1])
        table(cur.fetchall(), [d[0] for d in cur.description or []])
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
