#!/bin/sh
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# A stand-in for the front end (eccp's CPFE) that keeps the ECIL stream:
# cpfe-cir with --ecil_output=$KEEP_STREAM[.<n>].ecil instead of the library
# (the generated-file option is dropped).  KEEP_FE names the real cpfe-cir.
# Invocations without a generated file go straight to cpfe-cir.
out=
count=$#
while [ "$count" -gt 0 ]; do
  arg=$1
  shift
  case "$arg" in
    --gen_c_file_name=*) out=1 ;;
    *) set -- "$@" "$arg" ;;
  esac
  count=$((count - 1))
done
if [ -z "$out" ] || [ -z "$KEEP_STREAM" ]; then
  exec "$KEEP_FE" "$@"
fi
n=0
dest=$KEEP_STREAM.ecil
while [ -e "$dest" ]; do
  n=$((n + 1))
  dest=$KEEP_STREAM.$n.ecil
done
"$KEEP_FE" --ecil_output="$dest" "$@"
status=$?
[ -s "$dest" ] || rm -f "$dest"
exit $status
