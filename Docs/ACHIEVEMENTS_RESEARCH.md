# Achievements: research

This file records what we learned from outside sources. The engine reference is
[ACHIEVEMENTS.md](ACHIEVEMENTS.md). Test evidence and open work are in
[ACHIEVEMENTS_NOTES.md](ACHIEVEMENTS_NOTES.md).

## Sources and pins

| Source | Pin | Researched |
|---|---|---|
| rcheevos | [`683197d8dc92555a51e8280a4123d071ed66bff3`](https://github.com/RetroAchievements/rcheevos/tree/683197d8dc92555a51e8280a4123d071ed66bff3) (vendored in `deps/rcheevos`) | 2026-10-10 |
| DuckStation | [`cd024df237d46faa4c65574b101261b31267e2fd`](https://github.com/stenzek/duckstation/tree/cd024df237d46faa4c65574b101261b31267e2fd) (master when inspected) | 2026-10-10 |
| Mbed TLS | 3.6.4 | 2026-10-11 |
| curl | 8.16.0 | 2026-10-11 |
| Wii curl/Mbed TLS port | [`d9b21a24142c6bfbfff15cd4151ef154b9659c4e`](https://github.com/AndrewPiroli/wii-curl/tree/d9b21a24142c6bfbfff15cd4151ef154b9659c4e), release [c8.16.0+m3.6.4](https://github.com/AndrewPiroli/wii-curl/releases/tag/c8.16.0%2Bm3.6.4) | 2026-10-11 |

- The research downloaded source text only. It ran no outside binary.
- DuckStation source was read into `/tmp/wiistation-duckstation-ra`. No DuckStation program ran
  and no DuckStation source changed.
- No login, account creation, registration, SDK download, award, leaderboard submission,
  progress reset or hardware purchase occurred.
- The rcheevos pin is for reproducibility. It is not a release recommendation.
- The authoritative documents are the
  [rc_client integration guide](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration)
  and the current
  [Hardcore compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html).
  Check API signatures against the pinned headers. Do not copy old wiki examples.

## Why RetroAchievements

RetroAchievements (RA) with its MIT-licensed C library
[rcheevos](https://github.com/RetroAchievements/rcheevos) is the only service we found that
supplies both community PS1 sets and a runtime made for emulators. That is a comparison, not
proof that no other service exists. See
[RA system and emulator support](https://docs.retroachievements.org/general/emulator-support-and-issues.html).

- The rcheevos README recommends the official-release `master` branch and `rc_client` for new
  clients. It has custom file handlers for formats such as CHD. It has no network code.
- The runtime is portable in principle. The authoring toolkit is Windows-only.
- The client guide recommends: start with Hardcore off, use token login after the first
  login, send a unique versioned client identity, and ask the RA administrators for Hardcore
  validation.
- `rc_client` supplies evaluation and service logic. The emulator supplies memory reads,
  HTTP/HTTPS and the UI.
- Casual first, Hardcore later, is our staging. rcheevos does not require it. The guide starts
  development with Hardcore off.

## rc_client API

The pinned
[rc_client.h](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_client.h)
gives:

| Need | API |
|---|---|
| Create, read memory, send requests | `rc_client_create(read_memory, server_call)` |
| Get gameplay and status events | `rc_client_set_event_handler` |
| Password or remembered-token login | `rc_client_begin_login_with_password`, `rc_client_begin_login_with_token` |
| Load with a known RA hash | `rc_client_begin_load_game` |
| Identify with hash callbacks | `rc_client_begin_identify_and_load_game` (with `RC_CLIENT_SUPPORTS_HASH`) |
| Each emulated frame / menus | `rc_client_do_frame` / `rc_client_idle` |
| After an emulator reset | `rc_client_reset` |
| Keep Casual trigger progress | `rc_client_progress_size` and the sized serialize/deserialize APIs |
| Disc change | `rc_client_begin_change_media(client, hash, callback, userdata)` takes a hash. `rc_client_begin_identify_and_change_media` identifies a file. Some wiki examples are older than that split. |
| Unload, shut down | `rc_client_unload_game`, `rc_client_destroy` |

- The calls return asynchronous work. Keep state until completion and handle cancellation.
- Hardcore defaults to on in the pinned header. Set it off immediately after creation.
- Aborting an `rc_client` handle suppresses its high-level completion callback. The transport
  must still finish its own callback and lifetime work. Upstream has a regression test for a
  delayed completion after `rc_client_destroy`. Abort does not close a socket.
- The pinned header uses `rc_client_set_unpromoted_enabled` and the `PROMOTED`/`UNPROMOTED`
  categories. DuckStation's bundled version uses `set_unofficial_enabled` and
  `begin_change_media_from_hash`.
- The pinned header has no `rc_client_get_pending_unlock_count`. DuckStation's pending-count
  display cannot be copied. Use public events and transport observations for pending and
  confirmed states.
- [Client activation code](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c#L1783)
  reads memory during load to validate addresses. It can defer activation to frame or idle
  processing.
- [Upstream client tests](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/test/client/test_rc_client.c)
  use a fake asynchronous `server_call`, canned JSON and a controlled millisecond clock. They
  cover delayed, aborted and destroyed login, award retry and HTTP 429. They also test
  spectator mode.
- [Client implementation](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c).

### Frames and threads

- The [integration guide](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration)
  requires processing every emulated frame, also frames skipped for fast-forward or speed.
  During a pause, call `rc_client_idle`. It does the periodic communication. A host-present
  callback alone cannot be the evaluation hook.
- WiiStation uses one PS1 frame boundary that all GPU and CPU backends share, in
  `psxcounters.c`.

### Transport contract

- The guide's transport and login example uses GET when there is no POST body and POST when
  there is one. It passes status and body to the callback for every outcome. It keeps the
  returned user name and token for later logins, not the password.
- The [request contract](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_api_request.h)
  exposes the URL, the POST data and content type, the response body length and status, and
  separate retryable and non-retryable client errors.
- The [request dispatch](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c#L4791)
  destroys the temporary request storage as soon as `server_call` returns. An asynchronous
  transport must copy the URL, the body and the headers first.

### Spectator and unpromoted

- Set spectator mode before any game loads, with Hardcore off. The pinned client then skips
  session creation, does not schedule the presence ping for that load, and suppresses award
  and leaderboard requests.
- Spectator mode cannot be turned off for a loaded game. Unload first. Spectator mode turned
  on after a normal session starts does not make the session read-only.
- Unpromoted achievements never submit awards, also outside spectator mode. A normal session
  still publishes session and presence activity.
- Spectator and unpromoted tests suppress awards. They cannot prove end-to-end submission.

### Connect API requests

The Connect API normally sends encoded POSTs to `https://retroachievements.org/dorequest.php`.
Classify a request by its `r` operation, not by the HTTP verb. The table gives protocol intent.
It does not promise that the server logs nothing. Sources:
[runtime request builders](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/api/rc_api_runtime.c),
[user request builders](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/api/rc_api_user.c).

| Operation | Effect |
|---|---|
| `gameid`, `achievementsets`, legacy `patch`, `unlocks`, image GETs | Retrieval. No intended change, but an authenticated retrieval shows the identity. |
| `login2` | Token issue or validation. A sensitive account action. |
| `startsession` | Starts a play session. Treat it as a write, though it returns existing unlocks. |
| `ping` | Publishes session and Rich Presence activity. A write. |
| `awardachievement` | Changes official achievement progress. A write. |
| `submitlbentry` | Changes leaderboard records. |

- The [public Web API](https://api-docs.retroachievements.org/) is for site data. It is not an
  award endpoint. Its API key is not the login token.
- The [Standalones guide](https://api-docs.retroachievements.org/connect/standalone.html) is for
  native games on RA's Standalones console, with admin-made pages and integration credentials.
  "Standalone emulator" does not mean WiiStation should use it.
- We found no public RA sandbox server. Get a supported test procedure from upstream first.

## PS1 hashing

- The [PS1 hash](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_hash_disc.c#L930)
  opens track 1. It finds the boot executable through the `BOOT` entry of `SYSTEM.CNF` and falls
  back to `PSX.EXE`. A missing primary executable fails identification.
- The MD5 input is the executable file name, then the executable content. With a `PS-X EXE`
  header, the little-endian value at byte offset 28 is the payload size, and 2048 bytes are
  added for the header.
- A whole-file MD5, a file name, a serial or WiiStation's `CdromId` is not an RA hash. Regions,
  revisions, patches and related engines need the upstream algorithm. Call it. Do not write it
  again. Hash the sectors of the disc in the drive, not a card file name or save names.
- The [default CD reader](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_cdreader.c)
  sends CUE and GDI to their parsers and other paths to its binary-track reader. It detects the
  sector layout and strips sector headers. BIN/CUE and raw or cooked ISO thus need no container
  decoder. It does not decompress CHD or PBP.
- The [hash dispatcher](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_hash.c)
  accepts CHD as a candidate but does not decode it. It infers PSP from a plain `.pbp`.
  WiiStation must select `RC_CONSOLE_PLAYSTATION`, give the decompressed sectors of the
  selected PBP disc and use a custom reader. Do not hash compressed container bytes. Do not
  always select PBP disc 0.
- The [rc_hash.h contract](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_hash.h)
  needs track open and close, absolute-sector reads and the first sector of a track. A read
  returns its byte count. File callbacks use 64-bit offsets and the standard seek origins.
  Prefer per-iterator callbacks or `rc_client_set_hash_callbacks`. The global registration APIs
  are deprecated.
- Our adapter must return logical sector user data, honour byte spans, track origins and
  pregaps, and use its own handle. Hashing must not move the live emulation reader.
- DuckStation's [hash](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L902-L925)
  is a second reference for valid PS-X EXE discs.

## Memory map

The table is in [ACHIEVEMENTS.md](ACHIEVEMENTS.md#memory-map). Source: the
[rcheevos PS1 memory regions](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/runtime/rc_consoleinfo.c#L803).

- The published table has no BIOS, VRAM or hardware-register region. Do not expose guessed CPU
  mirror addresses.
- Return raw emulated bytes. Do not apply `SWAP16` or `SWAP32`. Do not cast to native
  `uint32_t`.
- Clip reads at the valid bounds, return the real count, and return 0 when memory is not
  available.
- The `psxM`/`psxH` binding is our inference. Test it on every CPU core.

## Hardcore rules

From the current
[compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html):

- Required: correct triggers and indicators (measured and trigger states), an achievement
  list, Rich Presence, leaderboards, secure offline queueing, a unique incrementing client user
  agent, and approval eligibility.
- Not allowed in Hardcore: state loads, rewind, slowdown, frame advance, gameplay cheats,
  debugging, memory editing, TAS and input playback.
- Allowed: saving states.
- A resume must drop to Casual. Entering Hardcore needs a full reset.
- A Hardcore-default login is recommended, not required. The older wiki text is stricter.
- Eligibility: six months of public release, or a qualifying parent emulator. Confirm how
  WiiStation qualifies.
- Licensing and privacy obligations apply.
- Standard memory-card formats are recommended. Keeping hit counts in Casual save states is
  recommended.
- Do not impersonate RetroArch or PCSX for approval. An accepted login is not approval.

More sources:

- The [FAQ](https://docs.retroachievements.org/general/faq.html) allows fast-forward. That does
  not make every speed hack acceptable.
- The rcheevos [libretro option restrictions](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/libretro/rc_libretro.c)
  ban some PCSX-ReARMed clock and forced-PAL settings and SwanStation underclock settings.
  These rules are for those cores. They are not a WiiStation list. Send CPU clock, region
  forcing, cycle hacks, GPU timing fixes and patch or cheat classes to RA for review. Turn off
  unreviewed settings that change gameplay.
- Honour `RC_CLIENT_EVENT_RESET`: reset the game, then tell the client.
- The [player rules](https://docs.retroachievements.org/guidelines/users/global-leaderboard-and-achievement-hunting-rules.html)
  ban unsuitable imported saves and unapproved Hardcore clients. The user's recovered memory
  cards are not evidence of past achievements. Ask RA before any promise of Hardcore credit
  from migrated cards. Do not add bulk awards or local-to-Hardcore upload without service
  guidance.

## Offline play

- The [official FAQ](https://docs.retroachievements.org/general/faq.html#can-i-earn-achievements-offline)
  says a fully offline start is not supported. Start online, then keep the emulator open
  through an outage until it connects again.
- The pinned client keeps retry callbacks in memory. It retries transient award and submission
  failures with exponential delays, capped at two minutes. It raises disconnected and
  reconnected events.
- The progress serializer calls the runtime serializer. It does not export a durable network
  outbox. The compliance document expects a durable secure queue, which is more than the
  public client API gives. Restart-safe queueing is open upstream work.
- Never make fake server replies for an offline login or session. Reconnect with the original
  session, account, hash and mode, and the true unlock time. Show terminal server errors.

## Multi-disc

- Use the current media-change APIs after hashing the new disc. Keep the session for a known
  disc of the same game. Handle an unknown or unrelated disc before more evaluation. Check BIOS
  tray timing, hash cancellation and PBP disc selection.
- The official [multi-disc tutorial](https://docs.retroachievements.org/general/tutorials/multi-disc-games.html)
  shows playlists and tray operations for some frontends. Its `.m3u` workflow does not require
  WiiStation to drop its swap UI.

## HTTPS on the Wii

- The bench probe used the checked
  [Wii curl 8.16.0 / Mbed TLS 3.6.4 packages](https://github.com/AndrewPiroli/wii-curl/releases/tag/c8.16.0%2Bm3.6.4)
  with strict CA and host-name checks, bounded bodies, HTTPS only, timeouts and no redirects.
- With this backend, the auxiliary verification field stays zero after a failed verification.
  A rejection shows as curl error 60, HTTP status 0 and the diagnostic text.
- The probe pins a 2024 CA snapshot for reproducibility. It is not a production update policy.
- The probe is now built from source on libogc v3 (no prebuilt packages). See the notes,
  "libogc v3 rebuild, 2026-10-23".
- Existing SMB networking does not prove TLS.
- RetroArch Wii transport is reference code only. Build configuration alone does not prove
  that an endpoint works.

## TLS entropy

The port's entropy is fake. Certificate checks that pass do not prove secret session keys.

- The [port patch](https://raw.githubusercontent.com/AndrewPiroli/wii-curl/master/wii-mbedtls/wii-mbedtls.patch)
  seeds SHA1 from `gettime`, the device ID and the MAC, then mixes timer samples. Its
  [README](https://github.com/AndrewPiroli/wii-curl) calls this fake entropy. The
  `entropy_poll.o` dependencies and disassembly in `wii/.deps/network/lib/libmbedcrypto.a` of
  the NetSurf checkout agree. A strong label on the callback does not make it unpredictable.

### Is there a hardware source?

- The pinned libogc2 headers and `libogc.a` have no entropy service. The IOS API gives IPC
  open, read, write and ioctl. It does not expose Starlet kernel syscalls. The ES code has no
  random command. This is evidence about the API, not proof that Hollywood has no random
  source. Sources: [libogc2 ES source](https://raw.githubusercontent.com/extremscorner/libogc2/master/libogc/es.c),
  [local pinned setup](../scripts/setup-devkitpro-local.sh).
- [IOS syscall research](https://wiibrew.org/wiki/IOS/Syscalls) names an internal
  `IOSC_GenerateRand`. We found no supported PPC IPC path to it and no quality assessment. An
  ARM syscall number cannot go through `IOS_Ioctl`. Custom IOS, exploits or key extraction
  would be a separate project. Stock IOS SSL is a different transport, not an entropy callback
  for Mbed TLS.

### The NV seed option

1. Build Mbed TLS 3.6.4 from source with `MBEDTLS_ENTROPY_NV_SEED`,
   `MBEDTLS_PLATFORM_NV_SEED_ALT`, `MBEDTLS_ENTROPY_C` and `MBEDTLS_PLATFORM_C`.
2. Turn off the fake platform source with `MBEDTLS_NO_PLATFORM_ENTROPY`. Keep default-source
   registration on.
3. Give exact read and write callbacks through `mbedtls_platform_set_nv_seed`.
4. Make at least `MBEDTLS_ENTROPY_BLOCK_SIZE` secret bytes (64 with SHA512) on a trusted OS
   CSPRNG. Each Wii needs its own seed.
5. Build Mbed TLS and curl with the same configuration headers. NV seed changes the entropy
   context layout. A macro in WiiStation alone is not enough.

Sources: [NV porting guide](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how_to_integrate_nv_seed/),
[3.6.4 entropy source](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/entropy.c).

- The first extraction from each entropy context updates the stored seed before it releases
  entropy.
- The [default file writer](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/platform.c)
  checks the `fwrite` length but ignores `fclose` errors. It has no durable transaction.
- Custom callbacks must check exact lengths, flush and close, and return a negative error for
  any incomplete commit. Mbed TLS checks only for a negative result.
- After a failed seed update, destroy that context. Do not retry it. 3.6.4 sets its
  initial-run flag before it tries the update.
- Serialize owners. Two contexts must not read the same old seed.
- NV-only entropy is weaker against compromise and cloning. An SD backup, a rolled-back seed
  or a cloned state can repeat generator states. Reseeding alone does not fix it. Keep the
  seed and TLS/DRBG state out of save states, frozen bundles, collected logs and replay
  backups. A known seed needs new secret entropy. Hashed public device IDs cannot restore
  secrecy. Source: [cloning analysis](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/random_generator_cloning/).

### Seed provisioning

- The HBC client uses plain TCP with compression and CRC. A seed sent that way is visible on
  the LAN. Source: [HBC file transport](../../hbc-reborn/tools/hbc.py).
- An HTTPS download made with the weak generator cannot bootstrap secrecy.
- Provision the seed physically on a trusted computer, or through a separately reviewed
  confidential channel. Plain SD storage assumes the card stays private. A key on the same card
  adds nothing.
- No tool today does confidential bootstrap automatically.

| Option | Use now? |
|---|---|
| Current fake source | Public feasibility probes only. Never credentials. |
| NV seed with custom storage | Host and Wii dummy storage and source-built public HTTPS pass. Secure bootstrap, verified RTC setup and durability still needed. |
| Fresh one-use OS randomness file | A controlled test after confidential provisioning. Fail closed when used up. Never reuse a frozen file. |
| Native IOS or hardware source | No verified callable source or quality assessment |

## UTC clock

- A hardware run reported `time(NULL)=1791682853` at about 2026-10-11 08:40 UTC: about seven
  hours behind, which agrees with a Pacific local clock. That does not prove the cause.
- Mbed TLS compares certificate dates with its time callback. Good entropy does not fix a wrong
  epoch. Sources: [x509_crt.c](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/x509_crt.c),
  [x509.c](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/x509.c).
- `MBEDTLS_PLATFORM_TIME_ALT` and `mbedtls_platform_set_time` can give TLS a reviewed UTC clock
  without a change to the console RTC. Source:
  [platform_time.h](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/include/mbedtls/platform_time.h).
- A blanket "+7 hours" is wrong. Unauthenticated NTP, an HTTP Date header or the HBC host time
  must not become the security authority on a hostile network.
- Reject authenticated connections when UTC is unknown or not plausible. Keep date checks on.
  Test certificates just before and after their validity limits, and timezone and DST changes.

### The test clock (`tests/achievement_clock.c`)

- It takes a quoted, signed minute correction and adds it to the Wii RTC epoch. Examples:
  Pacific daylight time `420`, standard time `480`, a verified UTC clock `0`. It does not
  detect timezones. Check the console time and change the correction when the timezone or DST
  changes. A correction cannot fix RTC drift.
- The SDK has wall-clock `gettime()` and monotonic `__SYS_GetSystemTime()`. The clock reads the
  corrected RTC at the start of a TLS session and advances it with the monotonic counter. A
  wall-clock or DST change thus does not move a running session. Source:
  [SDK time source](https://raw.githubusercontent.com/extremscorner/libogc2/master/libogc/timesupp.c).
- It never uses `settime`, an RTC write, NTP or an HTTP Date header.
- Mbed TLS is built with `MBEDTLS_PLATFORM_TIME_ALT` and `MBEDTLS_PLATFORM_GMTIME_R_ALT`.
  Unknown, malformed, implausible, overflowing or regressing time fails closed: the GMTIME
  callback returns NULL, so both certificate-date checks reject. A time of `-1` alone would give
  a 1969 date.
- The valid range is 2020 to 2099. The TLS adapter also checks that the target time type can
  hold the result.
- On the Wii, `MBEDTLS_PLATFORM_MS_TIME_ALT` uses `__SYS_GetSystemTime()` for milliseconds.

## DuckStation

DuckStation is a behaviour reference only. Its achievements and HTTP source files and its
[license](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/LICENSE)
are `CC-BY-NC-ND-4.0`
([header](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1-L4)).
Do not copy or adapt its source. Write WiiStation's code against upstream rcheevos. This is not
a legal opinion on every dependency or old revision.

| Area | DuckStation behaviour | Source |
|---|---|---|
| Client creation | `CreateClient` gives memory and server callbacks to `rc_client_create` and sets the event handler. It adds rcheevos's user-agent clause and starts with Hardcore off. It applies unofficial, spectator and encore settings and allows background memory reads. | [lines 579–628](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L579-L628) |
| Frames | `System::FrameDone` polls HTTP and calls `Achievements::FrameUpdate` once per emulated frame (not in GPU dump replay). The wrapper calls `rc_client_do_frame` and updates Rich Presence. Pause polls HTTP and calls `rc_client_idle`. | [frame end](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L2135-L2153), [wrapper](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L942-L960), [core idle](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/core.cpp#L810-L820) |
| Memory | 2 MiB RAM then 1 KiB scratchpad. `ClientReadMemory` returns raw bytes with an overflow-safe range check. No BIOS or VRAM. It picks one buffer from the start address, so it does not handle a read across the boundary. | [constants](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L101-L104), [callback](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L839-L864) |
| Locks | Other threads need the achievements mutex. Frame evaluation and server callbacks take it. A wait for owner-tagged HTTP requests releases it, which prevents a deadlock. | [lock contract](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.h#L78-L83), [callbacks and wait](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L866-L960) |
| Identity | Keeps the game-database identity apart from the RA hash. Opens track 1 with `IsoReader`, finds the executable through `SYSTEM.CNF` or `PSX.EXE`, removes the `cdrom:` prefix, leading separators and the version suffix. Hashes the name then the content, capped at the smaller of the PS-EXE payload plus the 2048-byte header and the file size. Checks the PS-EXE header first (its own detail). | [identities](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L674-L737), [boot name](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L753-L887), [MD5](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L902-L925) |
| Disc change | `SetGameHash` ignores the same hash, stores identity while inactive, aborts an old request, then calls `rc_client_begin_change_media_from_hash`. A later reset reloads the game. Start uses `rc_client_begin_load_game`. An associated swap does not rewrite the hash database. Unknown media turns Hardcore off. | [media lifecycle](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1160-L1330), [hash notification](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L4240-L4245) |
| HTTP | GET or POST with its user agent, requests tagged with an owner pointer, 60-second timeout. URL and POST text go into downloader-owned strings. Cancelled requests become non-retryable errors, other failures retryable. WinHTTP on Windows, libcurl elsewhere. Ten concurrent requests, 30-second downloader timeout, 60-second achievements override. | [adapter](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L866-L939), [request creation](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/util/http_downloader.cpp#L219-L235), [polling](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/util/http_downloader.cpp#L279-L422) |
| Login | Password login returns a token. The token is stored encrypted and used for later logins. A decryption failure asks for a login. AES-CBC key from the user name plus a machine ID (not in portable mode), with SHA256 rounds. Not an OS credential store. | [token login](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L697-L725), [password callback](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L2117-L2165), [key](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L2388-L2536) |
| Pending unlocks | Disconnected and reconnected events show retries. The count comes from `rc_client_get_pending_unlock_count`. This is live-session retry, not a restart-safe queue. | [events](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1855-L1873), [count](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L2320-L2327) |
| States | Uses `rc_client_progress_size` and the sized APIs in an optional length-prefixed section. Load waits for login and activation. Missing data resets the runtime. A failed deserialize logs the reset. | [state code](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1935-L2046) |
| Hardcore | Starts at boot or reset, never midway. State load, undo load and frame step ask to turn Hardcore off first. Settings: no rewind or debugging, automatic region, normal video timing, no speed below 1x, no CPU underclock below 100%. Not all overclocking or enhancements are banned. | [start/reset](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1078-L1157), [load guard](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L2927-L2944), [frame step](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L3864-L3877), [undo](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L5428-L5442), [settings](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/settings.cpp#L1258-L1280), [policy](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1895-L1932) |
| Reset event | Schedules a reset for RAIntegration only. Otherwise it ignores `RC_CLIENT_EVENT_RESET` and relies on its boot/reset path. WiiStation must handle the reset event. | [reset event](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L1457-L1472) |

How WiiStation adapts each pattern:

| DuckStation pattern | WiiStation adaptation |
|---|---|
| Separate `FrameUpdate` and `IdleUpdate` | One shared emulated-frame hook and a menu/pause idle hook. No display-frame dependency. |
| Memory callback returns RAM/scratchpad bytes | Bounds-checked `psxM`/`psxH` byte callback, verified on big-endian hardware |
| Achievement mutex for calls and completions | One owner thread and a response queue. No desktop lock design. |
| Requests own their strings, tagged by session | Copy request fields, tag the session generation, cancel or finish stale requests safely |
| Network waits release the mutex | Never block emulation or wait for a callback while holding its lock. Bounded teardown. |
| Overlay with its own lock and events | Queue small UI events. Draw outside evaluation and restore GX state. |
| Badge cache and progress databases | Bounded SD/USB assets. Text first. No SQLite at the start. |
| Progress in emulator states | Versioned optional `WSXE` section with identity checks |
| Background memory reads on | Off. Completions run on the emulator thread. |
| Token encryption from machine identity | Not usable as an SD-card security design |

- Its downloader is not a Wii network solution. Its concurrency and timeouts are not Wii
  targets. Measure a much smaller concurrency budget against SMB traffic and TLS memory on
  hardware.
- Optional DuckStation features that are not first-milestone scope: hash and progress
  databases, badge prefetch, unofficial unlock tracking, Discord presence, Windows
  RAIntegration, desktop UI threading, SQL caches, graphics overlays and a large UI model.
- This research shows behaviour at one revision. It does not prove Wii performance, current
  approval, hash coverage of every format or restart-safe offline submission.

## Other systems

Checked 2026-10-10 against primary documentation and source. The survey covers the main
families. It does not list every private or obscure product. A backend that stores an unlock
is different from an evaluator that knows which PS1 RAM condition earns it.

### RetroAchievements ecosystem and local options

| Option | What it gives | Decision for WiiStation |
|---|---|---|
| RA + `rcheevos`/`rc_client` | Evaluator for achievements and leaderboards, hashing, client and session logic, PS1 community sets, accounts, Rich Presence | Chosen. Needs hashing, memory callbacks, transport and UI. Wii build, TLS, endianness and frame budget need checks. |
| RAIntegration / RAEmus | Windows DLL with authoring, debugging and runtime integration ([authoring setup](https://docs.retroachievements.org/developer-docs/emulator-setup-for-developers.html)) | Reference and tooling on a PC. The DLL cannot load on the Wii. |
| RetroArch/libretro | Frontend with RA. Cores expose memory. | Reference. A libretro conversion is a much larger project and does not add achievements to WiiStation's UI. |
| RAWeb self-hosting | RA's Laravel/React application, GPL-3.0 | Defer. Adds server and database work. Gives no right to mirror the live site's data. Does not remove emulator integration or client registration. |
| Wii RA ESP32 adapter | Hardware offload of the runtime and HTTPS | Optional fallback experiment, see below |
| Companion PC or server transport | HTTPS and maybe authentication off the Wii | Only if native HTTPS fails its gates. Adds a machine, setup, trust and a protocol. |
| Local sets with the rcheevos runtime | Local RAM conditions, unlocks on SD/USB | Built (the current engine). Sets must be written and tested for each game and region. Keep local unlocks apart from official ones. |
| Save-file milestones | Decode known card fields | Optional separate feature. Cannot rebuild one-time, no-damage or time-limit events, and cannot prove past official unlocks. |

These are often layers, not rival providers. rcheevos, RAIntegration, RetroArch and the ESP32
adapter all belong to the RA ecosystem.

- **RetroArch on the Wii.** Do not trust old reports that it "cannot do RA". The current
  [Makefile.wii](https://github.com/libretro/RetroArch/blob/master/Makefile.wii) sets
  `HAVE_NETWORKING = 1` and `HAVE_CHEEVOS := 1`, adds `-DHAVE_SOCKET_LEGACY -DHAVE_CHEEVOS` and
  includes rcheevos headers. This shows build intent, not a working HTTPS login or approval.
  Also check the [Makefile.griffin](https://github.com/libretro/RetroArch/blob/master/Makefile.griffin)
  `platform=wii` path. See the [Wii compilation guide](https://docs.libretro.com/development/retroarch/compilation/wii/).
- **ESP32 adapter.** An ESP32-S3 in GameCube memory-card Slot B runs `rc_client`, networking and
  evaluation. Forked loader and kernel parts send watched RAM over EXI each frame. The author
  announced a public release in July 2026, Casual only, needing much testing. It supports
  native Wii/GameCube games, not WiiStation PS1 games. WiiStation would need a new PS1 memory
  provider, hash path, synchronization and protocol, plus the hardware. Sources:
  [README](https://github.com/odelot/wii-ra-adapter/blob/main/README.md),
  [announcement](https://retroachievements.org/forums/topic/36728?page=1).
- RA support for Wii games in Dolphin is a different question.

### Other achievement backends

These expect a developer-owned product with its own definitions and unlock calls. None gives a
PS1 RAM-condition catalog.

| System | Needs | Decision |
|---|---|---|
| Steamworks | App achievements, statistics and unlock APIs, Steam product ([achievements](https://partner.steamgames.com/doc/features/achievements), [SDK](https://partner.steamgames.com/doc/sdk), [Web API](https://partner.steamgames.com/doc/webapi_overview)) | Exclude. No Wii SDK path. Cannot award another publisher's PS1 rerelease achievements. |
| GOG GALAXY | Developer Portal, definitions, SDK `IStats` ([SDK](https://docs.gog.com/sdk/), [achievements](https://docs.gog.com/sdk-stats-and-achievements/), [portal](https://docs.gog.com/developer-portal/)) | Exclude. Desktop only. The SDK caches offline unlocks, but definitions and ownership are product-specific. |
| Epic Online Services | Product setup and EOS client ([achievements](https://dev.epicgames.com/docs/epic-online-services/player-and-game-data/achievements-interface), [platforms](https://dev.epicgames.com/docs/epic-online-services/eos-get-started/eos-get-started-reference)) | Exclude. No Wii in the SDK list. |
| Google Play Games | Play Console definitions, client unlock calls ([integration](https://developer.android.com/games/pgs/integrate-achievements)) | Exclude. Android only. |
| Apple Game Center | GameKit, capability, signed-in player ([GameKit](https://developer.apple.com/documentation/gamekit)) | Exclude. Apple devices only. |
| Xbox | Xbox services/GDK, Partner Center definitions ([implementation](https://learn.microsoft.com/en-us/gaming/gdk/docs/gdk-dev/pc-dev/tutorials/pc-e2e-guide/e2e-services/e2e-achievements), [GDK](https://github.com/microsoft/GDK)) | Exclude. Windows/Xbox only. |
| PlayStation trophies | Approved developer and product, SDK after agreement ([partner process](https://sonyinteractive.com/en/news/blog/showing-your-game-to-playstation/)) | Exclude. Not a public API. |
| Nintendo developer services | Registration for current platforms ([portal](https://developer.nintendo.com/register)) | Exclude. No public Wii achievement service. The portal says new Wii U/3DS titles and patches cannot be released. |
| Game Jolt trophies | Game entry, trophy definitions, signed HTTP, player token ([Game API](https://ssr.gamejolt.net/help-docs/creators/game-api), [requests](https://ssr.gamejolt.net/game-api/doc/construction)) | Exclude from the first integration. Adaptable, but no PS1 catalog. |
| Newgrounds medals | Medal definitions, signed-in session, `Medal.unlock` ([components](https://www.newgrounds.io/help/components/)) | Exclude from the first integration |
| PlayFab / Nakama-Hiro / custom server | Player data and custom achievements ([PlayFab](https://learn.microsoft.com/en-us/xbox/playfab/player-progression/player-data/), [Hiro](https://heroiclabs.com/docs/hiro/server-framework/achievements/), [Nakama](https://heroiclabs.com/docs/index.html)) | Defer. Possible over a Wii transport, but much more work and no PS1 catalog. |

- Check redistribution terms before shipping anything from a commercial SDK or self-hosted
  data. Source access does not authorize writing another publisher's achievement namespace.

### Trackers

| Tool | What it is | Decision |
|---|---|---|
| Exophase | Imports RA activity. Its May 2026 [announcement](https://forums.exophase.com/threads/retroachievements-tracking-updates-for-may-2026-hardcore-leaderboard-and-beaten-status.73869/) describes Hardcore and Casual unlocks and beaten status. | Optional viewing after WiiStation submits to RA. Not an evaluator. |
| Achievement Watcher | PC achievement-file parser and notifier, Windows/Node/Electron ([upstream](https://github.com/xan105/Achievement-Watcher)). Its "Steam emulator" support is about PC files. [Achievement Watcher Next](https://github.com/Shirowwww/Achievement-Watcher-Next) is a Windows tracker too. | Exclude from the core. A Wii export file could feed a companion display. |

There is no reason to integrate several commercial backends first. Build RA Casual,
performance and networking first. Then decide if a local mode or a companion solves a real
need.

## Open research questions

1. Pick a tagged rcheevos release. Confirm its C toolchain, memory budget and threading setup
   under devkitPPC and libogc.
2. Check hashes against a known supported frontend for BIN/CUE, cooked ISO, CHD and selected
   PBP discs, with multi-track, pregap and truncated cases.
3. Get a supported durable offline-queue API and the storage protection rules from upstream.
4. Hardcore approval, option policy and the list of supported hashes are outside decisions.
   They are not guaranteed.
5. The ESP32 and companion routes are alternatives to measure. They are not prerequisites and
   they do not work with WiiStation today.
