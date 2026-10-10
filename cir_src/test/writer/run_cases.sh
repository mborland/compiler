#!/bin/sh
# Golden tests of the writer through the real front end.
# Usage: run_cases.sh [-r] CPFE_CIR ECIL_DUMP [FE options...]
#   -r  record the expected dumps (cases/<name>.dump)
# Each case is run twice (determinism) as
#   CPFE_CIR --ecil_output=- <options> case.c | ECIL_DUMP
# and compared with cases/<name>.dump.  The error case must exit nonzero and
# its dump must end with F_ABORT.
record=0
if [ "$1" = "-r" ]; then record=1; shift; fi
cpfe=$1; dump=$2; shift 2
here=$(cd "$(dirname "$0")" && pwd)
tmp=${TMPDIR:-/tmp}/ecil-cases.$$
mkdir -p "$tmp"
status=0
for c in empty main_return0 error_abort; do
  for pass in 1 2; do
    (cd "$here/cases" && "$cpfe" --ecil_output="$tmp/$c.$pass.ecil" "$@" "$c.c") \
        >"$tmp/$c.$pass.out" 2>&1
    echo $? > "$tmp/$c.$pass.rc"
    "$dump" "$tmp/$c.$pass.ecil" > "$tmp/$c.$pass.dump" 2>&1
  done
  cmp -s "$tmp/$c.1.ecil" "$tmp/$c.2.ecil" || { echo "$c: NOT DETERMINISTIC"; status=1; }
  if [ "$c" = error_abort ]; then
    [ "$(cat "$tmp/$c.1.rc")" != 0 ] || { echo "$c: expected a failing exit"; status=1; }
  else
    [ "$(cat "$tmp/$c.1.rc")" = 0 ] || { echo "$c: unexpected failure"; cat "$tmp/$c.1.out"; status=1; }
  fi
  if [ $record = 1 ]; then cp "$tmp/$c.1.dump" "$here/cases/$c.dump"; fi
  if cmp -s "$tmp/$c.1.dump" "$here/cases/$c.dump"; then echo "$c: ok"
  else echo "$c: MISMATCH"; diff "$tmp/$c.1.dump" "$here/cases/$c.dump" | head; status=1; fi
done
rm -rf "$tmp"
exit $status
