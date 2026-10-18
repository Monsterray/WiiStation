# Light guns and the mouse: how they work and how to test them

WiiStation emulates three pointer devices on a PlayStation port: the Namco GunCon, the Konami
Justifier and the PlayStation Mouse. The Wii Remote's pointer (its IR camera and the sensor bar)
moves each of them. This page says what to set, which game to use, and what to check. Section 3
is what a Dolphin run checks without a person; section 4 needs a person at a real Wii.

## 1. How a port becomes a gun or a mouse

A port (or multitap slot) is a pointer device when all three hold (`Gamecube/PadSSSPSX.c`
`UpdateState`, and `pad_gun_chan` in `Gamecube/PlugPAD.c` for the reticle):

- `LightGun` in `settings.ini` is 1 (GunCon), 2 (Justifier) or 3 (Mouse). Menu: Input, Light Gun.
- `PadLightgunN` for that port is 1. The default is 1 for every port. Menu: Configure Buttons,
  Gun/Mouse button of that port.
- A Wii Remote, alone or with a Nunchuk, plays that port. A Classic Controller, a GameCube pad or
  any other controller on the port stays a pad.

| Type | ID (42h reply) | Pointer to the PlayStation | Buttons |
|---|---|---|---|
| GunCon | 63h | X = ir.x / 1.72 + 75, Y = ir.y / 2 + 25 (NTSC) or + 48 (PAL), each 16 bit; no pointer: X = 1, Y = 10 (off screen) | Fixed: B = trigger (Circle), A = gun A (Start), 1 = gun B (Cross). The button mapping is not used. |
| Justifier | 31h | No coordinates in the reply. Each vblank (`psxcounters.c`) arms `lightgunInterrupt`: IRQ10 at the line of ir.y / 2, timer 0 loaded from ir.x. Ports 1 and 2 only, and only after the game sends 10h in the second byte of 42h | The pad mapping of the Wii Remote: Square, Cross, Start. Which bit is the trigger is not checked on hardware. |
| Mouse | 12h | dX, dY: the pointer's movement since the last vblank, times the mapping's analog sensitivity, clamped to -128..127; no pointer: 0 | The pad mapping: the button mapped to R1 is the left button, L1 the right button. Cross held: no movement (lift the "mouse"). |

`ir.x`, `ir.y` are 640 x 480 TV pixels. With 16:9 Pillarbox (`ScreenMode = 2`) x is first moved
into the 4:3 picture (`x * 848 / 640 - 104`).

The reticle (`GunReticle = 1`, the default) is drawn on the picture sent to the TV, over the game
(`drawGunReticles`, `SoftGPU/drawGX.c`; called by both soft plugins and by OpenGX's `flipEGL`): a
small cross where the remote points, and a ring for a gun. Its colour is the remote's: green,
red, blue, yellow for remotes 1 to 4. It is not in the game's frame, so a VRAM dump or a save
state never holds it.

### Switching between types: what is kept

Nothing is stored per type, so switching loses nothing, but these limits follow from one key:

- `LightGun` is one value for the whole console. Port 1 cannot be a GunCon while port 2 is a mouse.
- `PadLightgunN` is on for every port by default. With `LightGun` set, a second player on a Wii
  Remote becomes a second gun unless that port's Gun/Mouse button is turned off.
- The Justifier and the mouse use the Wii Remote's pad mapping, the same mapping the remote has
  as a pad, and the mouse speed is that mapping's analog sensitivity. A mapping changed for the
  mouse is changed for the pad too.
- A per-game settings file holds `LightGun`, `GunReticle` and `PadLightgunN` like any other
  setting. In 5.13.1 such files were saved as `.cfg` and read as `.ini`; that is being fixed
  separately (`Func_SaveSettingsSeparately`). Until it is, a gun set for one game only does not
  come back.

## 2. Settings for every test

```
LightGun = 1            ; 1 GunCon, 2 Justifier, 3 Mouse
PadAutoAssign = 0       ; manual ports, so the remote stays where it is put
PadType2 = 2            ; port 2: a Wii controller
PadAssign2 = 0          ; Wii Remote 1
PadLightgun2 = 1
GunReticle = 1
```

Port 2 is used so that the remote's light shows its player (LED 2 in a game, LED 1 in the menu)
and the light can be checked in the same run. On a Wii, port 1 works the same way.

## 3. What Dolphin checks without a person

Dolphin's emulated Wii Remote with no host device bound sees no sensor bar (`ir.valid = 0`,
`num_dots = 0`, checked 2026-10-18), and test runs must not bind the host mouse or keyboard. Debug
builds therefore take a pointer from the input script: `irfake <vblank> <x> <y>` (up to 8 lines,
640 x 480) stands in for the remote's pointer while the remote reports none. It feeds the GunCon,
the Justifier, the mouse and the reticle, the same code paths a real pointer takes.

Profile (once): a named profile with Wii Remote 1 emulated and no device bound.

```
bash scripts/profile.sh new wiimote --game "Point Blank (USA)"
# in .profiles/wiimote/Config/WiimoteNew.ini, section [Wiimote1]: Source = 1
```

Run:

```
WSX_PROFILE=wiimote bash scripts/wsx.sh chain gun gun_reticle.txt --env XFB_RAM=1 --secs 360
WSX_PROFILE=wiimote bash scripts/wsx.sh run guncon --autoboot AB --input scripts/autoinput/gun_reticle.txt \
    --set LightGun=1,PadType1=0,PadType2=2,PadAssign2=0 --secs 70 --nodump --env XFB_RAM=1 --env FRAMES_DUMP=False
```

(`AB` is an autoboot file naming `sd:/wiistation/isos/Point Blank (USA)` and `Point Blank (USA).cue`.)
`scripts/chains/gun_reticle.txt` runs Point Blank four times: GunCon on OpenGX, GunCon on Soft
Fast, GunCon with `GunReticle=0`, Mouse on OpenGX. `scripts/autoinput/gun_reticle.txt` points at
(400, 300), then at (200, 150) from vblank 700, and saves the TV picture at vblanks 500 and 1000.

| Check | Where | Expected (measured 2026-10-18) |
|---|---|---|
| The port answers as the device | `tty.log`, `pad ... p=1 id=` | 63 (GunCon), 12 (Mouse) |
| GunCon coordinates | `padtrace.csv` of the single GunCon run: out_rx + 256 * out_ry = X, out_lx = Y | (400, 300): X 307, Y 175. (200, 150): X 191, Y 100 |
| Mouse movement, once per vblank | `padtrace.csv` of the chain (the last game's): out_rx, out_ry = dX, dY | vbl 10: 127, 127; vbl 11: 0, 0; vbl 701: 128, 128 (-128, -128); vbl 702: 0, 0 |
| Reticle drawn, both plugins, setting honoured | `xfb_01..06.bin` (`scripts/xfb2png.py`) | games 1 and 2: a green ring and cross at (400, 300) and (200, 150); game 3: none |
| LED: own number in the menu, PlayStation player in the game | `tty.log`, `led ...` | each game: `leds=2 game=1`; each return to the menu: `leds=1 game=0` |
| Settings read from settings.ini | `--set` writes the run's `settings.ini` | the ID above follows `LightGun` |

What Dolphin cannot check: the real pointer (sensor bar, distance, jitter), the Justifier's timing
against a game that reads it (none is in the library), the menu's Save writing `LightGun` and
`GunReticle` back (scriptable with `menukeys`, not done yet), a remote that disconnects and
reconnects, and rumble you can feel.

## 4. On the Wii, with a person

Install the build on the bench Wii (`scripts/wii_install.py` through the queue), then:

| Device | Game | Set | Check |
|---|---|---|---|
| GunCon | Point Blank (USA) (library) | `LightGun = 1`, remote on port 1 | The game's calibration target: shoot its centre; the shot lands where the reticle is. Aim at the four corners: the shots follow. Point off the screen and pull the trigger: Point Blank reloads. B fires, A and 1 are the gun's A and B. |
| GunCon, 16:9 | Point Blank | as above, `ScreenMode = 2` | The shots still land on the reticle at the picture's left and right edges. |
| Justifier | a Justifier game, not in the library (Lethal Enforcers I & II) | `LightGun = 2` | The calibration shot lands on the reticle; find which remote button is the trigger (the Square mapping) and note it here. |
| Mouse | a mouse game, not in the library | `LightGun = 3` | The cursor follows the remote's movement, not its position; it stops when the remote points off the screen; Cross held freezes it; R1 and L1 mappings click. Change the mapping's sensitivity: the speed changes. |
| Two players | Point Blank | `PadType2 = 2`, `PadAssign2 = 1`, both PadLightgun on | Two reticles, green and red; each remote fires its own player's shots. |
| Reticle off | any of the above | `GunReticle = 0` | No reticle; aiming still works. |
| Type switch | Point Blank | GunCon, then Mouse, then GunCon in the menu without leaving the game | The port answers as each type in turn; the button mapping and PadLightgun are unchanged. |
| Settings round trip | any | set Light Gun and save; quit and start WiiStation again | `settings.ini` has the value; per-game save (after the .cfg/.ini fix) brings it back for that game only. |

Report the shot positions against the reticle (a photo of the TV is enough): an offset that is
the same everywhere is a calibration constant (`75`, `25`/`48` in `UpdateState`), one that grows
toward the edges is the `1.72` scale.
