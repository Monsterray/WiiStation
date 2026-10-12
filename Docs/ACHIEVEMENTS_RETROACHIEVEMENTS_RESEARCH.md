# RetroAchievements integration research for WiiStation

Researched 2026-10-10. This is an implementation-planning note, not an implemented integration or a claim of RetroAchievements approval. Primary sources: official RetroAchievements documentation and rcheevos source, pinned for this investigation to commit [`683197d8dc92555a51e8280a4123d071ed66bff3`](https://github.com/RetroAchievements/rcheevos/tree/683197d8dc92555a51e8280a4123d071ed66bff3). Downloaded material was source text only; no external binaries were executed.

## Conclusion and deployment boundary

The official C runtime fits WiiStation's native integration model, but it supplies neither a Wii UI nor an HTTP/TLS transport. The first deliverable should be an opt-in Casual integration, with local status clearly distinguishing active achievements, pending network submissions, and confirmed server unlocks. Keep Hardcore unavailable until all restricted paths are covered and RetroAchievements validates WiiStation's own client identity. That staging is a recommendation, not a requirement imposed by rcheevos. The [official integration guide](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration) explicitly starts development with Hardcore disabled and explains client validation.

The ordinary [public web API](https://api-docs.retroachievements.org/) is for querying site information. Runtime evaluation and earned unlocks belong through `rc_client`/the emulator integration protocol; a public web API key is not the login token used by this runtime.

## Client lifecycle and API boundary

The pinned [rc_client.h](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_client.h) provides:

| Lifecycle need | API |
|---|---|
| Create/read memory/send requests | `rc_client_create(read_memory, server_call)` |
| Receive gameplay/status notifications | `rc_client_set_event_handler` |
| Password or remembered-token login | `rc_client_begin_login_with_password`, `rc_client_begin_login_with_token` |
| Supply known RA content hash | `rc_client_begin_load_game` |
| Identify content using hash callbacks | `rc_client_begin_identify_and_load_game` when built with `RC_CLIENT_SUPPORTS_HASH` |
| Process each emulated frame / menus | `rc_client_do_frame` / `rc_client_idle` |
| Inform runtime after emulator reset | `rc_client_reset` |
| Preserve Casual trigger progress | `rc_client_progress_size`, sized serialize/deserialize APIs |
| Unload / shut down | `rc_client_unload_game`, `rc_client_destroy` |

These calls return or expose asynchronous work/status; preserve state until completion and handle cancellation. **Use the pinned header, not copied wiki signatures.** For example, current `rc_client_begin_change_media(client, hash, callback, userdata)` accepts a hash. File identification uses `rc_client_begin_identify_and_change_media`; some wiki examples predate that split. This observation is an API-version warning derived by comparing those primary sources.

## PS1 identity: content hashing, not save names

The [PS1 hash implementation](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_hash_disc.c#L930) opens track 1, finds the boot executable through `SYSTEM.CNF`'s `BOOT` entry, and falls back to `PSX.EXE`. Its MD5 input includes the executable filename plus executable content. With a `PS-X EXE` header, byte offset 28 supplies a little-endian payload size, and 2048 bytes are added for the header. A missing primary executable fails identification.

Consequently a whole-file MD5, disc filename, game serial, or WiiStation `CdromId` is not an interchangeable RA hash. Regions, revisions, patches, and related engines must be identified through the upstream algorithm. Prefer calling it rather than independently reproducing its filesystem/parser behavior. Identification must use the currently mounted game's actual sectors; do not infer identity from a memory-card filename or the names in the save inventory.

## BIN/CUE, ISO, CHD, PBP, and sector adapters

The pinned [default CD reader](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_cdreader.c) dispatches CUE and GDI to their parsers and other paths to its binary-track reader. It detects sector layout and strips sector headers when supplying user data. Thus ordinary BIN/CUE and compatible raw/cooked ISO images are plausible without a separate container decoder; validate each supported sector layout against upstream hashes before announcing support. The default reader does not itself decompress CHD or PlayStation PBP.

The [hash dispatcher](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_hash.c) recognizes CHD as an identification candidate, which does not supply CHD decoding. Generic `.pbp` inference chooses PSP. WiiStation must explicitly select `RC_CONSOLE_PLAYSTATION`, expose the selected PBP disc's decompressed sectors, and use a custom reader for those formats. Do not hash compressed container bytes or always select PBP disc zero.

The [rc_hash.h callback contract](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_hash.h) requires track open/close, absolute-sector reads, and first-track sector reporting. Reads return their actual byte count. File callbacks use 64-bit offsets and standard seek origins. Prefer per-iterator callbacks or `rc_client_set_hash_callbacks`; global registration APIs are deprecated. A WiiStation adapter must return logical sector user data, honor requested byte spans and track origins/pregaps, and use an independent handle or preserved stream position so hashing cannot move the live emulation reader. Those adapter constraints are inferred engineering requirements from the contract.

## PS1 memory map and big-endian Wii host

The authoritative [rcheevos PS1 memory-region table](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/runtime/rc_consoleinfo.c#L803) is:

| RA logical addresses, inclusive | PS1 real addresses | WiiStation source |
|---|---|---|
| `0x000000–0x00FFFF` | `0x00000000–0x0000FFFF` | first 64 KiB of `psxM` (kernel RAM) |
| `0x010000–0x1FFFFF` | `0x00010000–0x001FFFFF` | remainder of 2 MiB `psxM` |
| `0x200000–0x2003FF` | `0x1F800000–0x1F8003FF` | 1 KiB scratchpad at start of `psxH` |

There is no BIOS, VRAM, or hardware-register region in that published table. Do not expose guessed CPU virtual-address mirrors. The proposed `psxM`/`psxH` bindings are WiiStation-specific inferences requiring tests for every enabled CPU core. Return raw emulated bytes without `SWAP16`/`SWAP32`; a byte-buffer callback should not convert PS1 little-endian memory to PowerPC integer order. Clip reads at valid bounds, return actual counts, and return zero when memory is unavailable.

## Frame semantics and concurrency

The [official integration guide](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration) requires processing every emulated frame, including frames skipped for fast-forward or performance. During pause, call `rc_client_idle` instead; it services periodic communication. A host-present callback alone therefore cannot be the evaluator hook.

For this checkout, use one consistent PS1 frame boundary shared by GPU/CPU backends, such as the existing `VBlankStart`/`HSyncTotal` flow in `psxcounters.c`; choose exactly one after tracing reset and backend behavior. Keep reads and evaluation on the emulator owner thread. Set `rc_client_set_allow_background_memory_reads(client, 0)` before asynchronous game loading; current [client activation code](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c#L1783) explicitly accounts for address validation reading memory during load and can defer activation to frame/idle processing. Queue response completion/UI events to the owner thread as the simplest WiiStation threading design. No benchmark establishes cost on Broadway yet; measure per-frame evaluation, memory allocations, hashing, image decode, and network queues separately.

## HTTP/TLS, asynchronous lifetime, and authentication

The guide's [transport/login example](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration) uses GET when no POST body exists, otherwise POST, and passes status/body to the provided callback on every outcome. It stores the returned username/token for later logins rather than the password. WiiStation must provide the actual HTTP/HTTPS stack.

The [request contract](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_api_request.h) exposes URL, POST data/content type, response body length/status, and distinct retryable/non-retryable client errors. The [client request-dispatch implementation](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c#L4791) destroys temporary request storage immediately after `server_call` returns. An asynchronous transport must therefore copy URL/body/header strings before returning, preserve callback data until completion, and never keep those temporary pointers.

Recommended transport acceptance gate: real-device HTTPS with hostname/certificate verification, usable clock/certificate roots, bounded allocations/timeouts, and response limits; no credential-bearing plain-HTTP fallback. Existing SMB initialization is not evidence that those capabilities exist. Remembered tokens need explicit opt-in, a delete/logout path, and redaction from logs/reports. SD storage is removable: do not call a token file an encrypted credential store without an actual key-management design. None of those Wii TLS/storage capabilities was verified in this research.

## Hardcore: policy, approval, and settings

Current [compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html) require correct achievements/indicators, Rich Presence/leaderboards, an accessible achievement list, secure offline queueing, a unique incrementing client user agent, and approval eligibility. They prohibit state loading, rewind, slowdown/frame advance, gameplay cheats, debugging/memory editing, and TAS/input playback in Hardcore. Resume must drop to Casual; entering Hardcore requires a full reset. Saving states remains allowed. A Hardcore-default login is recommended, not compulsory; the older wiki wording is stricter. The public-release eligibility period is six months, including a qualifying parent emulator. Licensing/privacy obligations also apply. Do not impersonate RetroArch or PCSX for approval.

The [FAQ](https://docs.retroachievements.org/general/faq.html) permits fast-forward. That does not imply every speed hack is acceptable: rcheevos' own [libretro option restrictions](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/libretro/rc_libretro.c) disallow certain PCSX-ReARMed clock/forced-PAL settings and SwanStation underclock settings. These are core-specific rules, not a ready-made WiiStation whitelist. Submit CPU-clock, region-forcing, cycle hacks, GPU timing fixes, and patch/cheat classifications for explicit review. Conservatively disable unreviewed gameplay-changing settings in Hardcore. Protect the underlying action functions, including automatic/forced-state paths, rather than only disabling menu buttons. Honor `RC_CLIENT_EVENT_RESET` by resetting the game, then notifying the client.

## Save states, existing memory cards, and historical progress

For Casual states, add bounded/versioned achievement-progress metadata to this checkout's existing optional `WSXE` extension, using upstream sized serialization and the game's RA identity. Do not replace the emulator's core-state layout. Restoring old states with no compatible achievement metadata must reset runtime progress rather than inherit the future timeline's trigger hit counts. This is a design recommendation from the serializer/reset API. It does not serialize confirmed website unlocks or pending network requests.

The [player rules](https://docs.retroachievements.org/guidelines/users/global-leaderboard-and-achievement-hunting-rules.html) prohibit inappropriate imported saves and unapproved Hardcore clients. The user's recovered historical memory cards do not constitute retroactive achievement evidence. Historical progress, state-derived progress, other people's saves, and achievement eligibility are separate matters; clarify the acceptable route with RetroAchievements before promising Hardcore credit from migrated cards.

## Offline sessions and durable synchronization

The [official FAQ](https://docs.retroachievements.org/general/faq.html#can-i-earn-achievements-offline) says full offline startup is unsupported: begin online, then keep the emulator open through an outage until it reconnects. The pinned client stores retry callbacks in memory, retries transient award/submission failures with exponential delays capped at two minutes, and raises disconnected/reconnected events. Its progress serializer calls the runtime serializer; it is **not** a durable network-outbox export. Source: [client retry/serialization implementation](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c).

The compliance document's durable secure-queue expectation is broader than the standalone public client API inspected here. Treat restart-safe queueing as unresolved upstream work/approval discussion, not as automatically solved by saving a progress blob. Never fabricate successful server replies to emulate offline login/session setup. Distinguish locally triggered, awaiting submission, and server-confirmed states; surface terminal server errors. Reconnect with the original session/account/hash/mode and truthful unlock timing. A fully offline local-only feature must remain clearly separate from promised synchronized RA credit.

## Multi-disc

Use the current client media-change APIs after hashing the newly mounted disc. Preserve the session/runtime for a recognized disc in the same game; handle unrecognized or unrelated media explicitly before continuing evaluations. Validate BIOS tray timing, hashing cancellation, and PBP internal disc selection. The official [multi-disc tutorial](https://docs.retroachievements.org/general/tutorials/multi-disc-games.html) documents playlists/tray operations for particular frontends; its `.m3u` workflow is not a requirement that WiiStation discard its existing swap UI.

## Remaining proof before implementation commitments

1. Pick a tagged rcheevos release and confirm its supported C toolchain, memory budget, and threading configuration under devkitPPC/libogc; this note pins current source for reproducibility, not a dependency recommendation.
2. Verify PS1 RAM/scratchpad reads with boundary, byte-order, and core-switch fixtures; verify one evaluation per emulated frame under skip, fast-forward, pause, reset, and region changes.
3. Compare hashes with a known supported frontend for equivalent BIN/CUE, cooked ISO, CHD, and selected PBP discs; include multi-track/pregap and truncated-input cases.
4. Prove device HTTPS, request-copy/lifetime behavior, token redaction, bounded responses, cancellation, and outage/reconnect semantics before account-linked testing.
5. Demonstrate correct Casual achievement progression/state restore and visibly limited unsupported logic. Resolve secure durable queueing and review every Hardcore-restricted entry point with RetroAchievements before enabling competitive credit.
