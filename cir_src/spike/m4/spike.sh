#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, repo root: one M4a spike.
#   spike.sh NAME [-S STD] [-- extra cc1 args]
# cir_src/spike/m4/NAME/ holds
#   cir.cpp    compiled through ClangIR: the pin's CIRGen (clang -cc1 -fclangir
#              -emit-cir -clangir-disable-passes) standing for what our library
#              would emit, then our pipeline's pass list (cir2obj.sh);
#   host.cpp   (optional) compiled by g++, linked with cir.cpp's object; it
#              includes the same headers and has main();
#   *.h        shared headers.
# Builds three programs and runs them: ref (g++ for everything), mixed (cir.cpp
# through ClangIR + host.cpp by g++) and, if there is no host.cpp, cir (alone);
# prints PASS when the outputs and exit codes agree.  Results are kept in
# build/mine/m4/NAME/ (cir.cir = CIRGen output, cir.lowered.cir, cir.ll).
W=$PWD; L=${EDG_LLVM_CIR:-/edg/llvm-cir}
n=$1; shift
STD=c++17
[ "$1" = "-S" ] && { STD=$2; shift 2; }
[ "$1" = "--" ] && shift
D=$W/cir_src/spike/m4/$n; O=$W/build/mine/m4/$n
ext=cpp; LANGX=c++; [ -f $D/cir.c ] && { ext=c; LANGX=c; }
SRC=$D/cir.$ext
[ $ext = c ] && STD=c11
rm -rf $O; mkdir -p $O; cd $O
[ $ext = c ] && CXXEXC=-fexceptions
CC1="$L/bin/clang -cc1 -triple x86_64-unknown-linux-gnu -x $LANGX -std=$STD ${CXXEXC:--fcxx-exceptions} -fexceptions -fclangir -resource-dir $($L/bin/clang -print-resource-dir) -internal-isystem $($L/bin/clang -print-resource-dir)/include -internal-isystem /usr/include -I$D"
# libstdc++ and libc headers of the host g++ (cc1 has no driver to find them)
SYS=; for d in $(echo | g++ -x c++ -E -v - 2>&1 | sed -n '/^#include <...>/,/^End of/p' | grep '^ /'); do SYS="$SYS -internal-isystem $d"; done
CC1="$CC1 $SYS"
host=; [ -f $D/host.cpp ] && host=$D/host.cpp
LF=; [ -f $D/link.flags ] && LF=$(cat $D/link.flags)
# reference: everything by g++
if [ $ext = c ]; then gcc -w -std=$STD -fexceptions -I$D -c $SRC -o ref.o 2>ref.err && g++ -w -std=c++17 -I$D ref.o $host -o ref $LF 2>>ref.err; else g++ -w -std=$STD -I$D -o ref $SRC $host $LF 2>ref.err; fi || { echo "$n: REF-BUILD-FAIL"; head -5 ref.err; exit 1; }
timeout 20 ./ref > ref.out 2>&1; echo $? > ref.rc
# ClangIR
$CC1 -emit-cir -clangir-disable-passes "$@" $SRC -o cir.cir 2> cirgen.err || { echo "$n: CIRGEN-FAIL"; head -8 cirgen.err; exit 1; }
# the printer can emit location aliases it does not define: drop locations
sed -E -i '/^#loc[0-9]* = /d; s/ loc\((fused\[[^]]*\]|[^()]*)\)//g' cir.cir
# a hand edit of CIRGen's output that stands for a shape our library emits differently
[ -f $D/patch.py ] && { python3 $D/patch.py cir.cir || { echo "$n: PATCH-FAIL"; exit 1; }; }
bash $W/cir_src/spike/m4/cir2obj.sh cir.cir cir.o > passes.err 2>&1 || { echo "$n: PASSES-FAIL"; head -12 passes.err; exit 1; }
[ -s cir.o ] || { echo "$n: NO-OBJECT"; head -12 passes.err; exit 1; }
if [ -n "$host" ]; then g++ -w -std=c++17 -I$D -c $host -o host.o; fi
g++ -no-pie cir.o ${host:+host.o} -o mixed $LF 2> link.err || { echo "$n: LINK-FAIL"; head -8 link.err; exit 1; }
timeout 20 ./mixed > mixed.out 2>&1; echo $? > mixed.rc
# the pin's own full pipeline, for comparison
$L/bin/clang -fclangir -O0 -w -std=$STD -fexceptions -I$D -c $SRC -o full.o 2> full.err &&
  g++ -no-pie full.o ${host:+host.o} -o full $LF 2>> full.err && { timeout 20 ./full > full.out 2>&1; echo $? > full.rc; } || echo "(clang -fclangir alone failed: $(head -c 200 full.err | tr '\n' ' '))"
if cmp -s ref.out mixed.out && cmp -s ref.rc mixed.rc; then echo "$n: PASS (mixed == ref; rc=$(cat ref.rc); $(wc -l < ref.out) lines)"; else echo "$n: FAIL"; diff ref.out mixed.out | head -10; echo "rc ref=$(cat ref.rc) mixed=$(cat mixed.rc)"; fi
[ -f full.out ] && { cmp -s ref.out full.out && cmp -s ref.rc full.rc && echo "   (clang -fclangir full pipeline: PASS)" || echo "   (clang -fclangir full pipeline: FAIL)"; }
