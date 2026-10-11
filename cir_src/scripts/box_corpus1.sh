#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# One case of box_corpus.sh: front end -> edg-cir --emit=asm.  Prints
# "path<TAB>class<TAB>message".  usage: box_corpus1.sh OUTDIR path variant...
out=$1; id=$2; path=$3; shift 3
W=$PWD; export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
n=${id}_$(basename $path); mkdir -p $out
timeout 120 $W/build/gcc-cir/bin/cpfe-cir --edg_base $W/bases/docker/cir-env/gcc --g++ \
  --gnu_version=999999 -tused -e 200 --no_wrap --no_wrap_diagnostics "$@" --ecil_output=$out/$n.ecil \
  tests/tests/$path > $out/$n.fe 2>&1
frc=$?
if [ ! -s $out/$n.ecil ]; then printf "%s\tFE_FAIL\trc=%s\n" $path $frc; rm -f $out/$n.*; exit 0; fi
timeout 120 build/cir/bin/edg-cir ${CORPUS_KG---keep-going} --emit=asm -o $out/$n.s $out/$n.ecil > $out/$n.lib 2>&1
rc=$?
msg=$(grep -m1 -E "error|crash|Assertion|Aborted" $out/$n.lib | cut -c1-200)
case $rc in
  0) c=OK;; 124) c=TIMEOUT;;
  *) if echo "$msg" | grep -q -E "internal crash|internal error|Assertion|Aborted"; then c=LIB_CRASH; elif echo "$msg" | grep -qi "unsupported\|not support"; then c=UNSUPPORTED; else c=LIB_ERROR; fi;;
esac
printf "%s\t%s\t%s\n" $path $c "$msg"
rm -f $out/$n.s
