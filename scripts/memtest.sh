#!/usr/bin/env bash
#
# memtest.sh  Opus 5/Andrew Belles
#
# run the smoke test under valgrind memcheck.
#
#   scripts/memtest.sh [config]
#
# Defaults to configs/ini/memtest.ini. Override BIN or ART in the environment.
# Exits 1 on a memory error, otherwise the smoke test's own status.

set -euo pipefail

BIN=${BIN:-build/bin/smoke}
ART=${ART:-artifacts}
CONFIG=${1:-configs/ini/memtest.ini}

if ! command -v valgrind >/dev/null; then
  echo "memtest: valgrind not installed; run scripts/setup.sh" >&2
  exit 1
fi

if [[ ! -x $BIN ]]; then
  echo "memtest: $BIN not built; run make first" >&2
  exit 1
fi

if [[ ! -f $CONFIG ]]; then
  echo "memtest: no such config: $CONFIG" >&2
  exit 1
fi

mkdir -p "$ART"

interrupted=0
trap 'interrupted=1' INT TERM

flags=(
  --tool=memcheck
  --leak-check=full
  --show-leak-kinds=all
  --errors-for-leak-kinds=definite,indirect
  --track-origins=yes
  --track-fds=yes
  --trace-children=yes
  --num-callers=40
  --fair-sched=yes    # otherwise valgrind can starve a camera thread for the run
  --error-exitcode=99 # separates a memory error from a failed smoke check
)

log=$ART/memtest.log
out=$ART/memtest.stdout

echo "memtest: $CONFIG -> $log"

status=0
valgrind "${flags[@]}" --log-file="$log" "$BIN" "$CONFIG" >"$out" 2>&1 || status=$?

if (( interrupted || status == 130 || status == 143 )); then
  echo "memtest: interrupted; partial log in $log" >&2
  exit 130
fi

tail -n 20 "$log"

case $status in
  0)  echo "memtest: PASS" ;;
  99) echo "memtest: FAIL, valgrind reported errors, see $log" >&2; exit 1 ;;
  *)  echo "memtest: valgrind clean; smoke checks failed ($status)" >&2
      exit "$status" ;;
esac
