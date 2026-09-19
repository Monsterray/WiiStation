#!/bin/bash
# dolphin_run.sh -- unattended WiiStation run in Dolphin with frame dumping.
#   usage: dolphin_run.sh <outdir> [seconds] [autoinput file] [settings file] [autoboot file]
#   env:   KEEP=<n> frames to keep (default 120, the last ones), SHEET_STEP=<n> thumbnail stride (20)
# Stages the input script, settings and (optionally) autoboot.txt into the SD sync folder, turns on
# Dolphin frame dumping (Dolphin.ini/GFX.ini/Logger.ini are backed up and restored), boots the debug
# .dol, kills Dolphin after <seconds>, keeps the last KEEP frames, extracts ptrace.log/vram.bin and
# reads perf.log straight from the FAT image (sdimage_read.py), then builds a contact sheet (sheet.py).
# Paths (Dolphin install, sync folder, .dol) are the constants below; adjust for another machine.
# Defaults for the optional files come from the WiiSDSync_paused_by_claude folder next to the sync dir.
set -u
OUT="$1"; SECS="${2:-170}"; AIN="${3:-}"; SET="${4:-}"; ABOOT="${5:-}"
D="/c/tools/Dolphin-x64"; CFG="$D/User/Config"; S="$D/User/Load/WiiSDSync/wiisxrx"; P="$D/User/Load/WiiSDSync_paused_by_claude"
SP="$(cd "$(dirname "$0")" && pwd)"   # sheet.py and sdimage_read.py live next to this script
mkdir -p "$OUT/frames"
cp "$CFG/Dolphin.ini" "$OUT/Dolphin.ini.orig"; cp "$CFG/GFX.ini" "$OUT/GFX.ini.orig"; cp "$CFG/Logger.ini" "$OUT/Logger.ini.orig" 2>/dev/null || true
python - "$CFG/Dolphin.ini" "$CFG/GFX.ini" <<'PY'
import sys
def setkey(path, section, key, val):
    lines = open(path).read().split("\n"); out=[]; insec=False; done=False
    for l in lines:
        if l.startswith("["):
            insec = (l.strip() == f"[{section}]")
            out.append(l)
            if insec and not done: out.append(f"{key} = {val}"); done=True
            continue
        if insec and l.split("=")[0].strip() == key:
            continue   # dropped: the value was written right under the header
        out.append(l)
    if not done:
        if not insec: out.append(f"[{section}]")
        out.append(f"{key} = {val}")
    open(path,"w").write("\n".join(out))
setkey(sys.argv[1], "Movie", "DumpFrames", "True")
setkey(sys.argv[2], "Settings", "DumpFramesAsImages", "True")
setkey(sys.argv[2], "Settings", "PNGCompressionLevel", "1")
import os
lg=os.path.join(os.path.dirname(sys.argv[1]),"Logger.ini")
if os.path.exists(lg): setkey(lg, "Logs", "FRAMEDUMP", "True")
PY
rm -rf "$D/User/Dump/Frames"
cp "${AIN:-$P/autoinput.txt}" "$S/autoinput.txt"; cp "${SET:-$P/settingsRX2022.cfg}" "$S/settingsRX2022.cfg"
if [ -n "$ABOOT" ]; then cp "$S/autoboot.txt" "$OUT/autoboot.txt.orig"; cp "$ABOOT" "$S/autoboot.txt"; fi
echo "framedump run: start $(date +%T)"
"$D/Dolphin.exe" -b -e /c/projects/WiiStation/Gamecube/WiiSXRX_debug.dol &
sleep "$SECS"
taskkill //IM Dolphin.exe //F >/dev/null 2>&1 || true
sleep 3
cp "$OUT/Dolphin.ini.orig" "$CFG/Dolphin.ini"; cp "$OUT/GFX.ini.orig" "$CFG/GFX.ini"; [ -f "$OUT/Logger.ini.orig" ] && cp "$OUT/Logger.ini.orig" "$CFG/Logger.ini"
rm -f "$S/autoinput.txt" "$S/settingsRX2022.cfg"
if [ -n "$ABOOT" ]; then cp "$OUT/autoboot.txt.orig" "$S/autoboot.txt"; fi
N=$(ls "$D/User/Dump/Frames" 2>/dev/null | wc -l); echo "frames dumped: $N"; grep -c "FRAMEDUMP" "$D/User/Logs/dolphin.log" | sed "s/^/framedump log lines: /"
# keep the last 120 frames only
ls "$D/User/Dump/Frames"/framedump_*.png 2>/dev/null | sed -E 's/.*framedump_([0-9]+)\.png/\1/' | sort -n | tail -${KEEP:-120} | while read i; do mv "$D/User/Dump/Frames/framedump_$i.png" "$OUT/frames/"; done
rm -rf "$D/User/Dump/Frames"
"/c/Program Files/7-Zip/7z.exe" e -y -o"$OUT" "$D/User/Load/WiiSD.raw" 'wiisxrx/ptrace.log' 'wiisxrx/vram.bin' >/dev/null 2>&1 || echo "7z extract failed"
echo "run done $(date +%T)"
grep -E "cmd=c1 " "$OUT/ptrace.log" | cut -c1-80 | head -6
LAST=$(ls "$OUT/frames" | sed -E 's/framedump_([0-9]+)\.png/\1/' | sort -n | tail -1); FIRST=$(ls "$OUT/frames" | sed -E 's/framedump_([0-9]+)\.png/\1/' | sort -n | head -1)
echo "kept frames $FIRST..$LAST"
if [ -n "$FIRST" ]; then python "$SP/sheet.py" "$OUT/sheet.png" "$OUT/frames" "$FIRST" "${SHEET_STEP:-20}" "$LAST" 6 206 && echo "sheet: $OUT/sheet.png"; else echo "no frames kept"; fi
python "$SP/sdimage_read.py" "$D/User/Load/WiiSD.raw" wiisxrx/perf.log "$OUT/perf.log" 2>&1 | tail -1; grep -a -E "^sio|offsoft|ogxoff" "$OUT/perf.log" 2>/dev/null | tail -3
