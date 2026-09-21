#!/bin/bash
# dolphin_run.sh -- unattended WiiStation run in Dolphin with frame dumping.
#
#   usage: dolphin_run.sh <outdir> [seconds] [autoinput file] [settings file] [autoboot file]
#
#   env:   DOL=<path>     build to boot (default: the repo's debug .dol)
#          KEEP=<n>       frames to keep, the last ones (default 120)
#          SHEET_STEP=<n> contact-sheet stride (default 20)
#          CACHE=1        emulate the PPC data cache: catches missing DCFlushRange /
#                         DCInvalidateRange, which are invisible in a default Dolphin and
#                         corrupt memory on real hardware. Costs ~4x the wall clock, so it is
#                         for RELEASE TESTING, not for the ordinary iterate-and-measure loop.
#          XFB_RAM=1      present CPU-written framebuffers, so a libogc crash screen shows
#                         up in the frame dump (Dolphin otherwise keeps XFB copies on the GPU)
#          MMU=1          emulate address translation; wild pointers fault instead of landing
#                         somewhere harmless
#          DSP_LLE=1      run the DSP microcode for real instead of Dolphin's high-level
#                         reimplementations. Needed for any custom ucode -- HLE matches
#                         ucodes by hash and falls back to a WRONG one otherwise (libogc's
#                         AESND is not on its list). Uses the DSP ROMs in Sys/GC.
#          AUDIO_DUMP=1   write the mixed audio to User/Dump/Audio and collect it; compare
#                         two runs with scripts/wav_compare.py
#          DOLPHIN_ARGS   extra arguments appended verbatim, e.g. "-C Graphics.Settings.OverlayStats=True"
#
# Stages the input script, settings and (optionally) autoboot.txt into the SD sync folder, boots the
# .dol in batch mode, kills Dolphin after <seconds>, keeps the last KEEP frames, extracts
# ptrace.log/vram.bin and reads perf.log straight from the FAT image (sdimage_read.py), then builds a
# contact sheet (sheet.py). Paths are the constants below; adjust for another machine. Defaults for
# the optional files come from the WiiSDSync_paused_by_claude folder next to the sync dir.
#
# Every Dolphin setting is passed with -C <System>.<Section>.<Key>=<Value>, which layers over the
# user's configuration for this run only. Nothing here writes to Dolphin.ini/GFX.ini/Logger.ini, so a
# run that dies half way cannot leave the user's Dolphin misconfigured. Note the system name for
# GFX.ini is "Graphics" -- "-C GFX.*" is silently ignored.
set -u
OUT="$1"; SECS="${2:-170}"; AIN="${3:-}"; SET="${4:-}"; ABOOT="${5:-}"
D="/c/tools/Dolphin-x64"; S="$D/User/Load/WiiSDSync/wiisxrx"; P="$D/User/Load/WiiSDSync_paused_by_claude"
SP="$(cd "$(dirname "$0")" && pwd)"   # sheet.py and sdimage_read.py live next to this script
DOL="${DOL:-/c/projects/WiiStation/Gamecube/WiiSXRX_debug.dol}"

if tasklist 2>/dev/null | grep -qi "Dolphin.exe"; then
  echo "refusing to start: a Dolphin instance is already running (the SD image would be locked and the kill below would hit it)"; exit 2
fi
[ -f "$DOL" ] || { echo "no such .dol: $DOL"; exit 2; }
mkdir -p "$OUT/frames"

# --- Dolphin settings for this run only -------------------------------------------------
CFGARGS=(
  # A guest crash, reload or exit asks the host to stop; with ConfirmStop on, Dolphin puts up a
  # modal dialog and waits for a human, batch mode included.
  -C Dolphin.Interface.ConfirmStop=False
  -C Dolphin.Movie.DumpFrames=True
  -C Graphics.Settings.DumpFramesAsImages=True
  -C Graphics.Settings.PNGCompressionLevel=1
  # The two options the OpenGX renderer needs to behave like hardware (see README).
  -C Graphics.Settings.SafeTextureCacheColorSamples=0
  -C Graphics.Hacks.EFBToTextureEnable=False
  # A panic dialog is as fatal to an unattended run as the stop dialog: Dolphin puts up a
  # modal box and waits for a human. They are still written to the log.
  -C Dolphin.Interface.UsePanicHandlers=False
  -C Logger.Options.WriteToFile=True
  -C Logger.Logs.FRAMEDUMP=True
  # Host-side speed, next to WiiStation's own FPS counter. The guest's counter measures
  # EMULATED time (it reads the PPC time base), so it happily reports 40 fps while Dolphin
  # crawls in wall-clock; only these tell you what the host is actually managing.
  -C Graphics.Settings.ShowSpeed=True
  -C Graphics.Settings.ShowVPS=True
)
[ "${CACHE:-0}" != 0 ]   && CFGARGS+=(-C Dolphin.Core.AccurateCPUCache=True)
[ "${MMU:-0}" != 0 ]     && CFGARGS+=(-C Dolphin.Core.MMU=True)
[ "${XFB_RAM:-0}" != 0 ] && CFGARGS+=(-C Graphics.Hacks.XFBToTextureEnable=False)
[ "${DSP_LLE:-0}" != 0 ] && CFGARGS+=(-C Dolphin.Core.DSPHLE=False -C Logger.Logs.DSPLLE=True)
[ "${AUDIO_DUMP:-0}" != 0 ] && CFGARGS+=(-C Dolphin.DSP.DumpAudio=True)
# shellcheck disable=SC2206
[ -n "${DOLPHIN_ARGS:-}" ] && CFGARGS+=(${DOLPHIN_ARGS})

# --- stage the guest's files ------------------------------------------------------------
rm -rf "$D/User/Dump/Frames" "$D/User/Dump/Audio"
# Dolphin APPENDS to dolphin.log across launches. Keep the previous one aside so the copy this
# run collects, and the fault grep at the end, describe this run only.
[ -f "$D/User/Logs/dolphin.log" ] && mv "$D/User/Logs/dolphin.log" "$D/User/Logs/dolphin.log.prev"
cp "${AIN:-$P/autoinput.txt}" "$S/autoinput.txt"; cp "${SET:-$P/settingsRX2022.cfg}" "$S/settingsRX2022.cfg"
# autoboot: without it WiiStation sits in its menu (the user keeps their own file renamed to .disabled)
[ -n "$ABOOT" ] || echo "note: no autoboot file given -- the run will stay in the menu unless $S/autoboot.txt already exists"
if [ -n "$ABOOT" ]; then [ -f "$S/autoboot.txt" ] && cp "$S/autoboot.txt" "$OUT/autoboot.txt.orig"; cp "$ABOOT" "$S/autoboot.txt"; fi

# --- run --------------------------------------------------------------------------------
echo "framedump run: start $(date +%T)"
echo "dol: $DOL ($(stat -c %y "$DOL" 2>/dev/null | cut -c1-19))"
printf 'options:'; printf ' %s' "${CFGARGS[@]}"; echo
printf '%s\n' "${CFGARGS[@]}" > "$OUT/dolphin-options.txt"
"$D/Dolphin.exe" -b -e "$DOL" "${CFGARGS[@]}" &
DPID=$!; sleep 2; WPID=$(ps -p $DPID 2>/dev/null | awk 'NR==2{print $4}')
# Fallback for the Windows PID: the guard above proved no Dolphin was running, so the only one is ours.
[ -n "$WPID" ] || WPID=$(tasklist //FI "IMAGENAME eq Dolphin.exe" //FO CSV //NH 2>/dev/null | head -1 | cut -d, -f2 | tr -d '"')
echo "dolphin windows pid: ${WPID:-unknown}"
sleep "$SECS"
# Kill only the instance this script started, and only with taskkill /F: a POSIX kill on a native
# process posts a window close, which raises the stop dialog instead of ending the run.
if [ -n "$WPID" ]; then taskkill //PID "$WPID" //F >/dev/null 2>&1 || true; else echo "no pid: leaving Dolphin running rather than posting a close"; fi
sleep 3

# --- unstage ----------------------------------------------------------------------------
rm -f "$S/autoinput.txt" "$S/settingsRX2022.cfg"
if [ -n "$ABOOT" ]; then if [ -f "$OUT/autoboot.txt.orig" ]; then cp "$OUT/autoboot.txt.orig" "$S/autoboot.txt"; else rm -f "$S/autoboot.txt"; fi; fi

# --- collect ----------------------------------------------------------------------------
N=$(ls "$D/User/Dump/Frames" 2>/dev/null | wc -l); echo "frames dumped: $N"
ls "$D/User/Dump/Frames"/framedump_*.png 2>/dev/null | sed -E 's/.*framedump_([0-9]+)\.png/\1/' | sort -n | tail -${KEEP:-120} | while read i; do mv "$D/User/Dump/Frames/framedump_$i.png" "$OUT/frames/"; done
rm -rf "$D/User/Dump/Frames"
cp "$D/User/Logs/dolphin.log" "$OUT/dolphin.log" 2>/dev/null || true
if [ "${AUDIO_DUMP:-0}" != 0 ]; then
  cp "$D/User/Dump/Audio"/*.wav "$OUT/" 2>/dev/null && ls -la "$OUT"/*.wav | awk '{print "audio dump:", $5, $9}'
fi
# Guest-side artifacts: 7-Zip refuses the FAT image after a kill often enough that perf.log is read
# with our own cluster-chain reader instead.
"/c/Program Files/7-Zip/7z.exe" e -y -o"$OUT" "$D/User/Load/WiiSD.raw" 'wiisxrx/ptrace.log' 'wiisxrx/vram.bin' >/dev/null 2>&1 || echo "7z extract failed"
python "$SP/sdimage_read.py" "$D/User/Load/WiiSD.raw" wiisxrx/perf.log "$OUT/perf.log" 2>&1 | tail -1
# 0 bytes is normal for a short run: the guest writes a perf report every N presents, and a run
# that ends before the first one has nothing to read.
echo "run done $(date +%T)"

# --- summarise ---------------------------------------------------------------------------
FIRST=$(ls "$OUT/frames" 2>/dev/null | sed -E 's/framedump_([0-9]+)\.png/\1/' | sort -n | head -1)
LAST=$(ls "$OUT/frames" 2>/dev/null | sed -E 's/framedump_([0-9]+)\.png/\1/' | sort -n | tail -1)
echo "kept frames ${FIRST:-none}..${LAST:-none}"
if [ -n "$FIRST" ]; then python "$SP/sheet.py" "$OUT/sheet.png" "$OUT/frames" "$FIRST" "${SHEET_STEP:-20}" "$LAST" 6 206 && echo "sheet: $OUT/sheet.png"; else echo "no frames kept"; fi
grep -a -E "^--- perf|^sio|offsoft|ogxoff" "$OUT/perf.log" 2>/dev/null | tail -4
# Guest-side faults and emulator complaints worth seeing without opening the log. The log was
# rotated before the run, so anything here happened during it.
F=$(grep -a -c -E "Unknown instruction|Invalid (read|write)|PANIC|Exception" "$OUT/dolphin.log" 2>/dev/null || echo 0)
echo "faults in dolphin.log: $F"
[ "$F" != "0" ] && grep -a -E "Unknown instruction|Invalid (read|write)|PANIC|Exception" "$OUT/dolphin.log" | head -5
exit 0
