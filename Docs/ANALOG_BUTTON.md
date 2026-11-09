# The ANALOG button

How the ANALOG button (or switch) and its light work on each PlayStation controller, what a game
sees, and what WiiStation models. Written 2026-10-19 for the ANALOG key combination, which replaced
the 5.14.0 rule that turned the pad analog by itself when a game left config mode.

## 1. What a game sees

The second byte of every reply is the controller's ID (psx-spx, "Controller ID"):

| ID | Meaning | Light |
|---|---|---|
| 41h | Digital pad, or an analog pad in digital mode. The reply has the buttons only. | Off |
| 73h | Analog pad in analog mode. The reply adds the two sticks (and L3/R3). | Red |
| 53h | Analog Joystick (SCPH-1110), or a Dual Analog in its green "flight" mode | Green |
| F3h | Config mode (after command 43h 01h), DualShock only | - |
| 79h | DualShock 2 with its pressure buttons on. PS2 games only (command 4Fh). | Red |

A game made only for the digital pad sees 73h as an unknown controller: it may ignore every button
or think the pad is unplugged (Wikipedia, "Dual Analog Controller").

## 2. Each controller

| Model | Control | Modes and IDs | Can the game set or lock the mode? | Power-on |
|---|---|---|---|---|
| SCPH-1080 digital pad | none | 41h only | No (no config mode) | 41h |
| SCPH-1110 Analog Joystick | a slide switch on the base, not a button | Digital 41h, analog 53h | No | the switch's position |
| SCPH-1150 Dual Analog (Japan, one motor), SCPH-1180 (US/Europe, no motor) | ANALOG button | Digital 41h (off), analog 73h (red), flight 53h (green, emulates the SCPH-1110) | No: it has no config mode. Only the player changes the mode. | Digital |
| SCPH-1200 DualShock | ANALOG button, slightly recessed | Digital 41h (off), analog 73h (red). No green mode. | Yes: 44h sets the mode, its key byte 3 locks the button | Digital |
| SCPH-110 DualShock (PSone) | ANALOG button | As SCPH-1200 (same protocol, other stick range) | As SCPH-1200 | Digital |
| SCPH-10010 DualShock 2, in a PS1 | ANALOG button | As SCPH-1200 for PS1 games. 45h says type 03h (01h on a PS1 pad); a bad 44h LED value sets its error byte to FFh. 79h only after 4Fh, which no PS1 game sends. | As SCPH-1200 | Digital |

The Dual Analog's button goes digital, red, green. The sources agree on that order; none checked
here says whether a press on green goes back to digital in one step. WiiStation does not model
the Dual Analog (section 4).

## 3. The DualShock (SCPH-1200) in detail

- **Power-on:** digital mode, light off, button unlocked (psx-spx "Analog Mode Note"; DuckStation
  `AnalogController::Reset`; Mednafen `InputDevice_DualShock::Power`).
- **A press** switches digital <-> analog. It also stops the motors and forgets the motor map that
  command 4Dh set ("A similar reset occurs when the user pushes the Analog button", psx-spx, command
  43h). If the game has used config mode, the byte after the ID reads 00h instead of 5Ah from then
  on, which tells the game the mode changed (psx-spx; DuckStation sets it back to 5Ah when the game
  enters config mode again).
- **44h (Set LED state), config mode only:** byte 3 = 00h digital, 01h analog, other values ignored.
  Byte 4 is the key: 03h (any value AND 3 = 3) locks the mode, so the button does nothing; 00h..02h
  unlock it (psx-spx; DuckStation and Mednafen lock on 03h). 44h also resets the motor map.
- **45h (Get LED state):** byte 4 of the reply is 00h off or 01h red. Games read it to show "analog
  on/off" or to decide whether to use the sticks.
- **Leaving config mode (43h 00h) does not change the mode** (psx-spx, DuckStation, Mednafen). A game
  that never sends 44h, such as MediEvil, leaves the choice to the player's button.
- **Reset button:** the console's reset does not cut the pad's power, so the mode stays. But once
  config mode has been used, a watchdog in the pad resets it to digital (light off, motors locked)
  if it is not polled for about one second, which can happen while the console reboots (psx-spx,
  command 43h). Emulators reset the pad to digital: DuckStation on Reset (with an option to force
  analog), Mednafen on power-on.
- **The button and config mode:** neither psx-spx nor the emulators block the button while the pad
  is in config mode. DuckStation queues a press until the transfer in progress ends; Mednafen
  checks it between transfers.

What games do with it, measured in WiiStation (tty.log `pad` lines, 2026-10-19):

| Game | What it sends | Result |
|---|---|---|
| MediEvil | 43h 01h, 45h, 4Ch, 47h, 46h, then 43h 00h. Never 44h. | Stays digital (41h) until the player presses ANALOG. After the press it enters config mode again, reads 45h and maps the motors (4Dh 00 01 FF..). |
| Ape Escape | 44h 01h 03h at vblank 307 | Analog, locked: the button does nothing. |
| Spyro the Dragon | 44h 01h 00h at vblank 15 | Analog, unlocked: each press switches the mode, and the game maps the motors again after each one. |

## 4. What WiiStation models

PSX Pad Type **Analog** (`ControllerType = 1`) is the SCPH-1200 DualShock, as it already was for the
config commands (Docs/CONTROLLER_TESTING.md). Its ANALOG button is a key combination on the Wii
controller (SETTINGS.md section 6.6; `analog_button` in `Gamecube/PadSSSPSX.c`):

- digital at each game start, unlocked (each game start is a power-on);
- a press switches 41h <-> 73h, unless the game locked the mode with 44h key 3;
- the press stops the motors and clears the 4Dh map; after config mode was used the byte after the
  ID reads 00h until the next 43h;
- per PlayStation port and multitap slot (a Co-Op port: any player's combination);
- a note on screen for two seconds: "Port 1: Analog (red light)", "Port 1: Digital (light off)",
  "Port 1: ANALOG locked by the game", or "no ANALOG button" for Digital and Stick Pad.

### AnalogAuto: ready to play (2026-11-01)

A real pad needs the player's press for a game like MediEvil or Gex 3, which ask the pad what it can
do (43h 01h, 45h, 4Ch, 47h, 46h, 43h 00h) and never send 44h. Gex 3 (measured in Dolphin, run
`gex3_a`): the sequence runs at vblanks 50 to 59, ID 41h before and after, and the stick is dead in
the game until the player presses ANALOG. After a press it asks again (43h 01h ... at once) and reads
the sticks, so it does use them. WiiStation's setting `AnalogAuto` (default 1) presses the button
for the player 600 vblanks (10 s) after such a game leaves config mode, if no pad has been sent
44h and the player has not pressed the combination by then: analog (73h), and the byte after the ID
reads 00h until the game's next 43h, which is how a real press tells the game. The wait is because
Ape Escape and Spyro also leave config mode (to detect the pad) and send 44h later: Spyro 3 vblanks
after, Ape Escape 278 (vblank 29, then 307). An immediate switch changed their SIO traffic and broke
a recorded Ape Escape replay; with the wait their runs are bit-identical to before (vsig_cmp on
the user's 14399-vblank recording, replay_rest, replay3). Measured: Gex 3 leaves config mode at
vblank 58 and switches at 658, MediEvil 19 and 619. Only once, only on a connected pad, and never after
a 44h to any pad or a combination press, so a player's later choice stands. A game that never enters config mode
is not touched: it is a digital-only game or a Dual Analog era game, and 73h could make a
digital-only one ignore its buttons. `AnalogAuto = 2` starts every game in analog mode for those
Dual Analog era games; `0` is the real pad.

Not modelled: the Dual Analog's green mode (53h) and the Analog Joystick, the watchdog reset, and
the queueing of a press until a transfer ends (WiiStation reads the combination when the game
reads the pad, between transfers).

## Sources

- psx-spx, "Controllers and Memory Cards": Controller ID, Analog Mode Note, Configuration Commands
  (43h, 44h, 45h, 4Dh), Dual Analog Pad in LED=Green Mode. https://psx-spx.consoledev.net/controllersandmemorycards/
- DuckStation `src/core/analog_controller.cpp` (Reset, ProcessAnalogModeToggle, SetAnalogMode,
  commands 43h/44h/45h). https://github.com/stenzek/duckstation
- Mednafen `src/psx/input/dualshock.cpp` (Power, CheckManualAnaModeChange, 44h lock), and its
  `psx.input.analog_mode_ct` combination (Select+Start+L1+R1+L2+R2 held about 1 s).
- Wikipedia, "Dual Analog Controller" and "DualShock" (button, light colours, the green mode).
- Wikipedia, "PlayStation Analog Joystick" (the mode switch).
