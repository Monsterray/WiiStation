# rcheevos sources

`include/`, `src/runtime/`, `src/util/`, `src/client/`, `src/api/`, and `src/hash/` from
[RetroAchievements/rcheevos](https://github.com/RetroAchievements/rcheevos/tree/683197d8dc92555a51e8280a4123d071ed66bff3),
commit `683197d8dc92555a51e8280a4123d071ed66bff3` (2026-10-10).

The emulator compiles only the local C evaluation runtime. Client/API/hash sources
are exercised by standalone host/Wii tests; online integration into the emulator
is still pending. See LICENSE and the original MD5 source notices.

`scripts/test-achievements-client.sh` verifies and downloads the pinned upstream
test archive, then runs its runtime/API/client suite with ASan (hash tests disabled).
Run it before `scripts/build-achievements-client-test.sh`, which uses that archive
for the Wii mock-client tests. `scripts/build-achievements-hasher.sh` and
`scripts/build-achievements-hash-wii-test.sh` build independent PS1 hash tools.

Local patch: `src/runtime/rc_memref.c`, `rc_read_memory`, assembles guest bytes in
little-endian order on a big-endian host. The pinned upstream implementation
shifted the first byte to the high end, causing 8/16-bit values to be masked out
and 32-bit values to be reversed on Wii. The hardware one-byte regression failed
before this fix. Keep this patch when updating, unless upstream includes the fix.
