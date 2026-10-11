#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: compile a C file with gcc and with cpfe-cir + edg-cir, run
# both, diff the output.  usage: box_c_cmp.sh file.c [front end options]
f=$1; shift
W=$PWD; export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
mkdir -p build/scratch/cc; n=build/scratch/cc/t
gcc -w -o $n.gcc $f -lm && $n.gcc > $n.gcc.out 2>&1; echo "gcc rc=$?"
$W/build/gcc-cir/bin/cpfe-cir --edg_base $W/bases/docker/cir-env/gcc --gcc --gnu_version=999999 "$@" --ecil_output=$n.ecil $f 2>&1 | head -5
build/cir/bin/edg-cir --emit=obj -o $n.o $n.ecil 2>&1 | head -5
gcc -no-pie $n.o -o $n.cir -lm && $n.cir > $n.cir.out 2>&1; echo "cir rc=$?"
diff $n.gcc.out $n.cir.out > /dev/null && echo SAME || { echo DIFF; diff $n.gcc.out $n.cir.out | head -20; }
