#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: run writer-produced .ecil probes through the whole consumer.
# usage: box_probes.sh <dir>   (default probe)
d=${1:-probe}
dump=build/cir/tools/ecil-dump/ecil-dump
cir=build/cir/bin/edg-cir
for f in $d/*.ecil; do
  b=${f%.ecil}
  n=$(basename $b)
  printf '%-10s ' $n
  if ! $dump $f > $b.dump 2> $b.dumperr; then
    echo "VALIDATE-FAIL: $(head -1 $b.dumperr)"; continue
  fi
  if ! $cir --emit=obj -o $b.o $f > $b.cirout 2> $b.cirerr; then
    echo "EDG-CIR-FAIL($?): $(grep -m1 -i 'error' $b.cirerr)"; continue
  fi
  if [ -f $b.c ] && grep -q '\bmain\b' $b.c; then
    if gcc $b.o -o $b.exe -lm 2> $b.lderr; then
      timeout 10 $b.exe > $b.out 2>&1; rc=$?
      gcc -w $b.c -o $b.ref -lm 2> /dev/null && { timeout 10 $b.ref > $b.refout 2>&1; rr=$?; } || rr=nogcc
      if [ "$rc" = "$rr" ] && cmp -s $b.out $b.refout; then echo "OK run rc=$rc matches gcc"
      else echo "RUN-MISMATCH edg=$rc gcc=$rr"; fi
    else
      echo "LINK-FAIL: $(grep -m1 'undefined\|error' $b.lderr)"
    fi
  else
    echo "OK (object only)"
  fi
done
