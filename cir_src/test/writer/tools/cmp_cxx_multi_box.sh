#!/bin/bash
# Container side: build the C++ files of .tmp-cmp/multi/<name>/*.cpp (several
# translation units of one program) with g++ and with cpfe-cir + edg-cir (one
# assembly per file), link each program, run both, and diff the output.
# Names come from .tmp-cmp/args.sh ("set -- name ...").
W=${EDG_WORKSPACE:-/edg/workspace}
cd $W
export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
FE=$W/build/gcc-cir/bin/cpfe-cir
. .tmp-cmp/args.sh
cd .tmp-cmp/multi
for n in "$@"; do
  echo "=== $n"
  (cd $n
   g++ -w -std=c++17 -o ../$n.gcc *.cpp 2>&1 | head -3
   objs=
   for f in *.cpp; do
     b=${f%.cpp}
     $FE --edg_base $W/bases/docker/cir-env/gcc --c++17 ${CMP_OPTIONS:-} --ecil_output=$b.ecil $f 2>&1 | head -20
     $W/build/cir/bin/edg-cir --emit=asm -o $b.s $b.ecil 2>&1 | head -20
     objs="$objs $b.s"
   done
   gcc -no-pie $objs -o ../$n.cir -L$W/build/cir/lib -ledg_cir_rt -lm -lstdc++ 2>&1 | head -10)
  ./$n.gcc > $n.gcc.out 2>&1; echo "gcc rc=$?"
  ./$n.cir > $n.cir.out 2>&1; echo "cir rc=$?"
  diff $n.gcc.out $n.cir.out > $n.diff && echo SAME || { echo DIFF; head -20 $n.diff; }
done
