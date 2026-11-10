# RetroAchievements: Casual now, Hardcore later

Research of 2026-11-09 (sources at the end). "Fact" = read in the source; "inferred" = our reading.

## What RetroAchievements says

- **Unknown clients get Casual only (fact).** rcheevos' integration guide: if the User-Agent is missing,
  not recognized, or has no numeric version, the server turns Hardcore unlock requests into Casual
  (softcore) ones. Since April 2025, unsupported or deprecated clients are downgraded to softcore
  (search summary, not read in full). Nothing found says Casual unlocks from an unregistered client are
  refused (inferred: they are accepted).
- **Hardcore needs validation by RAdmin (fact):** "Please contact RAdmin to have your client validated
  for hardcore support" (message form: https://retroachievements.org/user/radmin).
- **User-Agent (fact):** `<product>/<version> (<system>) <extensions>`, unique and stable, numeric
  incrementing version. The extensions may carry the rcheevos version (`rc_client_get_user_agent_clause`).
  Any past use of another emulator's User-Agent must be disclosed (WiiStation never used one).
- **Eligibility for Hardcore (fact):** the emulator "or the parent emulator it is forked from" public for
  at least 6 months. Whether WiiSX/PCSX counts as WiiStation's parent is not stated: ask.
- **Local leaderboards and caching on the console:** nothing found against them (inferred); caching static
  set data and images is expected of clients. Ask to be sure.

## Casual, for 6.0.0

| Item | Status |
|---|---|
| Own login (token), Casual only (`rc_client_set_hardcore_enabled(client, 0)`) | done |
| User-Agent `WiiStation/6.0.0 (Wii)` | done: the rcheevos clause (`rc_client_get_user_agent_clause`) follows it, e.g. `WiiStation/6.0.0 (Wii) rcheevos/12.5` (Gamecube/ra_transport.c; a debug run logs it) |
| Rich Presence pings (rc_client's own timing: 30 s, then every 2 min) | done (rc_client) |
| Unlocks kept when offline, sent when back (rc_client retries; "offline" path in achievements_official.c) | done |
| Leaderboards: none submitted (Casual); this Wii's own table only (local, never sent) | done |
| Release notes: Official = Casual; Hardcore not available yet | in RELEASE_NOTES_6.0.0.md |
| Message RAdmin (below), ideally before the release is announced | Monty |

## Hardcore, after RAdmin answers

What the compliance page asks, against WiiStation today:

| Requirement | WiiStation now | Work |
|---|---|---|
| Achievement list easy to reach | the book (menu, jump from a pop-up) | none |
| Correct triggers, Measured and trigger indicators | progress pop-ups, challenge icons (can be hidden: Hardcore may hide pop-ups, fine) | check against the rules |
| Rich Presence, Leaderboards work and cannot be turned off | RP yes; leaderboards read-only, not submitted | submit boards in Hardcore (rc_client does it when Hardcore is on); trackers on screen; no setting turns RP/boards off in Hardcore |
| Offline unlocks cached securely, synced later | rc_client retry while running; not kept across a power-off | a signed/secure queue on the card if RA requires it across restarts |
| No cheats, rewind, slow motion, frame advance | no cheats/rewind/frame advance; CPU speed and per-game speed settings below 100 % exist | block speeds under 100 % and any slow-down setting in Hardcore |
| No state loading (saving allowed) | states load freely | block loads in Hardcore (menu + hotkeys + chain lines) |
| No memory editor, debugger, scripting, input playback | autoinput.txt / lab scripts / debug builds | refuse Hardcore when any of these is active; release build only |
| Casual -> Hardcore needs a game reset; a resume drops to Casual | n/a | reset on switch; a resumed state = Casual |
| Hardcore shown at game start | game card shows the mode? | "HARDCORE" on the game card |
| Transparency: privacy policy, licenses, free/paid matrix | GPL, free | a short PRIVACY.md (what is sent: login, unlocks, RP; kept on the card: token, cache) + license list |
| Eligibility: 6 months public | 6.0.0 is the first release | ask about the WiiSX/PCSX parent clause |

## Draft message to RAdmin (Monty sends it; edit freely)

> Subject: New client: WiiStation (PS1 emulator for the Nintendo Wii, homebrew)
>
> Hi! I develop WiiStation, a GPL PlayStation emulator that runs on real Nintendo Wii hardware
> (a fork of WiiSX, itself based on PCSX): https://github.com/Monsterray/WiiStation. It uses rcheevos'
> rc_client, with the User-Agent "WiiStation/6.0.0 (Wii)" plus the rcheevos clause, and has never sent
> another emulator's User-Agent. It runs Casual only: the player's own login, unlocks, Rich Presence; no
> leaderboard submissions.
>
> 1. Is a client like this fine to release with Casual enabled while we work toward Hardcore?
> 2. For the six-month rule: does WiiSX/PCSX count as the parent emulator, or does WiiStation's own first
>    public release (6.0.0) start the clock?
> 3. WiiStation evaluates a set's leaderboard definitions on the console to keep a local, never-submitted
>    table for the players on that Wii, and caches sets and badges on the SD card. Is that OK?
> 4. Anything you would like changed before we apply for Hardcore validation?
>
> Thanks, Monty

## Sources

- rcheevos wiki, rc_client integration: https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration
- Hardcore compliance requirements: https://docs.retroachievements.org/general/hardcore-compliance-requirements.html
- RAdmin contact: https://retroachievements.org/user/radmin
- Web API docs (rate limiting, caching): https://api-docs.retroachievements.org/
