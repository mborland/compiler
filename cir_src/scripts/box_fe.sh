#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, repo root: run cpfe-cir on a C file, keep the ECIL stream in
# /tmp/fe.ecil and print the diagnostics.  usage: box_fe.sh file.c [options]
# Env: SHOW_DUMP=1 (ecil-dump of the bodies), SHOW_CIR=1 (unverified CIR),
#      EMIT=llvm|asm|cir-lowered... (edg-cir output), EDG_CIR_DEBUG_NOTES=1.
f=$1; shift
export LD_LIBRARY_PATH=build/cir/lib EDG_CIR_TEE=/tmp/fe.ecil
build/gcc-cir/bin/cpfe-cir -DTEST_NUMBER=1 "$@" "$f" 2>&1 | head -${LINES_MAX:-20}
echo "== ecil bytes: $(wc -c < /tmp/fe.ecil)"
build/cir/tools/ecil-dump/ecil-dump /tmp/fe.ecil > /tmp/fe.dump 2>&1
if [ -n "$SHOW_DUMP" ]; then
  sed -n '/FUNC_BEGIN\|func_begin/,$p' /tmp/fe.dump | head -${DUMP_LINES:-200}
fi
unset EDG_CIR_TEE
if [ -n "$SHOW_CIR" ]; then
  EDG_CIR_NO_VERIFY=1 build/cir/bin/edg-cir --emit=cir --disable-cir-passes /tmp/fe.ecil 2>&1 | head -${CIR_LINES:-80}
fi
if [ -n "$EMIT" ]; then
  build/cir/bin/edg-cir --emit=$EMIT /tmp/fe.ecil 2>&1 | head -${EMIT_LINES:-80}
fi
