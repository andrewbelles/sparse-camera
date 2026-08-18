#!/usr/bin/env bash
#
# perf.sh  Opus 5/Andrew Belles
#
# profile the smoke test and write a breakdown to artifacts/.
#
#   scripts/perf.sh [config]

set -euo pipefail

BIN=build/bin/smoke-perf
ART=artifacts
FLAMEGRAPH=tools/FlameGraph
FREQ=999

CONFIG=${1:-configs/ini/perf.ini}

# perf report otherwise fetches symbols over the network for every build-id.
export DEBUGINFOD_URLS=

paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo 4)

if ! command -v perf >/dev/null || (( paranoid > 2 )); then
  echo "perf: environment not ready; run scripts/setup.sh" >&2
  exit 1
fi

if [[ ! -x $BIN ]]; then
  echo "perf: $BIN not built; run make perf" >&2
  exit 1
fi

if [[ ! -f $CONFIG ]]; then
  echo "perf: no such config: $CONFIG" >&2
  exit 1
fi

mkdir -p "$ART"
run=$ART/perf-run.log
: >"$run"

# Ctrl-C reaches the whole foreground group; this stops the script following it.
interrupted=0
trap 'interrupted=1' INT TERM

abort_if_interrupted() {
  (( interrupted )) || return 0
  echo "perf: interrupted; any samples captured are in $ART/perf.data" >&2
  exit 130
}

echo "perf: $CONFIG at ${FREQ} Hz -> $ART/"

# Stacks stop at libzmq, which is built without frame pointers.
status=0
perf record -F "$FREQ" -g --call-graph=fp -o "$ART/perf.data" -- \
    "$BIN" "$CONFIG" >>"$run" 2>&1 || status=$?

# 130 and 143 are SIGINT and SIGTERM reaching the workload directly
if (( status == 130 || status == 143 )); then interrupted=1; fi
abort_if_interrupted

if [[ ! -s $ART/perf.data ]]; then
  echo "perf: no samples recorded, see $run" >&2
  exit 1
fi

perf report -i "$ART/perf.data" --stdio --sort=overhead,symbol \
    >"$ART/perf-report.txt" 2>/dev/null || true
abort_if_interrupted

perf report -i "$ART/perf.data" --stdio -g graph,0.5,caller \
    >"$ART/perf-callgraph.txt" 2>/dev/null || true
abort_if_interrupted

if [[ -x $FLAMEGRAPH/stackcollapse-perf.pl ]]; then
  perf script -i "$ART/perf.data" \
    | "$FLAMEGRAPH/stackcollapse-perf.pl" \
    | "$FLAMEGRAPH/flamegraph.pl" --title "sparse-camera" \
    >"$ART/flamegraph.svg"
else
  echo "perf: no flamegraph tooling; run scripts/setup.sh" >&2
fi

echo "perf: wrote $ART/"
head -n 25 "$ART/perf-report.txt"
