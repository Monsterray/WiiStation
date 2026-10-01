#!/bin/bash
# wiistation_play.sh [release|debug|PATH] -- start, or restart, the interactive WiiStation window
# the user plays and tests in, so that it opens where it was last closed.
#
# Dolphin saves the position and size of its windows (User/Config/Qt.ini, "mainwindow/geometry"
# and "renderwidget/geometry") in the MainWindow destructor -- that is, only when it is closed
# normally (Source/Core/DolphinQt/MainWindow.cpp). A kill skips that, and the next start opens
# wherever the last normal close left it. So this script closes the running window the way a
# user does (a window close message), waits for Dolphin to save and exit, and only then starts
# the new build. It kills only if the window has not gone after CLOSE_WAIT seconds, and says so,
# because then the position is not saved.
#
# The window is found by its command line: the install's shared profile (no -u) and a WiiStation
# DOL. The isolated test profile (.dolphin, scripts/dolphin_run.sh) and other projects' Dolphin
# windows are never touched.
#
# -b (batch mode): Dolphin exits when emulation stops. Without it, closing the game window only
# stops emulation and leaves Dolphin's main window running, so Dolphin never gets to its
# destructor and saves nothing. The game window's geometry ("renderwidget/geometry") is saved in
# batch mode too; only the main window's is not, and batch mode never shows the main window.
# ConfirmStop=False is passed so that a close does not stop on "Do you want to stop the current
# emulation?". Both are command-line settings: Dolphin does not write them back to Dolphin.ini.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
D="/c/tools/Dolphin-x64/Dolphin.exe"
CLOSE_WAIT="${CLOSE_WAIT:-15}"
case "${1:-release}" in
	release) DOL="$REPO/Gamecube/WiiSXRX_Release.dol" ;;
	debug)   DOL="$REPO/Gamecube/WiiSXRX_debug.dol" ;;
	*)       DOL="$1" ;;
esac
[ -f "$DOL" ] || { echo "no such dol: $DOL"; exit 2; }
DOL_WIN="$(cygpath -m "$DOL")"

session_pids() {
	powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.Name -eq 'Dolphin.exe' -and \$_.CommandLine -like '*WiiSXRX_*.dol*' -and \$_.CommandLine -notlike '* -u *' } | ForEach-Object { \$_.ProcessId }" 2>/dev/null | tr -d '\r'
}

for pid in $(session_pids); do
	echo "closing WiiStation window (pid $pid) so Dolphin saves its position"
	powershell.exe -NoProfile -Command "\$p = Get-Process -Id $pid -ErrorAction SilentlyContinue; if (\$p) { [void]\$p.CloseMainWindow() }" 2>/dev/null
	t=0
	while [ $t -lt "$CLOSE_WAIT" ] && session_pids | grep -qx "$pid"; do sleep 1; t=$((t + 1)); done
	if session_pids | grep -qx "$pid"; then
		echo "pid $pid did not close in ${CLOSE_WAIT} s -- killing it; its window position is NOT saved"
		taskkill //PID "$pid" //F >/dev/null 2>&1
	else
		echo "closed after ${t} s"
	fi
done

# The SD folder (6 GB of games) is repacked into WiiSD.raw at every start with folder sync on:
# 27-35 s, and 7 s unpacking at exit. scripts/sd_sync.py first copies WiiStation's own files
# (cards, states, settings) from the image back into the folder, then says whether any of the
# user's files changed; if none did, this start skips the repack and Dolphin uses the image.
# Only when no other Dolphin has this profile open (one started by hand would be using the image).
LOAD="/c/tools/Dolphin-x64/User/Load"
SYNC=on
others=$(powershell.exe -NoProfile -Command "Get-CimInstance Win32_Process | Where-Object { \$_.Name -eq 'Dolphin.exe' -and \$_.CommandLine -notlike '* -u *' } | ForEach-Object { \$_.ProcessId }" 2>/dev/null | tr -d '\r')
if [ -z "$others" ]; then
	SYNC=$(python "$REPO/scripts/sd_sync.py" "$(cygpath -m "$LOAD")" | tee /dev/stderr | tail -1)
else
	echo "another Dolphin has this profile open (pid $others): SD folder sync stays on"
fi
[ "$SYNC" = off ] && SYNCARG="'-C','Dolphin.Core.WiiSDCardEnableFolderSync=False'," || SYNCARG=""
echo "SD folder sync: $SYNC"

echo "starting $(basename "$DOL") ($(stat -c %y "$DOL" | cut -c1-19))"
# DSPThread: the LLE DSP (needed for sound) on its own thread, not Dolphin's CPU thread
powershell.exe -NoProfile -Command "Start-Process -FilePath '$(cygpath -w "$D")' -ArgumentList '-b','-e','$DOL_WIN',$SYNCARG'-C','Dolphin.Core.DSPHLE=False','-C','Dolphin.DSP.DSPThread=True','-C','Dolphin.Interface.ConfirmStop=False'"
