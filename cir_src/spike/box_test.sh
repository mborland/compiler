#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, cwd=/edg/workspace.
B=build/cir-spike; S=$B/spike
$S -print-cir -o $B/spike.o > $B/pre.cir 2>$B/err.txt; echo "spike rc=$?"; cat $B/err.txt
gcc $B/spike.o -o $B/spike_bin && $B/spike_bin; echo "program exit=$?"
$S -S -o $B/spike.s -print-llvm > $B/spike.ll; echo "S rc=$?"
$S -O2 -S -o $B/spike_O2.s -print-cir-post > $B/post.cir; echo "O2 rc=$?"
ls -l $S; ldd $S
(ulimit -s 256; $S -stack-mb 64 -o /tmp/x.o; echo "ulimit256 rc=$?")
echo ---- PRE; cat $B/pre.cir
echo ---- POST; cat $B/post.cir
echo ---- LL; cat $B/spike.ll
echo ---- ASM; cat $B/spike.s
echo ---- O2ASM; grep -v '^\s*\.' $B/spike_O2.s
echo ---- CLANG
$EDG_LLVM_CIR_PREFIX/bin/clang -fclangir -emit-cir -Xclang -clangir-disable-passes -S cir_src/spike/spike.c -o - 2>&1
