# Testing online RetroAchievements on WiiStation

Researched 2026-10-11. This is a proposed test strategy, not an implemented online
client or a claim of service approval. No login, account creation, service award,
leaderboard submission, or account-progress reset was performed. The current
[engine](ACHIEVEMENTS_ENGINE.md) evaluates local definitions; its
[Wii tests](ACHIEVEMENTS_HARDWARE_TESTS.md) do not exercise online integration.

## What to build and test first

Use three gates: deterministic client tests with fake responses; verified Wii
HTTPS plus a real-service spectator session; then an explicitly authorized,
legitimate Casual award. Spectator and unpromoted achievements are useful tests,
but deliberately suppress awarding and cannot establish end-to-end submission.
Keep the existing local journal separate from official service unlocks.

The checkout vendors runtime/util sources and headers, **not** the client, API,
or hashing implementation. Online testing needs those sources from the same
pinned rcheevos revision, a PS1 sector/hash adapter, asynchronous HTTPS transport,
frame/idle pumping, lifecycle ownership, and observable status events. Preserve
the documented Wii endian fix when extending or updating the dependency.
[Vendored subset](../deps/rcheevos/README.wiistation.md),
[upstream integration guide](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration).

### Use the pinned API, not DuckStation's API names

For commit `683197d8dc92555a51e8280a4123d071ed66bff3`, the inspected header uses
`rc_client_set_unpromoted_enabled`, `PROMOTED`/`UNPROMOTED` categories and
`rc_client_begin_change_media` for an existing hash. DuckStation's separately
bundled version uses `set_unofficial_enabled` and `begin_change_media_from_hash`.
The pinned public header also has no `rc_client_get_pending_unlock_count`;
DuckStation's pending-count example cannot simply be copied. Freeze the chosen
source/header revision and compile the test harness against it. Use public events
and transport observations for pending/confirmed test assertions; investigate a
supported API before promising a production pending-count UI.
[Pinned header](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_client.h),
[DuckStation client setup](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/achievements.cpp#L579-L628).

## Gate 1: deterministic host client tests

Compile the actual pinned `rc_client` and WiiStation glue with ASan; apply UBSan
to glue as in the existing engine suite. Adapt upstream's fake asynchronous
`server_call`, canned JSON and controllable millisecond clock instead of writing
another achievement protocol. No network listener is needed for most tests.
Upstream already exercises delayed/aborted/destroyed login and award retry cases,
including HTTP 429. Retain the relevant upstream tests, then add tests of our
queue, lifetime and status boundaries.
[Upstream client tests](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/test/client/test_rc_client.c).

The minimum deterministic matrix is:

| Case | Required evidence |
| --- | --- |
| Password/token login | Successful login, invalid credentials, expired token, special-character encoding, no password retained |
| Load/identify | Correct game/set, unknown hash, empty set, unsupported condition, malformed/truncated/oversized JSON |
| Ownership | Copy request URL/body/content type before returning from `server_call`; response storage survives its callback; each transport request completes once |
| Cancellation | Abort login/load, swap games, logout, destroy; delayed transport completion neither revives the old session nor accesses freed glue |
| Activation/threading | Memory validation and client mutation occur on the emulator owner thread; delayed load never reads freed RAM |
| Frame/idle | One evaluation per emulated frame through frameskip; pause pumps `rc_client_idle` and completions without earning frames |
| Events | Trigger/challenge/progress and server-error/disconnected/reconnected sequence; callbacks queue safe snapshots for rendering |
| Retry | Timeout, connection loss, 429 and server failure; advance fake clock through upstream retries; permanent errors do not retry forever |
| Confirmation | Trigger event precedes possible server success; rejected or pending awards are never reported as confirmed |
| Identity isolation | Late response for game/account A does not become progress for B; confirmed unlocks cannot be revoked by an older Casual state |
| State | Sized progress round-trip, missing/corrupt/mismatched blob, pending network requests excluded from state restore |

Set hardcore off immediately after creation: it defaults on in the pinned header.
Set background memory reads off for WiiStation's proposed owner-thread design,
then test activation through frame/idle processing. Aborting an rc_client handle
suppresses its high-level completion callback; the transport must still resolve
its own callback/lifetime obligations. Include delayed completions after
`rc_client_destroy`, matching the upstream regression pattern, rather than
assuming abort cancelled a socket.
[Pinned header](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_client.h),
[client implementation](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c).

For device transport tests, add a small fixture service on the bench host serving
those same canned responses. The Wii connects to the host's LAN address;
`localhost` on Wii is the Wii. Use dummy identities only. A fixture may count
synthetic `awardachievement` requests to prove transport without official awards.
Restrict this override to the test build so dummy traffic cannot accidentally
reach production. A local HTTP fixture validates sockets/HTTP only; a TLS fixture
with a deliberately trusted test CA additionally validates TLS behavior. Neither
establishes actual RA service interoperability. These are proposed engineering
controls, not an existing RA staging environment.

## Canonical PS1 hashes before service requests

Use upstream PS1 hashing over an independent logical-sector reader. Its input
is the boot executable name and contents, not the whole image's MD5 or `CdromId`.
Compare Wii output to the same upstream host hasher; DuckStation is a second
reference on valid PS-X EXE discs. The adapter must not seek the emulator's live
reader. Freeze hashes, format, disc index and executable identity in results.
[PS1 hasher](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_hash_disc.c#L930),
[DuckStation hash](https://github.com/stenzek/duckstation/blob/cd024df237d46faa4c65574b101261b31267e2fd/src/core/system.cpp#L902-L925).

Start with the original synthetic disc and upstream disc-reader fixtures, then
user-owned supported games. Cover equivalent cooked ISO/raw BIN-CUE, pregaps,
track offsets, truncated/missing boot executable, region/revision/patch mismatch,
and each selected multi-disc image. Add CHD/PBP only after their decompressed-sector
adapters exist. For a known-supported game, compare against its current RA-linked
hash; an unknown image must remain unidentified, never be forced to a nearby
serial's set. Do not upload disc images, save cards or RAM dumps.
[Hash callbacks/default reader](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_hash.h),
[CD reader](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/hash/rc_cdreader.c).

## Gate 2: real Wii HTTPS, then spectator

Before credentials, prove DNS, connection and HTTPS against RA using a bounded
public GET. Validate hostname, certificate chain, trust roots and usable clock;
test rejection of wrong hostname, untrusted CA and expired certificate against
the fixture. Reject HTTPS-to-HTTP redirects for credential-bearing requests.
Record elapsed handshake time, MEM1/MEM2 and stack peaks, body limits, timeout,
keepalive/reconnection behavior and impact while SMB streams a disc. Existing
SMB connectivity does not prove TLS. No Wii HTTPS implementation is selected or
verified by this research; this is a prerequisite experiment.
[Guide: transport supplied by emulator](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration).

For the first authenticated service run, set **spectator before loading any game**
and hardcore off. The pinned client skips session creation; it does not schedule
the periodic presence ping for that spectator load and suppresses award and
leaderboard requests. Assert this at the outgoing transport boundary with a
fail-closed allowlist. Spectator cannot be disabled for that loaded game; unload
first. Do not enable spectator only after starting a normal session and call it
read-only. Unpromoted achievements never submit awards even outside spectator,
but a normal session can still publish session/presence activity.
[Client load/award/ping paths](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/client/rc_client.c),
[upstream spectator regression](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/test/client/test_rc_client.c).

### Service requests by effect

The Connect API normally sends encoded POSTs to `https://retroachievements.org/dorequest.php`.
Classify by the `r` operation, not by HTTP verb. The table describes inspected
protocol intent, not a guarantee of zero server access logs or analytics.
[Runtime request builders](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/api/rc_api_runtime.c),
[user request builders](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/src/api/rc_api_user.c).

| Operation | Effect / test boundary |
| --- | --- |
| `gameid`, `achievementsets`, legacy `patch`, `unlocks`; image GETs | Retrieval; no intended achievement/presence mutation, but authenticated retrieval exposes identity |
| `login2` | Authentication/token issuance or validation; sensitive account action, not anonymous read-only research |
| `startsession` | Starts play session; treat as write even though it returns existing unlocks |
| `ping` | Publishes session/Rich Presence activity; write |
| `awardachievement` | Changes official achievement progress; write requiring the authorized real-award gate |
| `submitlbentry` | Changes leaderboard records; excluded from initial Casual live testing |

The public Web API's information endpoints can verify account/game data but are
not a substitute for the Connect award protocol. Its API key and the login token
are different secrets. No public sandbox server was established by this research;
obtain an upstream-supported test procedure before assuming one exists.
[Official public Web API](https://api-docs.retroachievements.org/).

## Gate 3: an actual service-confirmed Casual award

The present research request does not authorize account access or submissions.
Before a live award, agree on a user-authorized test account, exact supported
image/hash and one legitimate early achievement, with any upstream test-account
procedure confirmed. A spare account does not authorize fabricated achievements.
Use normal gameplay from a clean boot and clean copied card; no RAM injection,
modified promoted definitions, canned gameplay trace or imported late-game save
should reach production. The synthetic fixture remains on the mock service.
No leaderboard submits; hardcore stays off. Do not reset anyone's existing
progress merely to repeat a test.
[Player rules](https://docs.retroachievements.org/guidelines/users/global-leaderboard-and-achievement-hunting-rules.html),
[guide's development-mode recommendation](https://github.com/RetroAchievements/rcheevos/wiki/rc_client-integration).

Capture pre-run unlock status, matching image hash, truthful incrementing
`WiiStation/<numeric-version>` user agent, event timestamp, redacted request kind,
response success and independently retrieved post-run Casual unlock. Restart and
verify service-restored unlock status. A popup, optimistic score change or request
being sent is insufficient: the server and subsequent retrieval must agree.
One legitimately earned award plus mock fault tests is preferable to repeatedly
writing test awards to a real set. Service rate-limit/failure injection belongs
on the fixture, not repeated intentional abuse of production.

Hardcore requires a separate review and RA validation; a successful login or
Casual award is not approval. Test restrictions against the mock first: every
state-load/statetool/autoboot path, cheat/debug/input playback route, resume,
slowdown/frame advance, configuration changes and Casual-to-Hardcore full reset.
Keep scripted gameplay/performance automation disconnected from production
Hardcore. Leaderboards, Rich Presence, UI indicators, secure queueing and unique
identity are additional compliance gates.
[Current Hardcore requirements](https://docs.retroachievements.org/general/hardcore-compliance-requirements.html).

## Outages, storage and credentials

Test connection drop after an authenticated normal session, multiple pending
awards, reconnect while playing and while paused, terminal rejection, logout,
account switch and exit with pending work. Use the fixture to prove request
ownership, retry timing and separation; later use only a normal temporary network
outage in an authorized live session. Do not disconnect the shared bench's
network manually outside a leased, recoverable test.

The current FAQ requires online session startup and keeping the emulator open
through reconnection. Fully offline startup and restart-safe official award
synchronization are not established. Progress serialization and WiiStation's
local journal are not a durable official outbox. Test interrupted shutdown as an
explicit limitation; do not replay local journal lines as fabricated service
awards. Secure durable queueing needs an upstream-supported design and policy
clarification before it becomes a claimed feature.
[Offline FAQ](https://docs.retroachievements.org/general/faq.html#can-i-earn-achievements-offline),
[pinned progress API](https://github.com/RetroAchievements/rcheevos/blob/683197d8dc92555a51e8280a4123d071ed66bff3/include/rc_client.h).

Keep passwords out of ordinary settings. Remember only username/token when the
user opts in, in private ignored storage excluded from frozen manifests, state
metadata, crash collection and exported configurations. Test logout removal,
failed/corrupt storage and SD/USB loss. Removable SD is not a secure vault; storing
a decryption key alongside the ciphertext does not change that. Redact password,
token, Web API key, POST bodies and authenticated URLs from all logs; retain
operation/status/timing instead. DuckStation's desktop encryption and downloader
are behavioral references, not Wii components to copy.
[DuckStation transport/token handling](ACHIEVEMENTS_DUCKSTATION_RESEARCH.md).

## Hardware execution and acceptance artifacts

Every Wii probe uses the shared HBC-Reborn queue lease, a frozen DOL/matching ELF,
collector and fixture version, fresh private SD directory and exact restoration
of staged settings/logs. Host research/hash/mock tests can run without the lease;
Wii network, TLS and gameplay cannot. Follow [bench testing](BENCH_TESTING.md).
Stage dummy secrets for fixtures; real credentials must be separately provisioned
for an authorized run and excluded from normal evidence collection.

A run report should state its gate (mock, TLS, spectator or confirmed Casual
award), platform, build hashes, rcheevos revision, game hash/format/disc, CPU/GPU,
request counts by kind, event ordering, TLS results, memory/latency/frame-cost
measurements, cleanup verification and remaining limitations. Check all three
CPU cores for memory/evaluation and all three renderer choices (Soft Fast, Soft Timed and OpenGX) for eventual notifications;
measure network activity separately from frame evaluation. Gate 1 passing does
not prove TLS; gate 2 passing does not prove awarding; gate 3 does not prove
Hardcore compliance or durable offline support.

## First implementation slice

1. Vendor the matching client/API/hash sources and build a host-only rc_client
   harness with dummy credentials, fake asynchronous responses and an injected
   clock. Prove ownership/cancellation, spectator request suppression and mocked
   Casual award retry/confirmation before adding a real endpoint.
2. Add the sector adapter and canonical hash tests for the synthetic disc and
   supported BIN/CUE images; leave CHD/PBP unadvertised until separately proven.
3. Build a small leased Wii HTTPS probe and fixture-client DOL to measure verified
   TLS, callback behavior and resource use. Select the transport from these
   results; do not integrate an unmeasured blocking downloader into frame logic.
4. Integrate owner-thread frame/idle pumping and settings-file controls, with
   online disabled by default, hardcore unavailable and explicit status text.
   Then propose the concrete authorized spectator and Casual-award runs.

These steps can be completed without production awards. Real account credentials,
a chosen legitimate award, upstream guidance for test accounts, native Wii TLS
feasibility and Hardcore approval remain separate dependencies. This report
contains no evidence that any of those online gates already passed.
