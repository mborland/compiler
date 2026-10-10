#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, repo root: cpfe-cir -> assembly -> link -> run one C file.
# usage: box_fe_run.sh file.c [cpfe-cir options]; prints the exit status.
f=$1; shift
export LD_LIBRARY_PATH=$PWD/build/cir/lib
d=/tmp/ferun; rm -rf $d; mkdir -p $d; cp "$f" $d/t.c
(cd $d && $OLDPWD/build/gcc-cir/bin/cpfe-cir -DTEST_NUMBER=1 --edg_base $OLDPWD/bases/docker/cir-env/gcc --gnu_version=999999 -e 200 --no_wrap --no_wrap_diagnostics --gcc "$@" t.c) 2>&1 | head -20
ls $d
[ -n "$SHOW_ASM" ] && head -${ASM_LINES:-80} $d/t.int.s
gcc $d/t.int.s -o $d/t.exe -lm 2>&1 | head && $d/t.exe; echo "exit=$?"
