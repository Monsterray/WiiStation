#!/usr/bin/env bash
# gpucmd_table.sh [perf.log] -- the six most expensive GP0 commands per chained game
# (sums the "gpucmd:" lines of a PROBES=deep debug build): cmd=ms/calls.
log="${1:-.runs/softgpu_check/perf.log}"
[ -f "$log" ] || { echo "gpucmd_table.sh: no such perf.log $log (needs a PROBES=deep run)" >&2; exit 1; }
awk '/^gpucmd:/ { saw = 1; for (i = 2; i <= NF; i++) { split($i, a, "[=/]"); t[a[1]] += a[2]; n[a[1]] += a[3] } }
END { if (!saw) print "gpucmd_table.sh: no gpucmd: lines (needs a PROBES=deep run)" > "/dev/stderr" }
/^=== chain / { match($0, /gpuPlugin=[0-9]/); p = substr($0, RSTART + 10, 1)
	match($0, /rom=[^[(]*/); r = substr($0, RSTART + 4, 16)
	n6 = 0
	for (k in t) { v = t[k] / 1000; j = n6 + 1
		while (j > 1 && v > topv[j - 1]) { topk[j] = topk[j - 1]; topv[j] = topv[j - 1]; topn[j] = topn[j - 1]; j-- }
		if (j <= 6) { topk[j] = k; topv[j] = v; topn[j] = n[k]; if (n6 < 6) n6++ } }
	s = ""; for (j = 1; j <= n6; j++) s = s sprintf("%s=%dms/%d ", topk[j], topv[j], topn[j])
	printf "%-16s p%s: %s\n", r, p, s
	delete t; delete n; delete topk; delete topv; delete topn }' "$log"
