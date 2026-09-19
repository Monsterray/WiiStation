#!/bin/bash
# dolphin_run.sh -- unattended WiiStation run in Dolphin with frame dumping.
#   usage: dolphin_run.sh <outdir> [seconds] [autoinput file] [settings file] [autoboot file]
#   env:   KEEP=<n> frames to keep (default 120, the last ones), SHEET_STEP=<n> thumbnail stride (20)
#          DOL=<path> build to boot, XFB_RAM=1 to make a guest crash screen visible in the frames
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
if tasklist 2>/dev/null | grep -qi "Dolphin.exe"; then
  echo "refusing to start: a Dolphin instance is already running (the SD image would be locked and the kill below would hit it)"; exit 2
fi
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
# A guest-requested stop (WiiStation crashed and reloaded, or exited) must end the run on its own:
# with ConfirmStop on, Dolphin instead pops "Do you want to stop the current emulation?" and waits for a human.
setkey(sys.argv[1], "Interface", "ConfirmStop", "False")
import os
# XFB_RAM=1: present CPU-written framebuffers too, so the libogc exception screen (crash dump with PC/LR/stack)
# shows up in the frame dump; Dolphin's default "XFB copies to texture only" never displays it.
if os.environ.get("XFB_RAM"): setkey(sys.argv[2], "Hacks", "XFBToTextureEnable", "False")
lg=os.path.join(os.path.dirname(sys.argv[1]),"Logger.ini")
if os.path.exists(lg): setkey(lg, "Logs", "FRAMEDUMP", "True")
PY
rm -rf "$D/User/Dump/Frames"
cp "${AIN:-$P/autoinput.txt}" "$S/autoinput.txt"; cp "${SET:-$P/settingsRX2022.cfg}" "$S/settingsRX2022.cfg"
# autoboot: without it WiiStation sits in its menu (the user keeps their own file renamed to .disabled)
[ -n "$ABOOT" ] || echo "note: no autoboot file given -- the run will stay in the menu unless $S/autoboot.txt already exists"
if [ -n "$ABOOT" ]; then [ -f "$S/autoboot.txt" ] && cp "$S/autoboot.txt" "$OUT/autoboot.txt.orig"; cp "$ABOOT" "$S/autoboot.txt"; fi
echo "framedump run: start $(date +%T)"
DOL="${DOL:-/c/projects/WiiStation/Gamecube/WiiSXRX_debug.dol}"   # env DOL=... to test another build (e.g. a worktree)
echo "dol: $DOL ($(stat -c %y "$DOL" 2>/dev/null | cut -c1-19))"
"$D/Dolphin.exe" -b -e "$DOL" &
DPID=$!; sleep 2; WPID=$(ps -p $DPID 2>/dev/null | awk 'NR==2{print $4}')
# Fallback for the Windows PID: the guard above proved no Dolphin was running, so the only one is ours.
[ -n "$WPID" ] || WPID=$(tasklist //FI "IMAGENAME eq Dolphin.exe" //FO CSV //NH 2>/dev/null | head -1 | cut -d, -f2 | tr -d '"')
echo "dolphin windows pid: ${WPID:-unknown}"
sleep "$SECS"
# kill only the instance this script started, never every Dolphin on the machine
# taskkill /F only. A POSIX kill on a native process posts a window close and Dolphin then asks the user
# "Do you want to stop the current emulation?" -- never do that.
if [ -n "$WPID" ]; then taskkill //PID "$WPID" //F >/dev/null 2>&1 || true; else echo "no pid: leaving Dolphin running rather than posting a close"; fi
sleep 3
cp "$OUT/Dolphin.ini.orig" "$CFG/Dolphin.ini"; cp "$OUT/GFX.ini.orig" "$CFG/GFX.ini"; [ -f "$OUT/Logger.ini.orig" ] && cp "$OUT/Logger.ini.orig" "$CFG/Logger.ini"
rm -f "$S/autoinput.txt" "$S/settingsRX2022.cfg"
if [ -n "$ABOOT" ]; then if [ -f "$OUT/autoboot.txt.orig" ]; then cp "$OUT/autoboot.txt.orig" "$S/autoboot.txt"; else rm -f "$S/autoboot.txt"; fi; fi
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
