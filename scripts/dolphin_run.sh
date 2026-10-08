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
#          FRAMES_DUMP=False  no frame dump: for a long run (a chain), where it slows Dolphin
#                         and fills the disk
#          AUDIO_DUMP=1   write the mixed audio to the profile's Dump/Audio and collect it;
#                         compare two runs with scripts/wav_compare.py
#          WSX_PROFILE=<dir>  this run's Dolphin user directory (default <repo>/.dolphin)
#          WSX_ISOS=<list>    extra game folders to put on the card, comma separated, or
#                         "all" for every one. The game the autoboot file names is always
#                         copied; this is for a run that browses to another one.
#          DOLPHIN_ARGS   extra arguments appended verbatim, e.g. "-C Graphics.Settings.OverlayStats=True"
#          CARDS=1        put your own memory cards (the shared card's saves/*.mcd) on the
#                         test card. Default: none, so every game starts from a fresh card
#                         and a run does not change when you save a game in your own window
#                         (Ape Escape's boot-time card read moved every fingerprint).
#
# <seconds> is a limit, not a length: the run ends early if Dolphin exits on its own. A chained
# autoboot (see GamecubeMain.cpp) powers the console off after its last game, and Dolphin in
# batch mode exits then. Files in <out_dir>/card/ are put in the card's wiistation/ folder for the
# run and taken off after -- a chain's per-game input scripts go there.
#
# ---------------------------------------------------------------------------------------
# THE PROFILE. This runs against its own Dolphin user directory, not the install's shared
# User/, so it can run beside the user's own Dolphin session and beside another project's
# runs: nothing is shared except the CPU. The pattern is Wii64's .dev/dolphin_test.sh.
#
# Only two things have to be brought into it. Dolphin's SD folder sync is fixed at
# <userdir>/Load/WiiSDSync and does NOT follow junctions -- a junctioned folder syncs as
# empty, which Wii64 tested -- so the card's files are COPIED (cp -u, once) from the shared
# drop location the user puts games in. And every Dolphin setting the run depends on is
# passed with -C below, because a fresh profile has no Dolphin.ini to inherit them from.
# That is the better way round in any case: the run now says what it is, instead of
# quietly taking whatever the user last set in the GUI.
#
# The first run copies about 700 MB and Dolphin then builds the card image, so it takes a
# few minutes. Later runs reuse both.
# ---------------------------------------------------------------------------------------
#
# Stages the input script, settings and (optionally) autoboot.txt into the SD sync folder, boots the
# .dol in batch mode, kills Dolphin after <seconds>, keeps the last KEEP frames, extracts
# ptrace.log/vram.bin and reads perf.log straight from the FAT image (sdimage_read.py), then builds a
# contact sheet (sheet.py). Defaults for the optional files come from the
# WiiSDSync_paused_by_claude folder next to the shared sync dir.
#
# Every Dolphin setting is passed with -C <System>.<Section>.<Key>=<Value>. Nothing here writes to
# Dolphin.ini/GFX.ini/Logger.ini, so a run that dies half way cannot leave anything misconfigured.
# Note the system name for GFX.ini is "Graphics" -- "-C GFX.*" is silently ignored.
set -u
OUT="$1"; SECS="${2:-170}"; AIN="${3:-}"; SET="${4:-}"; ABOOT="${5:-}"
# Dolphin 2609 since 2026-10-01. Its User folder is a junction to /c/tools/Dolphin-x64/User (one
# copy, 16 GB). The old build stays as a fallback: DOLPHIN_DIR=/c/tools/Dolphin-x64.
D="${DOLPHIN_DIR:-/c/tools/Dolphin-2609}"
SP="$(cd "$(dirname "$0")" && pwd)"   # sheet.py and sdimage_read.py live next to this script
REPO="$(cd "$SP/.." && pwd)"
DOL="${DOL:-$REPO/Gamecube/WiiSXRX_debug.dol}"

# This run's own user directory, and the shared folder the user drops games into.
PROFILE="${WSX_PROFILE:-$REPO/.dolphin}"
SHARED="$D/User/Load/WiiSDSync/wiisxrx"        # the user's games (isos/), and BIOS/fonts/saves
SHARED_NEW="$D/User/Load/WiiSDSync/wiistation"  # WiiStation's own folder since 5.6.0, preferred
P="$D/User/Load/WiiSDSync_paused_by_claude"
S="$PROFILE/Load/WiiSDSync/wiistation"
# The games stay in the old folder, on this card as on the bench Wii's (it cannot be renamed
# over the network): chain files name sd:/wiisxrx/isos/<game>, a ROM folder like any other.
ISOS="$PROFILE/Load/WiiSDSync/wiisxrx/isos"
mkdir -p "$S" "$PROFILE/Logs" "$PROFILE/Load"
PROFILE_WIN=$(cd "$PROFILE" && pwd -W | tr '/' '\\')

# Only a Dolphin on THIS profile blocks a run: it would hold the same card image. Any other
# instance -- the user's own session, another project's runs -- has its own directory and is
# left strictly alone.
dolphin_instances() {   # prints "pid|commandline" per Dolphin.exe
  powershell.exe -NoProfile -Command 'Get-CimInstance Win32_Process | Where-Object Name -eq Dolphin.exe | ForEach-Object { "$($_.ProcessId)|$($_.CommandLine)" }' 2>/dev/null | tr -d '\r'
}
same_userdir_pids() {   # instances that use our profile
  dolphin_instances | while IFS='|' read -r pid cmd; do
    [ -n "$pid" ] || continue
    case "$cmd" in *"$PROFILE_WIN"*) echo "$pid";; esac
  done
}
BLOCKERS=$(same_userdir_pids | tr '\n' ' ')
if [ -n "${BLOCKERS// /}" ]; then
  echo "refusing to start: Dolphin PID(s) $BLOCKERS already use this profile ($PROFILE_WIN): same SD image. If stale from a killed run: taskkill //F //PID <pid>"; exit 2
fi
OTHERS=$(dolphin_instances | wc -l | tr -d ' ')
[ "$OTHERS" != "0" ] && echo "note: $OTHERS other Dolphin instance(s) are running on their own profiles; this run shares the CPU with them"
[ -f "$DOL" ] || { echo "no such .dol: $DOL"; exit 2; }
mkdir -p "$OUT/frames"

# --- Dolphin settings for this run only -------------------------------------------------
CFGARGS=(
  # A guest crash, reload or exit asks the host to stop; with ConfirmStop on, Dolphin puts up a
  # modal dialog and waits for a human, batch mode included.
  -C Dolphin.Interface.ConfirmStop=False
  # A fresh profile would otherwise ask about analytics on first launch.
  -C Dolphin.Analytics.PermissionAsked=True
  -C Dolphin.Analytics.Enabled=False
  # The SD card. A fresh profile has none of this set, and without the folder sync the guest
  # boots to an empty card.
  -C Dolphin.Core.WiiSDCard=True
  -C Dolphin.Core.WiiSDCardAllowWrites=True
  -C Dolphin.Core.WiiSDCardEnableFolderSync=True
  -C Dolphin.DSP.DSPThread=True
  -C Dolphin.Movie.DumpFrames=${FRAMES_DUMP:-True}
  -C Graphics.Settings.DumpFramesAsImages=True
  -C Graphics.Settings.PNGCompressionLevel=1
  # The three options the OpenGX renderer needs to behave like hardware (see README).
  -C Graphics.Settings.SafeTextureCacheColorSamples=0
  -C Graphics.Hacks.EFBToTextureEnable=False
  -C Graphics.Hacks.EFBAccessEnable=True
  # A panic dialog is as fatal to an unattended run as the stop dialog: Dolphin puts up a
  # modal box and waits for a human. They are still written to the log.
  -C Dolphin.Interface.UsePanicHandlers=False
  # The fault grep at the end reads these. Verbosity 4 and the PowerPC/memory categories are
  # what make "Unknown instruction" and "Invalid read" appear at all.
  -C Logger.Options.WriteToFile=True
  -C Logger.Options.Verbosity=4
  -C Logger.Logs.MASTER=True
  -C Logger.Logs.BOOT=True
  -C Logger.Logs.POWERPC=True
  -C Logger.Logs.MEMMAP=True
  -C Logger.Logs.OSREPORT=True
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

# --- put the card together --------------------------------------------------------------
# Small and always needed: the BIOS, the menu fonts and the memory cards.
for dir in bios fonts saves; do
  src="$SHARED_NEW/$dir"; [ -d "$src" ] || src="$SHARED/$dir"
  [ -d "$src" ] || continue
  mkdir -p "$S/$dir"
  cp -ru "$src/." "$S/$dir/" 2>/dev/null || true
done
# the memory cards: fresh ones unless CARDS=1 (a chain deletes the cards a game used after
# it, but the first boot of each game read whatever card was here)
[ "${CARDS:-0}" = 1 ] || { [ "$(cd "$S" && pwd -P)" != "$(cd "$SHARED" && pwd -P)" ] && rm -f "$S/saves/"*.mcd; }
# Games are hundreds of megabytes each, so only the ones this run can reach are copied: every
# line of the autoboot file that is a folder on the card -- one for a single game, one per game
# for a chain.
want_isos() {
  [ -n "$ABOOT" ] && tr -d '\r' < "$ABOOT" 2>/dev/null | sed -n 's|^sd:/wiisxrx/isos/||p'
  case "${WSX_ISOS:-}" in
    "")    ;;
    all)   ls "$SHARED/isos" 2>/dev/null ;;
    *)     echo "${WSX_ISOS}" | tr ',' '\n' ;;
  esac
}
want_isos | sed 's/[[:space:]]*$//' | sort -u | while read -r game; do
  [ -n "$game" ] && [ -d "$SHARED/isos/$game" ] || continue
  if [ ! -d "$ISOS/$game" ]; then
    echo "copying game to this profile's card (once): $game ($(du -sh "$SHARED/isos/$game" | cut -f1))"
  fi
  mkdir -p "$ISOS/$game"
  cp -ru "$SHARED/isos/$game/." "$ISOS/$game/" 2>/dev/null || true
done

# --- stage the guest's files ------------------------------------------------------------
rm -rf "$PROFILE/Dump/Frames" "$PROFILE/Dump/Audio"
# Dolphin APPENDS to dolphin.log across launches. Keep the previous one aside so the copy this
# run collects, and the fault grep at the end, describe this run only.
[ -f "$PROFILE/Logs/dolphin.log" ] && mv "$PROFILE/Logs/dolphin.log" "$PROFILE/Logs/dolphin.log.prev"
# the logs a run leaves: an earlier run's must not pass for this one's (a run cut off before
# its trace fired read the previous run's ptrace.log)
rm -f "$S/perf.log" "$S/tty.log" "$S/ptrace.log" "$S/vramio.log" "$S/atrace.log" "$S/lab.log" "$S/pcring.log" "$S/xfb.bin" "$S/"vram_*.bin "$S/"vtl_*.bin "$S/"hprof_*.bin "$S/"vsig_*.bin
cp "${AIN:-$P/autoinput.txt}" "$S/autoinput.txt"; cp "${SET:-$P/settingsRX2022.cfg}" "$S/settingsRX2022.cfg"
# autoboot: without it WiiStation sits in its menu
[ -n "$ABOOT" ] || echo "note: no autoboot file given -- the run will stay in the menu"
if [ -n "$ABOOT" ]; then cp "$ABOOT" "$S/autoboot.txt"; fi
CARDFILES=$(ls "$OUT/card" 2>/dev/null)
[ -n "$CARDFILES" ] && cp "$OUT/card/"* "$S/" && echo "card files: $(echo $CARDFILES)"

# --- run --------------------------------------------------------------------------------
echo "framedump run: start $(date +%T)"
echo "dol: $DOL ($(stat -c %y "$DOL" 2>/dev/null | cut -c1-19))"
echo "profile: $PROFILE_WIN"
printf 'options:'; printf ' %s' "${CFGARGS[@]}"; echo
printf '%s\n' "${CFGARGS[@]}" > "$OUT/dolphin-options.txt"
"$D/Dolphin.exe" -b -e "$DOL" -u "$PROFILE_WIN" "${CFGARGS[@]}" &
DPID=$!; sleep 2; WPID=$(ps -p $DPID 2>/dev/null | awk 'NR==2{print $4}')
# Fallback for the Windows PID: the only instance on our profile is the one just started.
[ -n "$WPID" ] || WPID=$(same_userdir_pids | head -1)
echo "dolphin windows pid: ${WPID:-unknown}"
# Wait for Dolphin to exit on its own (a chain powers off at its end), up to SECS.
T0=$(date +%s)
while [ $(( $(date +%s) - T0 )) -lt "$SECS" ]; do
  sleep 5
  if [ -n "$WPID" ] && ! tasklist //FI "PID eq $WPID" 2>/dev/null | grep -q " $WPID "; then
    echo "dolphin exited by itself after $(( $(date +%s) - T0 )) s"; WPID=""; break
  fi
done
# Kill only the instance this script started, and only with taskkill /F: a POSIX kill on a native
# process posts a window close, which raises the stop dialog instead of ending the run.
if [ -n "$WPID" ]; then taskkill //PID "$WPID" //F >/dev/null 2>&1 || true; else echo "no pid: leaving Dolphin running rather than posting a close"; fi
sleep 3

# --- unstage ----------------------------------------------------------------------------
# The card belongs to this profile alone, so only the per-run files are taken back off it.
rm -f "$S/autoinput.txt" "$S/settingsRX2022.cfg" "$S/autoboot.txt"
for c in $CARDFILES; do rm -f "$S/$c"; done

# --- collect ----------------------------------------------------------------------------
N=$(ls "$PROFILE/Dump/Frames" 2>/dev/null | wc -l); echo "frames dumped: $N"
ls "$PROFILE/Dump/Frames"/framedump_*.png 2>/dev/null | sed -E 's/.*framedump_([0-9]+)\.png/\1/' | sort -n | tail -${KEEP:-120} | while read i; do mv "$PROFILE/Dump/Frames/framedump_$i.png" "$OUT/frames/"; done
rm -rf "$PROFILE/Dump/Frames"
cp "$PROFILE/Logs/dolphin.log" "$OUT/dolphin.log" 2>/dev/null || true
if [ "${AUDIO_DUMP:-0}" != 0 ]; then
  cp "$PROFILE/Dump/Audio"/*.wav "$OUT/" 2>/dev/null && ls -la "$OUT"/*.wav | awk '{print "audio dump:", $5, $9}'
fi
# Guest-side artifacts: 7-Zip refuses the FAT image after a kill often enough that perf.log is read
# with our own cluster-chain reader instead.
CARD="$PROFILE/Load/WiiSD.raw"
[ -f "$CARD" ] || echo "no card image at $CARD -- did the folder sync run?"
"/c/Program Files/7-Zip/7z.exe" e -y -o"$OUT" "$CARD" 'wiistation/vram.bin' >/dev/null 2>&1 || echo "7z extract failed"
python "$SP/sdimage_read.py" "$CARD" wiistation/perf.log "$OUT/perf.log" 2>&1 | tail -1
# 7-Zip handed back a stale ptrace.log after a rewrite; our own FAT reader does not
python "$SP/sdimage_read.py" "$CARD" wiistation/ptrace.log "$OUT/ptrace.log" >/dev/null 2>&1
# VRAM transfers of the whole run (C0 reads, A0 loads, 80 moves, readback outcomes), repeats merged
python "$SP/sdimage_read.py" "$CARD" wiistation/vramio.log "$OUT/vramio.log" >/dev/null 2>&1 && [ -s "$OUT/vramio.log" ] && echo "vramio: $(sed -n 2p "$OUT/vramio.log" | sed 's/^# //'), $(grep -vc '^#' "$OUT/vramio.log") lines"
# the run's console text (SysPrintf: a PS1 program's printf under the HLE BIOS), debug builds
python "$SP/sdimage_read.py" "$CARD" wiistation/tty.log "$OUT/tty.log" >/dev/null 2>&1 && [ -s "$OUT/tty.log" ] && echo "tty: $(wc -l < "$OUT/tty.log") lines"
# the per-vblank timelines of a chain (debug builds; scripts/vtl_view.py)
for i in $(seq -w 1 16); do
	python "$SP/sdimage_read.py" "$CARD" "wiistation/vtl_$i.bin" "$OUT/vtl_$i.bin" >/dev/null 2>&1 || break
done
ls "$OUT"/vtl_*.bin >/dev/null 2>&1 && echo "vtl: $(ls "$OUT"/vtl_*.bin | wc -l) timeline(s)"
# the per-vblank guest signatures (debug builds; scripts/vsig_cmp.py)
for i in $(seq -w 1 16); do
	python "$SP/sdimage_read.py" "$CARD" "wiistation/vsig_$i.bin" "$OUT/vsig_$i.bin" >/dev/null 2>&1 || break
done
# the sampling profiles (PROBES=hprof builds; scripts/hprof_view.py)
for i in $(seq -w 1 16); do
	python "$SP/sdimage_read.py" "$CARD" "wiistation/hprof_$i.bin" "$OUT/hprof_$i.bin" >/dev/null 2>&1 || break
done
ls "$OUT"/hprof_*.bin >/dev/null 2>&1 && echo "hprof: $(ls "$OUT"/hprof_*.bin | wc -l) profile(s)"
# audio timeline (debug build, 'atrace <vblank>' in autoinput.txt); absent in most runs
python "$SP/sdimage_read.py" "$CARD" wiistation/atrace.log "$OUT/atrace.log" >/dev/null 2>&1 && [ -s "$OUT/atrace.log" ] && echo "atrace: $(wc -l < "$OUT/atrace.log") lines"
# the front XFB a scheduled `dump` wrote (scripts/xfb2png.py; XFB_RAM=1 for the real TV image)
python "$SP/sdimage_read.py" "$CARD" wiistation/xfb.bin "$OUT/xfb.bin" >/dev/null 2>&1 && [ -s "$OUT/xfb.bin" ] && echo "xfb: $(wc -c < "$OUT/xfb.bin") bytes"
# the interpreter's PC ring (psxinterpreter.c), written once if the PC leaves code
python "$SP/sdimage_read.py" "$CARD" wiistation/pcring.log "$OUT/pcring.log" >/dev/null 2>&1 && [ -s "$OUT/pcring.log" ] && echo "pcring: $(head -1 "$OUT/pcring.log")"
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
