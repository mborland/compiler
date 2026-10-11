#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: synthetic stress streams through edg-cir under a 256 KiB
# main-thread stack (the work runs on edg-cir's own big-stack worker).
# usage: box_stress.sh [kind:n ...]
# The C++ stress set (the proposed cpp-stress gate step):
#   box_stress.sh cxx_locals:2000 cxx_temps:5000 cxx_meminit:2000 \\
#     cxx_cond_chain:1000 cxx_cond_seq:500 cxx_blocks:500 cxx_array:1000 \\
#     cxx_array_nest:100 cxx_funcs:6000
B=${EDG_CIR_BUILD:-build/cir}
asm=$B/tools/ecil-asm/ecil-asm
cir=$B/bin/edg-cir
gen=cir_src/scripts/ecil-stress-gen.py
mkdir -p /tmp/stress
cases=${@:-chain_left:50000 chain_right:50000 elseif:20000 nested_blocks:2000 init_deep:5000 agg_init:5000 flat:300000 loops:2000 switch:5000 ternary:5000 land:5000}
for c in $cases; do
  k=${c%%:*}; n=${c##*:}
  python3 $gen $k $n > /tmp/stress/$k.txt || { echo "$c GEN-FAIL"; continue; }
  $asm /tmp/stress/$k.txt -o /tmp/stress/$k.ecil || { echo "$c ASM-FAIL"; continue; }
  s=$(date +%s%N)
  ( ulimit -s 256; /usr/bin/env time -f "%M KB" $cir --emit=obj -o /tmp/stress/$k.o /tmp/stress/$k.ecil ) > /tmp/stress/$k.out 2> /tmp/stress/$k.err
  rc=$?
  e=$(date +%s%N)
  run=""
  if [ $rc = 0 ] && [ "${k#cxx_}" != "$k" ]; then
    # C++ kinds are self-checking programs: execute them (exit 0 = all
    # constructors and destructors ran, in reverse order).
    gcc /tmp/stress/$k.o -o /tmp/stress/$k.exe && /tmp/stress/$k.exe; run="run=$?"
    [ "$run" = "run=0" ] || rc=99
  fi
  printf '%-22s rc=%d %6d ms %s %s\n' "$c" $rc $(( (e - s) / 1000000 )) "$(tail -2 /tmp/stress/$k.err | tr '\n' ' ' | cut -c1-100)" "$run"
done
