#!/usr/bin/env python3
"""disc_art_fetch.py -- download PS1 disc art (ScreenScraper "support-2D") for the games in the library.

    python scripts/disc_art_fetch.py [--out DIR] [--limit N] [--only SERIAL] [--dry-run] [--sleep S]

Games come from GAME_LIBRARY/games.tsv (key, boot file, format, status). A game's serial is the
[SLUS-00892] tag in its boot file name or key; without one, the game name is looked up in the
libretro Redump dat (cover_pack.read_dat). Games with neither are listed and skipped, as are the
Tests/ homebrew. Files are written as OUT/<SERIAL>.<ext> (SLUS00892.png), the name
cover_pack.py --disc-dir matches by serial. Default OUT: GAME_LIBRARY/cover_sources/disc.
Serials with a file already in OUT are skipped, so the script can be re-run.

Login: GAME_LIBRARY/screenscraper.ini ([developer] devid devpassword softname, [user] ssid
sspassword), read here at run time only; no value is ever printed (URLs are redacted).
One request at a time with a pause between; stops on quota (HTTP 430, 429 after retries).
"""
import argparse
import configparser
import json
import pathlib
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import cover_pack  # noqa: E402
import tools  # noqa: E402

API = "https://api.screenscraper.fr/api2/jeuInfos.php"
SYSTEM_PS1 = "57"
MEDIA = "support-2D"
REGIONS = {"us": ("us", "ca"), "eu": ("eu", "fr", "de", "uk", "it", "es", "pt", "nl", "se", "au"),
           "jp": ("jp",)}
PREFIX_REGION = {"SLUS": "us", "SCUS": "us", "SLES": "eu", "SCES": "eu",
                 "SLPS": "jp", "SCPS": "jp", "SLPM": "jp"}
TAG = re.compile(r"\[((?:SLUS|SCUS|SLES|SCES|SLPS|SCPS|SLPM)[-_ ]?\d{3,6})\]", re.I)


class Fatal(Exception):
    pass


def redact(text, creds):
    for k, v in creds.items():
        if v and k in ("devid", "devpassword", "devdebugpassword", "ssid", "sspassword"):
            text = text.replace(v, "***").replace(urllib.parse.quote(v), "***")
    return text


def load_creds(lib, debug=()):
    """debug: ["forcerequestok=99999", ...] -- ScreenScraper's developer debug mode (100 calls a day):
    adds devdebugpassword and those switches to every lookup."""
    ini = pathlib.Path(lib) / "screenscraper.ini"
    cp = configparser.ConfigParser()
    if not cp.read(ini, encoding="utf-8"):
        raise Fatal("screenscraper.ini not found in the library folder")
    try:
        d, u = cp["developer"], cp["user"]
        creds = {"devid": d["devid"], "devpassword": d["devpassword"], "softname": d["softname"],
                 "ssid": u["ssid"], "sspassword": u["sspassword"]}
        if debug:
            creds["devdebugpassword"] = d["devdebugpassword"]
            creds.update(kv.split("=", 1) for kv in debug)
        return creds
    except KeyError as e:
        raise Fatal("screenscraper.ini has no key %s" % e)


def games_of(lib, dat_path):
    """-> ordered {serial: (game key, boot file)} and [(key, why)] of games without a serial."""
    by_name = {}
    if dat_path and pathlib.Path(dat_path).is_file():
        entries, _ = cover_pack.read_dat(dat_path)
        for s, n in entries.items():
            by_name.setdefault(cover_pack._alnum(cover_pack.base_name(n)), s)
            by_name.setdefault(cover_pack._alnum(n), s)
    found, skipped = {}, []
    with open(pathlib.Path(lib) / "games.tsv", encoding="utf-8") as f:
        next(f)
        for line in f:
            p = line.rstrip("\r\n").split("\t")
            if len(p) < 3 or p[2] == "exe" or p[0].startswith("Tests/"):
                continue
            key, boot = p[0], p[1]
            m = TAG.search(boot) or TAG.search(key)
            serial = cover_pack.normalise_serial(m.group(1)) if m else None
            if not serial:
                stem = pathlib.Path(boot).stem
                serial = (by_name.get(cover_pack._alnum(stem)) or by_name.get(cover_pack._alnum(key.split("/")[-1]))
                          or by_name.get(cover_pack._alnum(cover_pack.base_name(stem))))
            if not serial:
                skipped.append((key, "no serial in the name and not in the dat"))
            elif serial not in found:
                found[serial] = (key, boot)
    return found, skipped


def have(out, serial):
    return any(out.glob(serial + ".*")) if out.is_dir() else False


def api_get(url, creds, tries=3):
    for i in range(tries):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": "WiiStation-disc-art/1"})
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code == 429 and i + 1 < tries:
                time.sleep(20 * (i + 1))
                continue
            raise
        except (urllib.error.URLError, TimeoutError) as e:
            if i + 1 < tries:
                time.sleep(5)
                continue
            raise Fatal("network error: " + redact(str(e), creds))


def info(serial, name, creds):
    """-> jeu dict or None (not found). HTTPError for login/quota."""
    q = dict(creds, output="json", systemeid=SYSTEM_PS1, romnom=name, serialnum=serial[:4] + "-" + serial[4:])
    for attempt in (q, {k: v for k, v in q.items() if k != "serialnum"}):
        try:
            raw = api_get(API + "?" + urllib.parse.urlencode(attempt), creds)
        except urllib.error.HTTPError as e:
            if e.code == 404:
                continue
            raise
        try:
            resp = json.loads(raw)["response"]
        except (ValueError, KeyError):
            continue        # plain-text answer = not found
        check_quota(resp.get("ssuser", {}))
        if "jeu" in resp:
            return resp["jeu"]
    return None


def check_quota(u):
    """Stop at the account's own daily limits, read from each answer (the server does not
    always refuse: in debug mode a forced counter past the limit still answers 200)."""
    for used, limit, what in (("requeststoday", "maxrequestsperday", "requests"),
                              ("requestskotoday", "maxrequestskoperday", "not-found answers")):
        try:
            n, m = int(u[used]), int(u[limit])
        except (KeyError, ValueError):
            continue
        if m and n >= m:
            raise Fatal("daily quota reached: %d of %d %s today; retry tomorrow" % (n, m, what))


def pick(jeu, serial, name):
    """Best support-2D media dict: own region, then wor, then any; disc number if the name has one."""
    medias = [m for m in jeu.get("medias", []) if m.get("type") == MEDIA and m.get("url")]
    if not medias:
        return None
    want = REGIONS.get(PREFIX_REGION.get(serial[:4], ""), ())
    dm = re.search(r"(?:Disc|CD)\s*(\d)|Disc(\d)of", name, re.I)
    disc = next((g for g in dm.groups() if g), None) if dm else None

    def score(m):
        r = m.get("region", "")
        reg = 0 if r in want else 1 if r == "wor" else 2
        num = 0 if (disc and m.get("support") == disc) or not disc else 1   # "support": the disc number
        return (num, reg)
    return min(medias, key=score)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    lib = tools.get("GAME_LIBRARY")
    ap.add_argument("--out", help="output folder (default GAME_LIBRARY/cover_sources/disc)")
    ap.add_argument("--limit", type=int, default=50,
                    help="look up at most N serials this run (default 50; 0 = all). Keeps a run small and slow")
    ap.add_argument("--retry-missing", action="store_true",
                    help="ask again for serials listed in missing.txt (not found or no disc art last time)")
    ap.add_argument("--only", help="one serial, e.g. SCUS94423 or SCUS-94423")
    ap.add_argument("--dry-run", action="store_true", help="list what would be fetched, no network")
    ap.add_argument("--sleep", type=float, default=2.0, help="seconds between requests (default 2)")
    ap.add_argument("--debug", action="append", default=[], metavar="KEY=VALUE",
                    help="developer debug mode, for testing (100 calls a day): forceupdate=1, forcelevel=N, "
                         "forcerequestok=N (daily counter), forcerequestko=N (not-found counter), forcerequestmin=N")
    ap.add_argument("--dat", default=str(pathlib.Path(lib) / "cover_sources" / "Sony - PlayStation.dat"))
    a = ap.parse_args()
    out = pathlib.Path(a.out or pathlib.Path(lib) / "cover_sources" / "disc")
    games, skipped = games_of(lib, a.dat)
    if a.only:
        o = cover_pack.normalise_serial(a.only)
        games = {s: v for s, v in games.items() if s == o}
    # Serials ScreenScraper did not have last time: not asked again (each not-found counts against the
    # daily not-found quota, and asking for the same misses run after run looks like scraping).
    missing_file = out / "missing.txt"
    missing = {}
    if missing_file.is_file():
        for line in missing_file.read_text(encoding="utf-8").splitlines():
            if line.strip():
                missing[line.split("\t")[0]] = line
    todo = {s: v for s, v in games.items() if not have(out, s) and (a.retry_missing or s not in missing)}
    print("%d serial(s), %d already in %s, %d missing last time%s, %d to fetch, %d game(s) without serial"
          % (len(games), sum(have(out, s) for s in games), out, len(missing),
             " (retried)" if a.retry_missing else " (skipped)", len(todo), len(skipped)))
    for k, why in skipped:
        print("  skip %s: %s" % (k, why))
    if a.limit:
        todo = dict(list(todo.items())[:a.limit])
    if a.dry_run:
        for s, (k, b) in todo.items():
            print("  would fetch %s  %s" % (s, k))
        print("estimate: %d serial(s) x up to 2 requests x %.0f s = about %.0f min"
              % (len(todo), a.sleep, len(todo) * 2 * a.sleep / 60))
        return 0
    if not todo:
        return 0
    creds = load_creds(lib, a.debug)
    out.mkdir(parents=True, exist_ok=True)
    ok = 0

    def miss(s, key, why):
        print("%s: %s (%s)" % (s, why, key))
        missing[s] = "%s\t%s\t%s\t%s" % (s, why, time.strftime("%Y-%m-%d"), key)
        missing_file.write_text("\n".join(sorted(missing.values())) + "\n", encoding="utf-8")

    try:
        for s, (key, boot) in todo.items():
            try:
                jeu = info(s, boot, creds)
                if jeu is None:
                    miss(s, key, "not found on ScreenScraper")
                else:
                    m = pick(jeu, s, boot)
                    if not m:
                        miss(s, key, "found, but no disc art")
                    else:
                        time.sleep(a.sleep)
                        data = api_get(m["url"], creds)
                        if data[:7] == b"NOMEDIA" or len(data) < 200:
                            miss(s, key, "no disc art file")
                        else:
                            if missing.pop(s, None):   # found after all (--retry-missing)
                                missing_file.write_text("".join(v + "\n" for v in sorted(missing.values())),
                                                        encoding="utf-8")
                            ext = "." + (m.get("format") or "png").lower().replace("jpeg", "jpg")
                            (out / (s + ext)).write_bytes(data)
                            ok += 1
                            print("%s: saved %s%s (region %s, %d bytes)" % (s, s, ext, m.get("region"), len(data)))
            except urllib.error.HTTPError as e:
                if e.code in (401, 403):
                    raise Fatal("login refused (HTTP %d): check screenscraper.ini" % e.code)
                if e.code in (429, 430, 431):
                    raise Fatal("quota or thread limit reached (HTTP %d); stop and retry later" % e.code)
                if e.code in (423, 426):
                    raise Fatal("ScreenScraper refuses requests (HTTP %d)" % e.code)
                print("%s: HTTP %d" % (s, e.code))
            time.sleep(a.sleep)
    except Fatal as e:
        print("ERROR: %s" % e)
        print("%d saved before stopping" % ok)
        return 1
    print("done: %d saved" % ok)
    return 0


if __name__ == "__main__":
    sys.exit(main())
