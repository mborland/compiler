#!/bin/bash
# Container side of a C++ comparison (see cmp_box.sh): compile each .cpp of
# .tmp-cmp/files with g++ and with cpfe-cir + edg-cir, run both and diff the
# output.  Arguments come from .tmp-cmp/args.sh ("set -- file.cpp ...").
W=${EDG_WORKSPACE:-/edg/workspace}
cd $W
export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
. .tmp-cmp/args.sh
FE=${CMP_FE:-$W/build/gcc-cir/bin/cpfe-cir}
cd ${CMP_DIR:-.tmp-cmp/files}
for f in "$@"; do
  n=${f%.cpp}
  echo "=== $n"
  g++ -w -std=c++17 ${CMP_GCC_OPTIONS:-} -o $n.gcc $f 2>&1 | head -3
  $FE --edg_base $W/bases/docker/cir-env/gcc --c++17 ${CMP_OPTIONS:-} --ecil_output=$n.ecil $f 2>&1 | head -40
  $W/build/cir/bin/edg-cir --emit=asm -o $n.s $n.ecil 2>&1 | head -20
  gcc -no-pie $n.s -o $n.cir -L$W/build/cir/lib -ledg_cir_rt -lm -lstdc++ 2>&1 | head -10
  ./$n.gcc > $n.gcc.out 2>&1; echo "gcc rc=$?"
  ./$n.cir > $n.cir.out 2>&1; echo "cir rc=$?"
  diff $n.gcc.out $n.cir.out > $n.diff && echo SAME || { echo DIFF; head -20 $n.diff; }
done
