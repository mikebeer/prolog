#!/bin/sh
# Run every bench/*.pl program with an interpreter and print the time it took.
# Usage: sh bench/run.sh ./c/prolog          (the C version)
#        sh bench/run.sh crexx-prolog        (CREXX: set CREXX_VM and use "crexx")
#        CREXX_VM=/path/crexx sh bench/run.sh --crexx
DIR=$(cd "$(dirname "$0")" && pwd)
ROOT="$DIR/.."
if [ "$1" = "--crexx" ]; then
  CREXX=${CREXX_VM:-crexx}
  run() { "$CREXX" "$ROOT/prolog.crexx" -nocompile -args "$1" < /dev/null 2>&1; }
else
  BIN=${1:-$ROOT/c/prolog}
  case "$BIN" in /*) ;; *) BIN="$(pwd)/$BIN" ;; esac
  run() { "$BIN" "$1" < /dev/null 2>&1; }
fi
for f in "$DIR"/*.pl; do
  name=$(basename "$f" .pl)
  start=$(date +%s)
  out=$(cd "$DIR" && run "$name.pl")
  end=$(date +%s)
  sum=$(printf '%s\n' "$out" | cksum | cut -d' ' -f1)
  echo "$name: $((end - start))s checksum $sum"
done
