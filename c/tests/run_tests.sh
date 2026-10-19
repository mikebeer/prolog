#!/bin/sh
# Run the shared regression tests (../tests/NAME.pl, expected ../tests/NAME.out)
# with the C interpreter.  Tests listed in tests/skip.txt are not run.
# usage: sh tests/run_tests.sh [interpreter]      (default ./prolog)
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/../.."
PL=${1:-"$DIR/../prolog"}
case "$PL" in /*) ;; *) PL="$(pwd)/$PL" ;; esac
pass=0; fail=0; skip=0
for src in "$ROOT"/tests/*.pl; do
  name=$(basename "$src" .pl)
  if grep -qx "$name" "$DIR/skip.txt" 2>/dev/null; then
    skip=$((skip+1)); echo "skip $name"; continue
  fi
  input=/dev/null; [ -f "$ROOT/tests/$name.in" ] && input="$ROOT/tests/$name.in"
  actual=$(cd "$ROOT/tests" && timeout 120 "$PL" "$name.pl" < "$input" 2>&1)
  if [ "$actual" = "$(cat "$ROOT/tests/$name.out")" ]; then
    pass=$((pass+1)); echo "ok   $name"
  else
    fail=$((fail+1)); echo "FAIL $name"
    if [ -n "$VERBOSE" ]; then
      printf '%s\n' "$actual" > /tmp/pl_actual.txt
      diff /tmp/pl_actual.txt "$ROOT/tests/$name.out" | head -${VERBOSE}
    fi
  fi
done
echo "passed $pass, failed $fail, skipped $skip"
[ "$fail" -eq 0 ]
