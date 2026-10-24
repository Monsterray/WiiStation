"""ra_ca_bundle.py [check | update SRC --source TEXT] -- the CA bundle Official mode trusts.

The bundle is Gamecube/release/achievements/cacert.pem. It ships with every install
(wii_install.py puts it at sd:/wiistation/achievements/cacert.pem) and the Wii's TLS checks
RetroAchievements' certificate against it, so it is pinned: cacert.pem.source beside it holds its
SHA-256 and where it came from, and wii_install.py refuses a bundle that does not match.

  check            (the default) the pin, the roots (how many, which expired or expire within a
                   year), and a live TLS connection to retroachievements.org and
                   media.retroachievements.org that trusts ONLY this bundle: it names the root that
                   anchors their chain and when it expires. Exit 1 when RetroAchievements does not
                   validate. Nothing is downloaded.
  update SRC --source TEXT
                   take another bundle the person chose and fetched themselves (e.g. a newer
                   MSYS2 ca-certificates package's /usr/ssl/certs/ca-bundle.crt, or curl.se's
                   cacert.pem checked against its .sha256), run every check on it, and only then
                   put it in place with a new pin. This script never fetches a bundle: which one to
                   trust stays a decision a person makes.

The current bundle: MSYS2 ca-certificates-20241223-1 (Mozilla NSS), as devkitPro installs it.
"""
import argparse
import datetime
import hashlib
import pathlib
import re
import shutil
import socket
import ssl
import sys

REPO = pathlib.Path(__file__).resolve().parents[1]
BUNDLE = REPO / "Gamecube/release/achievements/cacert.pem"
PIN = BUNDLE.with_name("cacert.pem.source")
HOSTS = ("retroachievements.org", "media.retroachievements.org")
MAX_SIZE = 1 << 20


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pinned():
    """(sha256, source) from cacert.pem.source; exits when the file is missing or malformed."""
    try:
        text = PIN.read_text(encoding="utf-8")
    except OSError:
        sys.exit(f"{PIN}: missing")
    h = re.search(r"^sha256 ([0-9a-f]{64})$", text, re.M)
    s = re.search(r"^source (.+)$", text, re.M)
    if not h or not s:
        sys.exit(f"{PIN}: needs 'sha256 <hex>' and 'source <text>' lines")
    return h.group(1), s.group(1)


def verify_pin():
    """The shipped bundle is the pinned one. Returns its source; exits otherwise (wii_install.py)."""
    want, source = pinned()
    if sha256(BUNDLE) != want:
        sys.exit(f"{BUNDLE}: SHA-256 {sha256(BUNDLE)} is not the pinned {want} ({PIN.name}); "
                 "use `ra_ca_bundle.py update` to change it")
    return source


def checks(path):
    """Every check on a bundle; returns the problems (empty: fine). Prints what it finds."""
    problems = []
    data = path.read_bytes()
    if len(data) > MAX_SIZE:
        return [f"{len(data)} bytes: too big for a CA bundle"]
    pems = data.count(b"-----BEGIN CERTIFICATE-----")
    if not pems or pems != data.count(b"-----END CERTIFICATE-----") or b"PRIVATE KEY" in data:
        return ["not a PEM bundle of certificates only"]
    ctx = ssl.create_default_context(cafile=str(path))
    roots = ctx.get_ca_certs()
    now = datetime.datetime.now(datetime.timezone.utc).timestamp()
    year = 365 * 86400
    gone = [r for r in roots if ssl.cert_time_to_seconds(r["notAfter"]) < now]
    soon = [r for r in roots if now <= ssl.cert_time_to_seconds(r["notAfter"]) < now + year]
    name = lambda r: dict(x[0] for x in r["subject"]).get("commonName") or dict(x[0] for x in r["subject"]).get("organizationName")
    print(f"{path}: {len(roots)} roots ({pems} PEM blocks), {len(gone)} expired, {len(soon)} expire within a year")
    for r in gone + soon:
        print(f"  {'expired' if r in gone else 'expires'} {r['notAfter']}: {name(r)}")
    for host in HOSTS:   # trusting only this bundle (create_default_context(cafile=) loads no system roots)
        try:
            with socket.create_connection((host, 443), timeout=15) as s, ctx.wrap_socket(s, server_hostname=host) as t:
                leaf = t.getpeercert()
        except (OSError, ssl.SSLError) as e:
            problems.append(f"{host}: does not validate with this bundle ({e})")
            continue
        issuer = dict(x[0] for x in leaf["issuer"])
        print(f"{host}: OK, issued by {issuer.get('organizationName')} {issuer.get('commonName')}, "
              f"leaf until {leaf['notAfter']}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", nargs="?", default="check", choices=("check", "update"))
    ap.add_argument("src", nargs="?", type=pathlib.Path, help="update: the bundle to take")
    ap.add_argument("--source", help="update: where SRC came from (package and version, or URL and date)")
    a = ap.parse_args()
    if a.action == "check":
        source = verify_pin()
        print(f"pinned: {BUNDLE.name} = {pinned()[0][:16]}... from {source}")
        problems = checks(BUNDLE)
    else:
        if not a.src or not a.src.is_file() or not a.source:
            ap.error("update needs SRC (a file) and --source TEXT")
        problems = checks(a.src)
        if not problems:
            shutil.copyfile(a.src, BUNDLE)
            PIN.write_text(f"sha256 {sha256(BUNDLE)}\nsource {a.source}\n"
                           f"taken {datetime.date.today().isoformat()} by scripts/ra_ca_bundle.py update\n", encoding="utf-8")
            print(f"{BUNDLE} replaced; pin written ({PIN.name})")
    for p in problems:
        print(f"PROBLEM: {p}")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
