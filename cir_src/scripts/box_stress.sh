#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: synthetic stress streams through edg-cir under a 256 KiB
# main-thread stack (the work runs on edg-cir's own big-stack worker).
# usage: box_stress.sh [kind:n ...]
# Environment:
#   EDG_CIR_BUILD            the build directory (default build/cir)
#   EDG_CIR_STRESS_STACK_KB  `ulimit -s` of the edg-cir run (default 256: the
#                            plain preset proves that nothing recurses on the
#                            main-thread stack; the ASan preset passes 8192 so
#                            that ASan's own reporting (leak symbolization at
#                            exit, deadly-signal handler) cannot overflow it)
#   EDG_CIR_STRESS_TIMEOUT   seconds allowed to each edg-cir run and to each
#                            run of a C++ self-checking program (default 600;
#                            then SIGTERM, and SIGKILL 10 s later).  A case
#                            that times out prints rc=124 (or 137) and the
#                            word TIMEOUT in its row.
# The C++ stress set (the proposed cpp-stress gate step):
#   box_stress.sh cxx_locals:2000 cxx_temps:5000 cxx_meminit:2000 \\
#     cxx_cond_chain:1000 cxx_cond_seq:500 cxx_blocks:500 cxx_array:1000 \\
#     cxx_array_nest:100 cxx_funcs:6000
B=${EDG_CIR_BUILD:-build/cir}
asm=$B/tools/ecil-asm/ecil-asm
cir=$B/bin/edg-cir
gen=cir_src/scripts/ecil-stress-gen.py
stack_kb=${EDG_CIR_STRESS_STACK_KB:-256}
limit=${EDG_CIR_STRESS_TIMEOUT:-600}
mkdir -p /tmp/stress
cases=${@:-chain_left:50000 chain_right:50000 elseif:20000 nested_blocks:2000 init_deep:5000 agg_init:5000 flat:300000 loops:2000 switch:5000 ternary:5000 land:5000}
for c in $cases; do
  k=${c%%:*}; n=${c##*:}
  python3 $gen $k $n > /tmp/stress/$k.txt || { echo "$c GEN-FAIL"; continue; }
  $asm /tmp/stress/$k.txt -o /tmp/stress/$k.ecil || { echo "$c ASM-FAIL"; continue; }
  s=$(date +%s%N)
  ( ulimit -s $stack_kb; /usr/bin/env time -f "%M KB" timeout -k 10 $limit $cir --emit=obj -o /tmp/stress/$k.o /tmp/stress/$k.ecil ) > /tmp/stress/$k.out 2> /tmp/stress/$k.err
  rc=$?
  e=$(date +%s%N)
  run=""
  mark=""
  if [ $rc = 124 ] || [ $rc = 137 ]; then
    mark="TIMEOUT"
  elif [ $rc = 0 ] && [ "${k#cxx_}" != "$k" ]; then
    # C++ kinds are self-checking programs: execute them (exit 0 = all
    # constructors and destructors ran, in reverse order).
    if gcc /tmp/stress/$k.o -o /tmp/stress/$k.exe; then
      timeout -k 10 $limit /tmp/stress/$k.exe; run="run=$?"
    else
      run="run=link-failed"
    fi
    if [ "$run" = "run=124" ] || [ "$run" = "run=137" ]; then
      mark="TIMEOUT"; rc=${run#run=}
    elif [ "$run" != "run=0" ]; then
      rc=99
    fi
  fi
  printf '%-22s rc=%d %6d ms %s %s %s\n' "$c" $rc $(( (e - s) / 1000000 )) "$(tail -2 /tmp/stress/$k.err | tr '\n' ' ' | cut -c1-100)" "$run" "$mark"
done
