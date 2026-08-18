#!/usr/bin/env bash
#
# setup.sh  Opus 5/Andrew Belles
#
# Prepares a machine to build and test this project. Idempotent; safe to rerun.
#
#   scripts/setup.sh          install what is missing, then verify
#   scripts/setup.sh --check  verify only, change nothing
#   scripts/setup.sh --yes    do not prompt before installing
#
# Covers the toolchain, libzmq, V4L2 headers, valgrind, perf, the perf sysctl,
# and the flamegraph scripts under tools/.

set -euo pipefail

TOOLS_DIR=${TOOLS_DIR:-tools}
FLAMEGRAPH_DIR=$TOOLS_DIR/FlameGraph
FLAMEGRAPH_URL=https://github.com/brendangregg/FlameGraph
PARANOID_WANT=1
SYSCTL_FILE=/etc/sysctl.d/99-sparse-camera.conf

check_only=0
assume_yes=0

for arg in "$@"; do
  case $arg in
    --check) check_only=1 ;;
    --yes|-y) assume_yes=1 ;;
    -h|--help) sed -n '3,13p' "$0"; exit 0 ;;
    *) echo "setup: unknown option $arg" >&2; exit 2 ;;
  esac
done

missing_pkgs=()
problems=()

have() { command -v "$1" >/dev/null 2>&1; }

note()  { printf '  %-28s %s\n' "$1" "$2"; }
ok()    { note "$1" "ok"; }
bad()   { note "$1" "$2"; problems+=("$1"); }

want_pkg() {
  # want_pkg <label> <apt package> <test command...>
  local label=$1 pkg=$2; shift 2

  if "$@" >/dev/null 2>&1; then
    ok "$label"
  else
    bad "$label" "missing ($pkg)"
    missing_pkgs+=("$pkg")
  fi
}

echo "setup: checking dependencies"

want_pkg "gcc"            build-essential      have gcc
want_pkg "make"           build-essential      have make
want_pkg "git"            git                  have git
want_pkg "perl"           perl                 have perl
want_pkg "libzmq headers" libzmq3-dev          test -f /usr/include/zmq.h
want_pkg "v4l2 headers"   linux-libc-dev       test -f /usr/include/linux/videodev2.h
want_pkg "valgrind"       valgrind             have valgrind
want_pkg "perf"           linux-tools-generic  have perf

# perf record opens no events unless this is 2 or lower.
paranoid=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo unknown)

if [[ $paranoid == unknown ]]; then
  bad "perf_event_paranoid" "cannot read"
elif (( paranoid <= 2 )); then
  ok "perf_event_paranoid ($paranoid)"
else
  bad "perf_event_paranoid" "is $paranoid, needs <= 2"
fi

if [[ -x $FLAMEGRAPH_DIR/stackcollapse-perf.pl && -x $FLAMEGRAPH_DIR/flamegraph.pl ]]; then
  ok "flamegraph scripts"
else
  bad "flamegraph scripts" "not in $FLAMEGRAPH_DIR"
fi

if (( ${#problems[@]} == 0 )); then
  echo "setup: everything present"
  exit 0
fi

if (( check_only )); then
  echo "setup: ${#problems[@]} item(s) missing; run scripts/setup.sh to fix" >&2
  exit 1
fi

echo
echo "setup: will change the following"

(( ${#missing_pkgs[@]} )) && echo "  apt install ${missing_pkgs[*]}"

if [[ $paranoid != unknown ]] && (( paranoid > 2 )); then
  echo "  set kernel.perf_event_paranoid=$PARANOID_WANT, persisted in $SYSCTL_FILE"
fi

[[ -d $FLAMEGRAPH_DIR ]] || echo "  clone $FLAMEGRAPH_URL into $FLAMEGRAPH_DIR"

if (( ! assume_yes )); then
  read -r -p "proceed? [y/N] " reply
  [[ $reply == [yY] ]] || { echo "setup: aborted"; exit 1; }
fi

if (( ${#missing_pkgs[@]} )); then
  # unique, preserving order
  mapfile -t pkgs < <(printf '%s\n' "${missing_pkgs[@]}" | awk '!seen[$0]++')
  sudo apt-get update
  sudo apt-get install -y "${pkgs[@]}"

  # the generic meta package can lag the running kernel
  have perf || sudo apt-get install -y "linux-tools-$(uname -r)" || true
fi

if [[ $paranoid != unknown ]] && (( paranoid > 2 )); then
  echo "kernel.perf_event_paranoid = $PARANOID_WANT" \
    | sudo tee "$SYSCTL_FILE" >/dev/null
  sudo sysctl -w "kernel.perf_event_paranoid=$PARANOID_WANT" >/dev/null
fi

if [[ ! -d $FLAMEGRAPH_DIR ]]; then
  mkdir -p "$TOOLS_DIR"
  git clone --depth 1 "$FLAMEGRAPH_URL" "$FLAMEGRAPH_DIR"
fi

echo
exec "$0" --check
