#!/usr/bin/env bash
# AmiTCP_NG RX DMA A/B benchmark.
# Runs the same staged library and benchmark workload with the SANA-II DMA
# callback omitted (off) and advertised (auto), recreating the interface for
# every run so OpenDevice() sees the selected buffer-management tag list.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
G="$ROOT/emu/hdd/System/Workbench3.2"
RUNS="${RUNS:-5}"
CPU="${CPU:-68000}"
TIMEOUT="${TIMEOUT:-170}"
RESULTS="$(mktemp /tmp/amitcp-sana2-ab.XXXXXX)"
trap 'rm -f "$RESULTS"' EXIT

if [ "$RUNS" -lt 1 ]; then
  echo "RUNS must be positive" >&2
  exit 2
fi

echo "Building once for same-binary A/B comparison..."
bash "$ROOT/docker/build-lib.sh" >/dev/null
cp "$ROOT/build/bsdsocket.library" "$G/Libs/bsdsocket.library"

for i in $(seq 1 "$RUNS"); do
  if [ $((i % 2)) -eq 1 ]; then
    MODES="off auto"
  else
    MODES="auto off"
  fi
  for mode in $MODES; do
    echo "run $i/$RUNS mode=$mode CPU=$CPU"
    output="$(RXDMA="$mode" CPU="$CPU" TIMEOUT="$TIMEOUT" NOSTAGE=1 \
      bash "$ROOT/docker/run-bench.sh")"
    result="$(printf '%s\n' "$output" | sed -n \
      's/.*= \([0-9][0-9]*\) KB\/s mode=\([0-9][0-9]*\) dma_in=\([0-9][0-9]*\) byte_in=\([0-9][0-9]*\).*/\1 \2 \3 \4/p' | tail -1)"
    if [ -z "$result" ]; then
      echo "No parseable benchmark result for mode=$mode" >&2
      printf '%s\n' "$output" >&2
      exit 1
    fi
    printf '%s %s\n' "$mode" "$result" >> "$RESULTS"
  done
done

median_for()
{
  awk -v wanted="$1" '$1 == wanted { print $2 }' "$RESULTS" |
    sort -n |
    awk '{ v[NR] = $1 } END {
      if (NR == 0) exit 1;
      if (NR % 2) print v[(NR + 1) / 2];
      else print (v[NR / 2] + v[NR / 2 + 1]) / 2;
    }'
}

off_median="$(median_for off)"
auto_median="$(median_for auto)"

echo
echo "mode run kbps mode_code dma_in byte_in"
cat "$RESULTS"
echo
awk -v off="$off_median" -v auto="$auto_median" 'BEGIN {
  if (off == 0) {
    print "off median: 0 KB/s";
    exit;
  }
  printf "off median:  %.1f KB/s\n", off;
  printf "auto median: %.1f KB/s\n", auto;
  printf "change:      %+.1f%%\n", ((auto / off) - 1.0) * 100.0;
}'
