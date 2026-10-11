# Entropy for authenticated Wii HTTPS

Researched 2026-10-11. The smallest defensible candidate is Mbed TLS's existing
nonvolatile seed facility, initialized with a unique secret from a trusted
computer and updated durably before TLS uses it. That is a conditional fallback,
not a hardware random generator. We can implement and fault-test its storage
boundary now; we cannot claim authenticated Wii TLS is safe with the current
prebuilt libraries and bench provisioning path. No seed, console key, credential,
hardware access or production account action was used for this research.

## Why the current port cannot carry credentials

AndrewPiroli's Wii patch seeds SHA1 from `gettime`, device ID and MAC, then mixes
timer samples. Its README explicitly calls this fake entropy. The local
`wii/.deps/network/lib/libmbedcrypto.a` in the NetSurf checkout agrees: the
`entropy_poll.o` dependencies and disassembly contain those operations. Labeling
the callback strong in Mbed TLS does not establish unpredictability. Certificate
and hostname checks passing in the public probe prove those checks, not secret
session-key generation. Keep that DOL credential-free.
[Port patch](https://raw.githubusercontent.com/AndrewPiroli/wii-curl/master/wii-mbedtls/wii-mbedtls.patch),
[port README](https://github.com/AndrewPiroli/wii-curl).

## Is a Wii hardware or IOS source available?

The installed pinned libogc2 headers and `libogc.a` expose no entropy/random
service. Its Broadway-facing IOS API supplies IPC open/read/write/ioctl calls;
that does not expose arbitrary Starlet kernel syscalls. The inspected ES
implementation has no random command. This is evidence about the available API,
not proof that Hollywood has no source of randomness.
[libogc2 ES source](https://raw.githubusercontent.com/extremscorner/libogc2/master/libogc/es.c),
[local pinned setup](../scripts/setup-devkitpro-local.sh).

Reverse-engineered IOS syscall tables identify internal `IOSC_GenerateRand`, but
no supported PPC IPC route or assessed entropy quality was established here.
An ARM kernel syscall number cannot simply be passed to `IOS_Ioctl`. Exploring
custom IOS, exploits or key extraction would be a separate platform project;
none is necessary for the current gameplay tests. Stock IOS SSL is also a
different transport, not an established entropy callback for modern Mbed TLS.
[IOS syscall research](https://wiibrew.org/wiki/IOS/Syscalls).

## The supported NV seed option

For a source build matching Mbed TLS 3.6.4, enable `MBEDTLS_ENTROPY_NV_SEED`,
`MBEDTLS_PLATFORM_NV_SEED_ALT`, `MBEDTLS_ENTROPY_C` and `MBEDTLS_PLATFORM_C`;
disable the fake platform source with `MBEDTLS_NO_PLATFORM_ENTROPY`. Leave
default-source registration enabled. Provide exact read/write callbacks through
`mbedtls_platform_set_nv_seed`. Generate at least `MBEDTLS_ENTROPY_BLOCK_SIZE`
secret bytes on a trusted OS CSPRNG; the current SHA512 configuration uses 64.
Each Wii requires a different seed. Rebuild Mbed TLS and curl against identical
configuration headers: enabling NV seed changes the entropy context layout and
cannot be added by defining a macro only in WiiStation.
[Mbed TLS NV porting guide](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how_to_integrate_nv_seed/),
[3.6.4 entropy source](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/entropy.c).

The first extraction from each entropy context updates the persistent seed
before releasing entropy. The default file writer checks `fwrite` length but
ignores `fclose` errors and provides no durable transaction. Custom callbacks
must check exact lengths, flush/close and supported persistence operations;
return a negative error on any incomplete commit. After a seed-update failure,
destroy that entropy context rather than retrying it: 3.6.4 sets its initial-run
flag before attempting the update. Serialize owners so two contexts cannot read
the same old seed concurrently. Prove storage behavior before selecting FAT or
an application-owned NAND file; neither has been validated here.
[3.6.4 platform writer](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/platform.c),
[3.6.4 initialization and update](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/entropy.c).

NV-only entropy has weaker compromise/cloning properties than fresh randomness.
An SD backup, seed rollback or cloned emulator state can repeat generator
states; normal reseeding alone does not solve it. Exclude seed and TLS/DRBG state
from save states, frozen bundles, collected logs and backups intended for replay.
If the seed becomes known, provision new secret entropy; hashing public device
identifiers cannot restore secrecy.
[Mbed TLS cloning analysis](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/random_generator_cloning/).

## Bootstrap and what we can safely do now

The existing HBC client uses plain TCP with compression/CRC, not confidential
transport. Uploading a secret seed that way exposes it to a LAN observer. A
public HTTPS download made with the weak generator cannot bootstrap secrecy
against an observer able to recover that connection. Provision physically on a
trusted computer or establish a separately reviewed confidential provisioning
channel. Plain SD storage additionally assumes the card remains private; an
encryption key beside the seed on the same card adds no such protection.
[HBC file transport source](../../hbc-reborn/tools/hbc.py),
[NV provisioning/security requirements](https://mbed-tls.readthedocs.io/en/latest/kb/how-to/how_to_integrate_nv_seed/).

| Option | Can proceed now? |
| --- | --- |
| Current fake source | Public feasibility probes only; never credentials |
| NV seed with custom storage | Host/Wii dummy storage and source-built public HTTPS pass; secure device bootstrap, verified RTC configuration and durability still required |
| Fresh one-use OS randomness file | Controlled experiment after confidential provisioning; fail closed on exhaustion and never reuse a frozen file |
| Native IOS/hardware source | No verified callable source or quality assessment; investigate separately |

The next useful implementation is deterministic host testing of missing,
truncated and read-only seeds, short writes, close/commit errors, concurrent
contexts, and simulated crashes at each rotation boundary. It can use public
dummy material and mock storage. Then verify private provisioning and real
storage persistence under a leased hardware run. No available tool establishes
that confidential bootstrap automatically today; authenticated RetroAchievements
must remain gated on those concrete results.

## UTC clock is a separate prerequisite

The parent hardware run reported `time(NULL)=1791682853` around 2026-10-11
08:40 UTC: roughly seven hours behind, consistent with a Pacific local clock.
That observation does not prove the cause. Mbed TLS's certificate checks compare
validity dates against its current-time callback; correct entropy does not fix
an incorrect epoch. Require an explicit UTC policy and reject authenticated
connections when UTC is unknown or implausible. Keep date verification enabled;
test not-yet-valid and expired certificates close to the boundary, including
timezone/DST changes. The existing broad expiration test passing does not prove
UTC accuracy.
[3.6.4 certificate time checks](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/x509_crt.c),
[UTC conversion and comparisons](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/library/x509.c).

With rebuilt libraries, `MBEDTLS_PLATFORM_TIME_ALT` and
`mbedtls_platform_set_time` can supply TLS with a reviewed UTC clock without
changing the console RTC. A configured local offset must be explicit and
maintained; adding seven hours universally would be wrong. Unauthenticated NTP,
an HTTP Date header, or plain HBC host time should not silently become the
security authority against a hostile network. Review a trustworthy UTC bootstrap
and use monotonic elapsed time within a session. No clock was changed here.
[3.6.4 time callback interface](https://raw.githubusercontent.com/Mbed-TLS/mbedtls/mbedtls-3.6.4/include/mbedtls/platform_time.h).

## Host contract test implemented

`scripts/test-achievements-entropy.sh` downloads/checks the official Mbed TLS
3.6.4 release archive (SHA-256
`ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110`) and rebuilds
its crypto library with NV seed callbacks and platform entropy disabled. The
ASan/UBSan harness `tests/entropy_seed_test.c` confirms exactly one entropy source,
rotation before DRBG output, different output across successive fresh contexts,
missing/truncated/read failure rejection, negative commit-error rejection, and
fresh-context rejection on repeated failures. It always destroys a context after
a failed update. The storage is a public in-memory fixture, not a FAT/NAND backend;
this proves the library/callback contract, not confidential provisioning,
atomicity, power-loss durability or concurrent-owner exclusion on Wii. The source
only checks callback results for negativity: a real callback must turn short I/O
and flush/close/commit failures into negative errors itself.

## File-backed implementation and Wii test

`achievement_seed.c` implements the NV callbacks with exact 64-byte reads,
exclusive `.busy` directory ownership, exclusive candidate creation, checked
write/flush/fsync/close, remove/rename and checked readback. The owner holds the
marker for the whole TLS session. Any callback error keeps the marker; a new
session refuses an existing marker and never promotes a leftover candidate or
restores an old seed. Calls must be serialized by one TLS owner. Missing seeds
are not synthesized from device identifiers. Failed contexts must still be
destroyed. Recovery requires a new privately provisioned seed.

The same `tests/entropy_storage_test.c` harness passes under host ASan/UBSan and
on the physical Wii (queue job `20261011-025257-be84db`, 20 seconds, zero failures).
It verifies successful fsync/rotation/reopen, distinct fresh-context outputs,
owner exclusion, missing/empty/truncated/oversized rejection, an exclusive-write
failure with unchanged application output, and refusal of simulated interrupted
rotation artifacts. Only deterministic public dummy data was used, inside the
collector's owned UUID directory. No seed was provisioned for authenticated use.

`scripts/test-achievements-entropy.sh --wii OUTPUT` now builds all three Mbed TLS
libraries from the checksum-verified official release with matched headers. Wii
uses `MBEDTLS_PLATFORM_MS_TIME_ALT` backed by `__SYS_GetSystemTime()` for milliseconds; the
upstream Unix socket convenience and timing modules are disabled. This does not
change the console RTC. The UTC callback policy is described below. `scripts/build-achievements-native-https-test.sh`
rebuilds official curl 8.16.0 against these headers and the pinned r41-2 SDK,
without applying the Wii port's fake-entropy patch. Curl's configure correctly
disables unavailable `realpath` and `fnmatch` functions, so the old probe stubs
are unnecessary. The remaining prebuilt component is the checked wiisocket
adapter, with the three duplicate libogc inet helpers removed in a private copy.

The final clean probe passed on Wii in job `20261011-032017-49eea1` (31 seconds):
RA and control certificates accepted, wrong-host/untrusted/expired certificates
rejected, seed owner released, fixture removed and clean HBC return. Initial
native failures occurred after successful HTTPS checks: the probe removed the
socket adapter before final seed/result I/O. Moving that teardown after result
writing fixed the regression. Failure diagnostics are now fsynced and collected
before owned-directory cleanup.

This validates reported filesystem operations and simulated restart artifacts,
not physical power-cut durability or resistance to a rolled-back/cloned SD card.
Private bootstrap and production client/transport integration
remain required before authenticated service traffic.


## Explicit UTC correction

`achievement_clock.c` accepts a quoted signed minute correction, added to the
Wii RTC epoch. `AchievementUTCOffsetMinutes = ""` is the production settings
default and means unknown UTC. The current local achievement engine does not
perform TLS; the source-built HTTPS probe exercises this policy using a public
`clock-offset.txt` fixture. Pacific daylight time needs `420`, standard time
`480`, and a verified UTC clock `0`. These are examples, not automatic timezone
detection. Verify the console calendar/time and update the correction when
its timezone or daylight-time setting changes. A correction cannot fix RTC drift.

The SDK distinguishes wall-clock `gettime()` from monotonic
`__SYS_GetSystemTime()`. We snapshot corrected RTC at the start of a TLS session
and advance it from the latter, so a wall-clock/DST adjustment does not move a
running session's UTC. No `settime`, RTC write, unauthenticated NTP response or
HTTP Date header is used. [SDK time source](https://raw.githubusercontent.com/extremscorner/libogc2/master/libogc/timesupp.c).

Mbed TLS is rebuilt with `MBEDTLS_PLATFORM_TIME_ALT` and
`MBEDTLS_PLATFORM_GMTIME_R_ALT`. Unknown, malformed, implausible, overflowing or
regressing time fails closed: the GMTIME callback returns NULL so both upstream
certificate-date predicates reject. Returning time `-1` alone would incorrectly
produce a 1969 date. The portable range is 2020–2099; the TLS adapter additionally
checks whether the target time type can represent the result. Host ASan/UBSan
checks exercise actual Mbed TLS date predicates one second before, exactly at,
and one second after a certificate boundary, different UTC corrections, DST
offsets, invalid configuration, integer bounds and counter rollback.

The physical Wii passed the same clock assertions and all five public HTTPS
cases in queue job `20261011-040024-514221` (37 seconds, clean HBC return).
Evidence is in `.runs/bench/20261011-utc-https/`: the frozen DOL/ELF and matched
configuration/library/source hashes, `results/probe.log`, and
`host-clock-window.json`. With the explicit `420` fixture, raw RTC `1791691271`
became UTC `1791716471`, inside the host's recorded before/after window. That
bounds the observed offset correction; it is not a clock synchronization or
long-term drift measurement. Valid RA/control TLS passed; wrong-host,
untrusted-root and expired certificates returned curl 60 without HTTP. The RA
HTTP response remained 403, so this is transport/date validation, not API login.
The pinned r41-2 production release and settings consistency check also passed.
