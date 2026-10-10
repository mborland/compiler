#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: per-frame cost of the crash-recovery guard.  Builds an
# N-function stream (one F_REGION frame per function) and feeds it frame by
# frame with and without the guard.  usage: box_guard_bench.sh [N]
n=${1:-20000}
python3 cir_src/scripts/ecil-stress-gen.py funcs $n > /tmp/funcs.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/funcs.txt -o /tmp/funcs.ecil || exit 1
export LD_LIBRARY_PATH=build/cir/lib
for round in 1 2 3; do
  echo -n "guarded:   "; build/cir/test/driver/consume_bench /tmp/funcs.ecil
  echo -n "unguarded: "; EDG_CIR_NO_GUARD=1 build/cir/test/driver/consume_bench /tmp/funcs.ecil
done
