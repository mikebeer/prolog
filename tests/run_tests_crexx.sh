#!/bin/sh
# Run the shared regression corpus with the CREXX interpreter (../prolog.crexx).
# Usage: sh tests/run_tests_crexx.sh [--update] [NAME ...]
# CREXX_VM (or CREXX) names the crexx driver; the first run compiles the interpreter.
# Each test NAME.pl is consulted (crexx prolog.crexx -args NAME.pl) with NAME.in as
# standard input (empty when there is none); stdout+stderr are compared with NAME.out.
# --update rewrites the NAME.out files from the CREXX version (the reference output).
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/.."
CREXX=${CREXX_VM:-${CREXX:-crexx}}
update=0
if [ "$1" = "--update" ]; then update=1; shift; fi
if [ ! -f "$ROOT/prolog.rxbin" ] || [ "$ROOT/prolog.crexx" -nt "$ROOT/prolog.rxbin" ]; then
  echo "compiling prolog.crexx (first run only)..." >&2
  (cd "$ROOT" && "$CREXX" prolog.crexx -noexec >/dev/null 2>&1)
fi
if [ $# -gt 0 ]; then names="$*"; else
  names=$(cd "$DIR" && ls *.pl | sed 's/\.pl$//'); fi
pass=0; fail=0
for name in $names; do
  input=/dev/null; [ -f "$DIR/$name.in" ] && input="$DIR/$name.in"
  actual=$(cd "$DIR" && timeout 600 "$CREXX" "$ROOT/prolog.crexx" -nocompile -args "$name.pl" < "$input" 2>&1)
  if [ $update -eq 1 ]; then
    printf '%s\n' "$actual" > "$DIR/$name.out"; echo "updated $name"; continue
  fi
  if [ "$actual" = "$(cat "$DIR/$name.out")" ]; then
    pass=$((pass+1)); echo "ok   $name"
  else
    fail=$((fail+1)); echo "FAIL $name"
    printf '%s\n' "$actual" | diff - "$DIR/$name.out" | head -${VERBOSE:-0}
  fi
done
[ $update -eq 1 ] && exit 0
echo "passed $pass, failed $fail"
[ "$fail" -eq 0 ]
