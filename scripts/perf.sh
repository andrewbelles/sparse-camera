#!/usr/bin/env bash
#
# perf.sh  Opus 5/Andrew Belles
#
# profile the smoke test and write a breakdown to artifacts/.
#
#   scripts/perf.sh [config]
#
# Defaults to configs/ini/perf.ini. Override BIN, ART, FREQ, CALLGRAPH, STAT,
# or FLAMEGRAPH_DIR in the environment. STAT=1 adds a second counters pass.
# Writes perf.data and the reports named at the end of this script into ART.

set -euo pipefail

BIN=${BIN:-build/bin/smoke-perf}
ART=${ART:-artifacts}
FREQ=${FREQ:-999}
# Stacks stop at libzmq, built without frame pointers; CALLGRAPH=dwarf sees through it.
CALLGRAPH=${CALLGRAPH:-fp}
FLAMEGRAPH_DIR=${FLAMEGRAPH_DIR:-tools/FlameGraph}

CONFIG=${1:-configs/ini/perf.ini}

# perf report otherwise fetches symbols over the network; DEBUGINFOD=1 re-enables.
[[ ${DEBUGINFOD:-0} == 1 ]] || export DEBUGINFOD_URLS=

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

# One pass; a counters pass would double a wall time bound by the slowest camera.
status=0
perf record -F "$FREQ" -g --call-graph="$CALLGRAPH" -o "$ART/perf.data" -- \
    "$BIN" "$CONFIG" >>"$run" 2>&1 || status=$?

# 130 and 143 are SIGINT and SIGTERM reaching the workload directly
if (( status == 130 || status == 143 )); then interrupted=1; fi
abort_if_interrupted

if [[ ${STAT:-0} == 1 ]]; then
  perf stat -o "$ART/perf-stat.txt" "$BIN" "$CONFIG" >>"$run" 2>&1 || true
  abort_if_interrupted
fi

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

perf annotate -i "$ART/perf.data" --stdio \
    >"$ART/perf-annotate.txt" 2>/dev/null || true
abort_if_interrupted

if [[ -x $FLAMEGRAPH_DIR/stackcollapse-perf.pl ]]; then
  perf script -i "$ART/perf.data" \
    | "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" \
    | "$FLAMEGRAPH_DIR/flamegraph.pl" --title "sparse-camera" \
    >"$ART/flamegraph.svg"
  echo "perf: wrote $ART/flamegraph.svg"
else
  echo "perf: no flamegraph tooling; run scripts/setup.sh" >&2
fi

echo "perf: wrote perf-report.txt, perf-callgraph.txt, perf-annotate.txt to $ART/"
head -n 25 "$ART/perf-report.txt"
