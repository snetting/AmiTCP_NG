#!/usr/bin/env bash
# A/B benchmark for the SANA-II receive-copy layout.
set -euo pipefail
export PATH="/tmp/amitcp-docker-shim:$PATH"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
G="$ROOT/emu/hdd/System/Workbench3.2"
RUNS="${RUNS:-3}"
CPU="${CPU:-68000}"
CHIPMEM="${CHIPMEM:-2}"
FASTMEM="${FASTMEM:-8}"
SPEED="${SPEED:-}"
RESULTS="$(mktemp /tmp/amitcp-rxcopy-ab.XXXXXX)"
trap 'rm -f "$RESULTS"' EXIT

podman unshare chown -R 0:0 "$ROOT/build" "$ROOT/emu" >/dev/null 2>&1 || true
"$ROOT/docker/build-lib.sh" >/dev/null
cp "$ROOT/build/bsdsocket.library" "$G/Libs/bsdsocket.library"
cp "$ROOT/build/AddNetInterface" "$G/C/AddNetInterface"

for i in $(seq 1 "$RUNS"); do
  if [ $((i % 2)) -eq 1 ]; then MODES="split contiguous"; else MODES="contiguous split"; fi
  for mode in $MODES; do
    echo "run $i/$RUNS rx_copy=$mode CPU=$CPU CHIPMEM=$CHIPMEM FASTMEM=$FASTMEM"
    output="$(RXCOPY="$mode" RXDMA=off CPU="$CPU" CHIPMEM="$CHIPMEM" FASTMEM="$FASTMEM" SPEED="$SPEED" NOSTAGE=1 \
      bash "$ROOT/docker/run-bench.sh")"
    result="$(printf '%s\n' "$output" | sed -n \
      's/.*= \([0-9][0-9]*\) KB\/s mode=.*dma_in=\([0-9][0-9]*\) byte_in=\([0-9][0-9]*\).*/\1 \2 \3/p' | tail -1)"
    if [ -z "$result" ]; then echo "No parseable result" >&2; printf '%s\n' "$output" >&2; exit 1; fi
    printf '%s %s\n' "$mode" "$result" >> "$RESULTS"
  done
done

median_for() {
  awk -v wanted="$1" '$1 == wanted { print $2 }' "$RESULTS" | sort -n |
    awk '{ v[NR] = $1 } END { if (NR % 2) print v[(NR+1)/2]; else print (v[NR/2]+v[NR/2+1])/2 }'
}
split_median="$(median_for split)"
contig_median="$(median_for contiguous)"
echo
echo "rx_copy run kbps dma_in byte_in"
cat "$RESULTS"
printf 'split median:      %.1f KB/s\n' "$split_median"
printf 'contiguous median: %.1f KB/s\n' "$contig_median"
awk -v splitrate="$split_median" -v contig="$contig_median" 'BEGIN {
  printf "change:            %+.1f%%\n", ((contig / splitrate) - 1.0) * 100.0
}'
