# Achievements for WiiStation

Research and proposed implementation plan — 2026-10-10. This is a design document; no emulator implementation, account registration, hardware purchase, or achievement submission was performed.

## Recommendation

Add **RetroAchievements through the upstream C library `rcheevos`**, integrated into WiiStation itself. Start with a disabled-by-default, softcore implementation. Establish correct identification, memory reads, frame evaluation, networking, and progress restoration before pursuing hardcore approval. Keep a local-only achievement mode as an optional subsequent feature, not a second online service to build first.

Native integration is the first choice because it can cover Wii and Wii U/vWii without extra hardware. Treat verified HTTPS on real Wii hardware as an early go/no-go experiment. If native networking proves too costly, compare a narrowly scoped companion transport and the existing ESP32 adapter architecture before choosing a fallback. Do not silently switch to unencrypted credential transport or invent a new achievement backend.

RetroAchievements' C client supplies achievement evaluation and service integration, not the platform's HTTP implementation or UI. The service's [integration instructions](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration) and [current compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html) are authoritative. Verify API signatures against the pinned headers rather than copying old wiki examples.

## Systems considered

The survey covers the relevant emulator ecosystem, major game-platform achievement services, aggregators, and local systems. It is not a claim that every private or obscure achievement product has been enumerated. Detailed evidence is in [the systems research](ACHIEVEMENTS_SYSTEMS_RESEARCH.md).

| System / approach | What it supplies | Fit for WiiStation | Decision |
|---|---|---|---|
| RetroAchievements + rcheevos | PS1 community sets, memory-condition evaluator, accounts, unlocks, leaderboards, rich presence | Direct fit; requires hashing, memory callbacks, transport and UI | Recommended |
| RAIntegration / RAEmus | RetroAchievements integration and authoring tooling, primarily the Windows emulator ecosystem | Not a Wii SDK replacement | Reference/tooling only |
| RetroArch/libretro achievements | An existing RetroAchievements implementation | Useful reference; converting WiiStation into a libretro core is a much larger project | Reuse lessons, not a frontend rewrite |
| Existing Wii RA ESP32 adapter | Hardware offload for RetroAchievements runtime and HTTPS | Early Wii/GC-game project; PS1/WiiStation needs a new integration and correct emulated-memory protocol | Optional fallback experiment |
| Companion PC/server transport | HTTPS and possibly authentication outside the Wii | Extra machine, configuration, trust and runtime dependencies | Only if native HTTPS fails its gates |
| Local custom achievements | Local progress with no service account | Technically feasible using rcheevos runtime and explicitly local definitions; sets must be authored/maintained | Optional later phase |
| Steamworks achievements | Steam account/game achievement backend | Designed for registered Steam applications, not existing PS1 sets or Wii homebrew | Exclude |
| Epic Online Services achievements | Developer-defined achievement backend and related stats | Requires its platform/service integration and custom achievement definitions | Exclude |
| GOG Galaxy achievements | GOG application achievement backend | No generic PS1 sets; no Wii integration route established | Exclude |
| Google Play Games / Apple Game Center | Mobile/Apple application achievements | Platform-bound; not Wii-compatible backends | Exclude |
| Game Jolt trophies / Newgrounds medals | Developer-defined achievements over game APIs | Custom sets, product setup and trigger authoring; no PS1 condition catalog identified | Exclude from first integration |
| PlayFab / Nakama-Hiro / custom backend | Player progress storage and developer-defined achievement services | Technically adaptable, but requires its own sets, account model and backend operations | Defer |
| Xbox, PlayStation and Nintendo platform achievements | Approved-platform game services | No public WiiStation/PS1-homebrew integration established; not drop-in providers | Exclude |
| Exophase / TrueAchievements-style trackers | Aggregation and presentation of achievements earned elsewhere | Not live PS1 memory evaluation or an award backend | Optional external viewing only |
| Achievement Watcher / Playnite achievement plugins | Desktop collection/notification tooling | Desktop consumers, not an on-Wii PS1 achievement system | Exclude from emulator core |
| Self-hosted RA-like service | Hosting under separate accounts/trust/model | Operational and ecosystem work; neither official RA progress nor a shortcut around client registration | Defer |
| Save-derived local milestones | Display accomplishments inferred from decoded memory-card fields | Cannot recover transient challenges or prove historical official unlocks | Optional separate feature |

These are often different layers rather than competing achievement providers. In particular, rcheevos, RAIntegration, RetroArch integration and the ESP32 adapter all belong to the RetroAchievements ecosystem.

## DuckStation implementation reference

DuckStation was inspected at commit [`cd024df237d46faa4c65574b101261b31267e2fd`](https://github.com/stenzek/duckstation/tree/cd024df237d46faa4c65574b101261b31267e2fd). The [detailed trace](ACHIEVEMENTS_DUCKSTATION_RESEARCH.md) covers actual source paths and lifecycle calls. Use DuckStation as a behavioral comparison for a PS1 emulator, while implementing against the separately maintained upstream rcheevos APIs.

Its current achievements and HTTP source files declare `CC-BY-NC-ND-4.0`, matching the repository [license](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/LICENSE). This plan does not propose copying or adapting those source files into WiiStation. Implement the needed behavior independently using rcheevos and WiiStation's existing code; do not treat current DuckStation source as a permissively licensed component.

Patterns worth applying:

| DuckStation pattern | WiiStation adaptation |
|---|---|
| Separate `FrameUpdate` and `IdleUpdate` use `rc_client_do_frame` and `rc_client_idle` | One shared emulated-frame hook plus a menu/pause idle hook; no display-frame dependency |
| Memory callback returns RAM/scratchpad bytes to rcheevos | Bounds-checked `psxM`/`psxH` byte callback, verified on big-endian hardware |
| Client calls and async completions synchronize through an achievement mutex | Prefer one client owner thread and a response queue; avoid inheriting desktop locking complexity |
| HTTP requests own URL/body strings and are tagged by achievement-session owner | Copy transient request fields, associate session generation, cancel/finish stale requests safely |
| Network waits release the client mutex | Never block emulation or wait for callbacks while holding their required lock; bounded teardown |
| Overlay state has separate synchronization and event-driven indicators | Queue small UI events; draw outside client evaluation with proper GX state restoration |
| Badge download/cache and progress databases are separate from emulation | Bounded SD/USB assets; text-first UI, no initial SQLite/database dependency |
| Progress serialization accompanies emulator states | Versioned optional `WSXE` section with identity checks and correct old-state handling |

DuckStation's current downloader uses WinHTTP on Windows and libcurl elsewhere; it is not a Wii networking solution. Its desktop defaults allow ten concurrent requests, with a 30-second downloader timeout and a 60-second achievements override. These are reference observations, not Wii targets: benchmark a much smaller concurrency budget against SMB traffic and TLS memory on hardware. Its token encryption depends on desktop machine identity and portable-mode behavior; do not transplant it as an SD-card security design.

DuckStation explicitly enables background memory reads. WiiStation should initially disable them and dispatch client completions on the emulator thread, keeping mutable PS1 memory under one owner. Also distinguish its separate achievement hash from the emulator's own game hash: boot-executable hashing and same-game media changes are the relevant behavior to reproduce. Use the pinned upstream API rather than assuming DuckStation's bundled client exposes identical functions.

Keep DuckStation's complete feature set as a validation checklist, not the first milestone's scope. Hash/progress databases, badge prefetch, unofficial unlock tracking, Discord presence, Windows RAIntegration, and desktop UI threading are optional surrounding features. None removes the need to validate WiiStation's own game hashes, frame timing, network transport, or approval status.

## Findings in this checkout

- `psxcounters.c` advances all CPU/GPU combinations through a shared emulated vertical-frame path. `VBlankStart` calls `gpuPtr->updateLace()` and `SysUpdate()`; the frame-end branch increments `frame_counter`. Evaluate once at one deliberately chosen emulated boundary, including skipped display frames. Never tie achievement evaluation to a Wii host-screen refresh or an FPS counter.
- `psxmem.c` provides the 2 MiB RAM buffer `psxM`; `psxH` contains hardware/scratchpad storage. The host is big-endian PowerPC while PS1 memory is little-endian byte data. An achievement memory callback must return the emulated bytes, not native-host `uint32_t` casts and not indiscriminate byte swapping.
- `Gamecube/GamecubeMain.cpp::loadISO` covers image load, identification, reset, settings and cards. `loadISOSwap` clears `CdromId` and reopens the disc. Both need explicit achievement lifecycle hooks; a stale serial or filename must not select a set.
- `misc.c` states already have a tagged `WSXE` extension with lengths and unknown-tag skipping. Add an optional versioned achievement-progress section here instead of rewriting the PCSX state layout. Bound its length and attach it to game hash, runtime version and definition identity.
- `Gamecube/fileBrowser/fileBrowser-SMB.c` owns network initialization in a worker; its connection state is private and oriented toward SMB. Generalize only network readiness/lifetime that achievements actually need. Do not make RA require configured SMB credentials, share a libsmb2 context with HTTP, or call competing network initializers during HBC-agent startup.
- `Gamecube/fileBrowser/smb2dev.c` already pumps asynchronous reads while games run. HTTPS requests will compete for IOS/network time: bench achievement traffic and SMB streaming together.
- Both Wii makefiles compile with explicit big-endian macros; neither linked-library list currently supplies an HTTP/TLS achievement stack. `mem2_manager.c` and the profiler give ways to measure allocation pressure and runtime cost.
- Menus live in `Gamecube/menu/`; graphics have multiple backends. A trophy overlay needs a release-build rendering path for each supported backend, with GPU/GX state restored afterward. A debug overlay alone is insufficient.

## Proposed design

### Game identity and PS1 memory

Use the canonical upstream PlayStation hash algorithm. A disc serial, the MD5 of the entire image, a card filename, and the user's earlier save inventory are not valid substitutes for the RA content hash. The PS1 hash uses the boot executable identity and contents discovered through `SYSTEM.CNF` with a fallback; follow upstream precisely.

Expose the [published PS1 logical map](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/runtime/rc_consoleinfo.c#L803): addresses `0x000000–0x1FFFFF` are the 2 MiB `psxM`; `0x200000–0x2003FF` are the 1 KiB scratchpad at real PS1 address `0x1F800000`, backed by the start of `psxH`. Do not add BIOS, VRAM, memory-card bytes or arbitrary hardware registers to that map. Bounds-check each request, including a range crossing the RAM/scratchpad boundary, and follow the callback's partial-read contract. Confirm equivalent bytes under Lightrec, PPC dynarec and interpreter.

Implement the rcheevos CD reader adapter against a dedicated read-only image handle, reusing image-format support where practical. Hashing must not move the live emulation CD cursor, close shared handles, or introduce concurrent access to non-thread-safe image/decompression state. Provide BIN/CUE, ISO, CHD and multi-disc PBP cases individually; reject unhandled formats explicitly. For PBP, select PlayStation and the current embedded disc explicitly rather than allowing a generic PSP inference. Cache a verified hash only against sufficiently strong file/container identity; changed content invalidates it.

On disc swap, identify the new media through current upstream APIs and let the client determine whether it belongs to the loaded game. Preserve progress for a same-game multi-disc transition; stop awarding for an unrelated or unrecognized disc until reidentification finishes. Unknown images produce a useful unsupported-version message, not a guessed achievement set.

### Runtime and asynchronous transport

Keep ownership of `rc_client` and its memory reads on the emulator thread. That thread evaluates once per emulated frame, services completed callbacks, and emits small UI events. A worker owns bounded HTTP requests, DNS/TLS, response download and timeout handling; it sends responses back to the client owner. Never read rapidly changing emulated RAM from the network worker. Use generation IDs/cancellation so a response for a previous game or login cannot mutate the new session.

Configure `rc_client_set_allow_background_memory_reads(client, 0)` before asynchronous loading; process `rc_client_idle` while paused/in menus. The HTTP worker must copy request URL/body/content-type before the server-call callback returns: upstream request storage is temporary. Return success or failure to the callback exactly once, and preserve callback lifetime through cancellation and shutdown.

Use the existing console networking APIs with a verified TLS implementation. Compare the current RetroArch Wii transport as reference code, then make a real-hardware prototype; build configuration alone does not establish secure endpoint compatibility. Test certificate validation, trust anchors, clock handling, redirects, modern server TLS requirements, timeouts and response limits. Credentials and tokens must never be sent over plaintext HTTP. Reuse a maintained TLS component where practical; avoid creating a TLS implementation or certificate-validation shortcut.

Do not build a new REST client for achievement awarding. Use rcheevos' Connect API flow. The public RA Web API is for data access and is not an equivalent trusted emulator award endpoint.

Username/password login can yield a token. If the user opts to remember login, persist only the minimum required credential/token data outside ordinary exported settings; no retained password, logs or crash dumps containing secrets. An SD card is readable storage: encryption with a key next to the file is not meaningful protection. Logout clears the stored session. Account switches separate caches, pending awards and achievement progress.

Use upstream retry/offline behavior first. Durable award queueing across application exits needs explicit investigation of the pinned client APIs and RA requirements; do not imply that in-memory retries survive power loss. Offline local unlocks, pending official submissions and server-confirmed unlocks must be visually distinct. Starting fully offline may differ from a network drop after an authenticated session: specify and test each case. Never manufacture official awards from arbitrary cached local records or old memory-card milestones.

The [current official FAQ](https://docs.retroachievements.org/general/faq.html#can-i-earn-achievements-offline) requires starting an official session online and keeping the emulator open through an outage until it reconnects. The inspected public `rc_client` progress serializer preserves runtime trigger state, not a durable award outbox. Support that documented session behavior first; restart-safe synchronization is an unresolved upstream/compliance gate, not a feature solved by writing a save-state blob.

### UI and storage

Add an Achievements settings panel and current-game list: enable/disable, login/logout, softcore status, game identification state, earned/locked achievements, measured progress and retry/error status. Support controller navigation and the existing keyboard facilities. Initial release can use text-only popups and downloaded icons later, but must support achievement/challenge/progress visibility where required.

Use a bounded notification queue and a capped icon cache. Graphics notifications must work with both GPU paths and survive resolution changes, interlace and 240p modes. Network/image failures must not pause gameplay. Keep set metadata, badges and local progress under `sd:/wiistation/achievements/` or the selected USB storage, separate from `.mcd` files. Evict download assets before sacrificing emulator RAM. Do not embed full disc images or memory-card contents in any service request.

Imported ordinary memory cards remain usable in softcore. Their contents do not establish achievement dates or prove a past unlock: sets evaluate through normal runtime conditions. Restoring a state restores appropriate runtime progress using rcheevos serialization; reject mismatched game/definitions, and follow upstream reset behavior for old states without progress data. Never revert already confirmed server unlocks when loading a state.

### Hardcore is a separate release gate

Start softcore, with honest labeling. Before hardcore, obtain recognition/approval for the WiiStation user agent/version and implement the current [hardcore compliance requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html). An accepted account login is not proof of emulator approval.

Audit all routes for loading savestates, cheats/memory editing, execution stepping/automation and gameplay-altering options. Include scripted statetool/autoboot paths, per-game configurations and forced mismatched-state loads. Enforce rules in the common operations, not just disabled menu buttons. Evaluate `fastLoad`, timing/game hacks, HLE BIOS and CPU options against actual service policy; do not blanket-ban ordinary presentation frameskip or permissible fast-forward without evidence. Starting hardcore after a disallowed softcore operation requires the proper reset/restart transition; disc swaps and resets need client notifications.

Hardcore also requires functioning leaderboards, rich presence, progress/challenge indicators and secure offline queueing under current rules. Do not ship a hardcore switch before those criteria and upstream validation are complete. Do not disguise WiiStation as an approved emulator.

Current eligibility includes six months of public emulator history or qualifying parent history; confirm how WiiStation qualifies with the service. Hardcore prohibits loading states, while saving them remains permissible. Keep ordinary memory-card behavior separate from savestate policy, and clarify service rules for migrated historical saves before promising competitive credit.

## Implementation sequence and acceptance gates

| Phase | Work | Exit evidence |
|---|---|---|
| 0. Feasibility and upstream alignment | Pin rcheevos; start client/user-agent approval discussion; compile C runtime for devkitPPC; prototype verified HTTPS; measure memory and transport contention | Cross-build succeeds; real Wii authenticates using verified TLS; memory/stack peaks and SMB contention are recorded; no promised hardcore certification |
| 1. Deterministic local core | Byte callback, shared frame hook, image adapter and hashes, lifecycle/reset/swap; fixture definitions without official submissions | Endianness/bounds checks pass; one evaluation per emulated frame including frameskip; same canonical hashes as a trusted reference across supported formats |
| 2. Softcore client | Login, async responses, set loading, events, game list, minimal popups, retry/error paths | A dedicated test account receives actual expected softcore awards; no false award on wrong-region/hash; errors leave games playable; no secrets in logs |
| 3. Usable beta | Progress indicators, cached badges, state extension, reconnect and durable offline policy, multi-disc, SD/USB behavior | State round-trip preserves hit/measured progress; old states handled; no stale-game callbacks; reconnect/no duplicate awards; power-loss and storage-removal behavior documented |
| 4. Hardcore and full service compliance | Core feature restrictions, leaderboards, rich presence, all required indicators, secure queueing, approved user-agent/version policy | Current upstream requirements satisfied, all bypass paths tested, upstream approval obtained; otherwise remain softcore |
| 5. Optional local achievements | Explicitly local profiles/sets via rcheevos runtime, separated namespace and progress | Local mode works without an account and cannot silently submit local accomplishments to official RA |

Do not add a multi-provider abstraction, authoring toolkit, new backend server, libretro core rewrite or mandatory hardware adapter during phases 0–3. If phase 0 shows native HTTPS is unsuitable, write a measured transport comparison and choose one fallback before expanding integration work.

## Validation matrix

1. **Correctness:** synthetic RAM and scratchpad reads, unaligned/boundary requests, invalid addresses, PS1 little-endian multi-byte conditions on PPC, pointer chains, measured progress, ResetIf/PauseIf and hit counts. Compare frame traces with rcheevos on a host reference without uploading synthetic awards.
2. **Games/images:** select officially supported hashes from the user's PS1 library. Cover a small/simple set, a larger set, a multi-disc RPG, NTSC and PAL, raw BIN/CUE, CHD, ISO and PBP only when each adapter is implemented. Test modified/unrecognized discs and ordinary imported cards. Determine current supported versions from RA, not the serial database alone.
3. **Execution/rendering:** Lightrec, PPC dynarec, interpreter; OpenGX and software GPU; frameskip and fast-forward where permitted; reset, restart, change game, multi-disc swap, state load/save and old-state files.
4. **Network/storage:** no configured SMB share, SMB streaming plus award traffic, Wi-Fi reconnect, failed TLS trust/date, server errors/rate limits, expired token, logout/login/account change, cached session offline, cold start offline, SD/USB unavailable and unexpected shutdown. Every in-flight request must have deterministic ownership and cancellation.
5. **Performance:** compare existing profiler/chained-run baselines on real Wii and vWii, with identical builds and scenes, plus Dolphin for functional debugging. Record per-frame runtime p50/p95/p99, emulated speed, stutters, MEM1/MEM2 peaks, allocations, TLS stack/handshake costs and SMB disc stalls. Provisional target: no more than 1% median emulation-speed regression and no missed achievement evaluations; refine notification and TLS memory budgets from measurements. If cost is excessive, optimize evaluated reads/cache/I/O; never skip achievement frames to hide it.
6. **Service behavior:** dedicated test account and upstream-supported test procedure, truthful user-agent/version, softcore/hardcore separation, no duplicate/retroactive fabricated awards, reset transitions, leaderboard cancellation and approved rejection behavior.

## First implementation slice

The next implementation task should be **phase 0 plus a host-tested PS1 memory callback and one local fixture**. Its deliverable is a small proof of feasibility with measured HTTPS/runtime/memory results, not a full feature presented as production-ready. Hash one known supported BIN/CUE image and one equivalent CHD through dedicated handles and prove identical hashes before connecting achievements to every supported format.

## Research and unresolved decisions

- [RetroAchievements integration details](ACHIEVEMENTS_RETROACHIEVEMENTS_RESEARCH.md): APIs, exact hash/map, async callbacks, offline limitations, registration and hardcore rules.
- [System comparison](ACHIEVEMENTS_SYSTEMS_RESEARCH.md): the broader ecosystem and alternatives.
- [DuckStation implementation trace](ACHIEVEMENTS_DUCKSTATION_RESEARCH.md): the PS1-specific reference, desktop assumptions, and independent WiiStation adaptations.
- Native verified HTTPS component and exact hardware footprint remain untested.
- Durable official offline queue API/support and storage protection requirements need confirmation against the pinned client/upstream.
- Hardcore approval, option policy and current supported game hashes are external dependencies, not guarantees.
- ESP32/companion routes are alternatives to benchmark, not prerequisites or already-working WiiStation support.
