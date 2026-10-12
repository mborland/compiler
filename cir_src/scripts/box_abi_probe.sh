#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, from the repo root: the two-TU ABI probes of
# cir_src/test/abi-probes/<name>/{p.h,defs.cpp,main.cpp}.  defs.cpp (the callee
# and the returner) and main.cpp (the caller) are each built by g++, by the
# pin's clang and by cpfe-cir + edg-cir, and every pairing is linked and run;
# a program prints "<name>: OK" and exits 0 when every value arrived intact.
#   usage: box_abi_probe.sh [-s STD] [name ...]      (default: all probes)
# Output: one line per pairing, "defs-by/main-by: OK|BAD|BUILD-FAIL".
W=$PWD
STD=c++20
if [ "$1" = "-s" ]; then STD=$2; shift 2; fi
P=$W/cir_src/test/abi-probes
names=("$@"); [ ${#names[@]} -eq 0 ] && names=($(ls $P))
export EDG_CIR_RT_LIBDIR=$W/build/cir/lib LD_LIBRARY_PATH=$W/build/cir/lib
FE=$W/build/gcc-cir/bin/cpfe-cir
LLVM=${EDG_LLVM_CIR:-/edg/llvm-cir}
out=$W/build/mine/abi
rm -rf $out; mkdir -p $out
build() {  # compiler source object
  case $1 in
    gxx)   g++ -w -O0 -std=$STD -I$P/$n -c $2 -o $3 ;;
    clang) $LLVM/bin/clang++ -w -O0 -std=$STD -I$P/$n -c $2 -o $3 ;;
    cir)   $FE --edg_base $W/bases/docker/cir-env/gcc --g++ --gnu_version=999999 \
             --${STD/++/++} -I$P/$n --no_wrap --no_wrap_diagnostics \
             --ecil_output=$3.ecil $2 > $3.fe 2>&1 &&
           $W/build/cir/bin/edg-cir --emit=obj -o $3 $3.ecil > $3.lib 2>&1 ;;
  esac
}
for n in "${names[@]}"; do
  for d in gxx clang cir; do
    build $d $P/$n/defs.cpp $out/$n.defs.$d.o 2>/dev/null || echo "$n: defs by $d: BUILD-FAIL"
    build $d $P/$n/main.cpp $out/$n.main.$d.o 2>/dev/null || echo "$n: main by $d: BUILD-FAIL"
  done
  for d in gxx clang cir; do
    for m in gxx clang cir; do
      [ -s $out/$n.defs.$d.o ] && [ -s $out/$n.main.$m.o ] || continue
      g++ -no-pie $out/$n.defs.$d.o $out/$n.main.$m.o -o $out/$n.$d.$m \
        -L$W/build/cir/lib -ledg_cir_rt -lm > $out/$n.$d.$m.link 2>&1 ||
        { echo "$n: defs=$d main=$m: LINK-FAIL"; continue; }
      timeout 20 $out/$n.$d.$m > $out/$n.$d.$m.out 2>&1; rc=$?
      printf "%s: defs=%s main=%s: rc=%s %s\n" $n $d $m $rc \
        "$(head -c 300 $out/$n.$d.$m.out | tr '\n' '|')"
    done
  done
done
