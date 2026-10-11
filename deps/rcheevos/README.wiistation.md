# rcheevos runtime subset

`include/`, `src/runtime/`, and `src/util/` from
[RetroAchievements/rcheevos](https://github.com/RetroAchievements/rcheevos/tree/683197d8dc92555a51e8280a4123d071ed66bff3),
commit `683197d8dc92555a51e8280a4123d071ed66bff3` (2026-10-10).

Only the local C evaluation runtime is compiled. Client/API/hash/network integration
is not included in this first slice. See LICENSE and the original MD5 source notices.

Local patch: `src/runtime/rc_memref.c`, `rc_read_memory`, assembles guest bytes in
little-endian order on a big-endian host. The pinned upstream implementation
shifted the first byte to the high end, causing 8/16-bit values to be masked out
and 32-bit values to be reversed on Wii. The hardware one-byte regression failed
before this fix. Keep this patch when updating, unless upstream includes the fix.
