#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, repo root: capture the ECIL stream cpfe-cir sends for a C
# file, then replay it through edg-cir under gdb and print the backtrace.
# usage: box_fe_gdb.sh file.c [cpfe-cir options]
f=$1; shift
export LD_LIBRARY_PATH=build/cir/lib EDG_CIR_TEE=/tmp/fe.ecil
build/gcc-cir/bin/cpfe-cir -DTEST_NUMBER=1 --edg_base bases/docker/cir-env/gcc --gnu_version=999999 -e 200 --no_wrap --no_wrap_diagnostics --gcc "$@" "$f" 2>&1 | head -5
unset EDG_CIR_TEE
gdb -batch -ex "handle SIGSEGV stop nopass" -ex "handle SIGABRT stop nopass" -ex run -ex "bt 18" --args build/cir/bin/edg-cir --emit=asm -o /dev/null /tmp/fe.ecil 2>&1 | grep -E "^#|SIG|edg-cir:" | cut -c1-220 | head -${LINES_MAX:-30}
