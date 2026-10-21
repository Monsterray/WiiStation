"""pad_detect.py GCPADNEW.INI: point port 1 of a Dolphin GCPadNew.ini (the movie profile's copy)
at the USB game pad that is plugged in now, and map its rumble motor.

The user's own setup names one pad (Device = DInput/0/Logitech Dual Action); with a
Logitech RumblePad 2 plugged in instead, Dolphin found no such device and port 1 was dead
(2026-10-21). Pads of the same family share the button and axis numbering, so only the
Device line changes. Dolphin names a DirectInput pad by its OEM name, which Windows keeps
per VID/PID in the registry (...\\MediaProperties\\PrivateProperties\\Joystick\\OEM).

Prints one line for the movie script: what port 1 is now, or why it was left alone.
Exit 0 either way: the recording can still go ahead with the keyboard.
"""
import re
import subprocess
import sys
import winreg

# Logitech pads with the Dual Action's layout (buttons 1-12, X/Y + Z/Rz sticks, one hat)
SAME_LAYOUT = {"046D:C216", "046D:C218", "046D:C219"}
OEM_KEY = r"System\CurrentControlSet\Control\MediaProperties\PrivateProperties\Joystick\OEM"


def present_pads():
    """(VID:PID) of the HID game controllers plugged in now, in Windows' order."""
    ps = ("Get-PnpDevice -PresentOnly -Class HIDClass | Where-Object { $_.FriendlyName -match "
          "'game controller' } | ForEach-Object { $_.InstanceId }")
    try:
        out = subprocess.run(["powershell.exe", "-NoProfile", "-Command", ps], capture_output=True,
                             text=True, timeout=30).stdout
    except (OSError, subprocess.TimeoutExpired):
        return []
    ids = []
    for m in re.finditer(r"VID_([0-9A-F]{4})&PID_([0-9A-F]{4})", out.upper()):
        vp = f"{m.group(1)}:{m.group(2)}"
        if vp not in ids:
            ids.append(vp)
    return ids


def oem_name(vp):
    vid, pid = vp.split(":")
    for root in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        try:
            with winreg.OpenKey(root, rf"{OEM_KEY}\VID_{vid}&PID_{pid}") as k:
                name, _ = winreg.QueryValueEx(k, "OEMName")
                if name:
                    return name
        except OSError:
            pass
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    path = sys.argv[1]
    text = open(path, encoding="utf-8").read()
    m = re.search(r"(\[GCPad1\][^\[]*?Device = )([^\r\n]*)", text)
    if not m:
        print("port 1: no [GCPad1] Device line, left as it is")
        return
    current = m.group(2).strip()
    if not current.startswith("DInput/") or "Keyboard" in current:
        print(f"port 1: {current} (keyboard), left as it is")
        return
    pads = present_pads()
    family = [p for p in pads if p in SAME_LAYOUT]
    if not family:
        print(f"port 1: {current}" + ("" if pads else " -- no USB game pad is plugged in: the keyboard will not move it either"))
        return
    name = oem_name(family[0]) or {"046D:C216": "Logitech Dual Action", "046D:C218": "Logitech RumblePad 2 USB",
                                   "046D:C219": "Logitech Cordless RumblePad 2"}[family[0]]
    device = f"DInput/0/{name}"
    text = text[:m.start(2)] + device + text[m.end(2):]
    # rumble: the PlayStation game's motor through WiiStation's GameCube pad to the pad's own
    if not re.search(r"\[GCPad1\][^\[]*?Rumble/Motor = ", text):
        text = re.sub(r"(\[GCPad1\][^\[]*?)(\r?\n)(\[GCPad2\])", lambda k: f"{k.group(1)}{k.group(2)}Rumble/Motor = `Constant`{k.group(2)}{k.group(3)}", text, count=1)
    open(path, "w", encoding="utf-8").write(text)
    print(f"port 1: {device}" + (" (was " + current + ")" if device != current else "") + ", rumble on")


if __name__ == "__main__":
    main()
