#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# One C case: front end -> edg-cir -> link -> run.  Prints
# "path<TAB>class<TAB>message".  usage: box_c1.sh OUTDIR id path options...
out=$1; id=$2; path=$3; shift 3
W=$PWD; export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
n=${id}_$(basename $path); mkdir -p $out
timeout 120 $W/build/gcc-cir/bin/cpfe-cir --edg_base $W/bases/docker/cir-env/gcc --gcc --gnu_version=999999 -e 200 --no_wrap --no_wrap_diagnostics "$@" --ecil_output=$out/$n.ecil tests/tests/$path > $out/$n.fe 2>&1
if [ ! -s $out/$n.ecil ]; then printf "%s\tFE_FAIL\t%s\n" $path "$(grep -m1 error $out/$n.fe | cut -c1-120)"; exit 0; fi
timeout 120 build/cir/bin/edg-cir --emit=obj -o $out/$n.o $out/$n.ecil > $out/$n.lib 2>&1
rc=$?
msg=$(grep -m1 -E "error|crash|Assertion" $out/$n.lib | cut -c1-160)
if [ $rc -ne 0 ]; then printf "%s\tLIB_FAIL\t%s\n" $path "$msg"; exit 0; fi
gcc -no-pie $out/$n.o -o $out/$n.exe -lm > $out/$n.link 2>&1 || { printf "%s\tLINK_FAIL\t%s\n" $path "$(head -c 150 $out/$n.link | tr '\n' ' ')"; exit 0; }
timeout 20 $out/$n.exe > $out/$n.out 2>&1; rc=$?
if [ $rc -eq 0 ]; then printf "%s\tRUN_OK\t\n" $path; else printf "%s\tRUN_FAIL\trc=%s\n" $path $rc; fi
rm -f $out/$n.o $out/$n.exe
