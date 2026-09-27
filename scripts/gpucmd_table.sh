#!/usr/bin/env bash
# gpucmd_table.sh [perf.log] -- the six most expensive GP0 commands per chained game
# (sums the "gpucmd:" lines of a PROBES=deep debug build): cmd=ms/calls.
log="${1:-.runs/softgpu_check/perf.log}"
awk '/^gpucmd:/ { for (i = 2; i <= NF; i++) { split($i, a, "[=/]"); t[a[1]] += a[2]; n[a[1]] += a[3] } }
/^=== chain / { match($0, /gpuPlugin=[0-9]/); p = substr($0, RSTART + 10, 1)
	match($0, /rom=[^[(]*/); r = substr($0, RSTART + 4, 16)
	s = ""; for (k in t) s = s sprintf("%s=%dms/%d\n", k, t[k] / 1000, n[k])
	cmd = "sort -t= -k2 -nr | head -6 | tr \"\n\" \" \""
	printf "%-16s p%s: ", r, p; printf "%s", s | cmd; close(cmd); print ""
	delete t; delete n }' "$log"
