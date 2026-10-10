#!/bin/bash
# Inside cir-env: capture the stream for a C file and print the ecil-dump
# lines around a pattern.  usage: box_dump_grep.sh file.c PATTERN [ctx] [opts]
f=$1; pat=$2; ctx=${3:-6}; shift 3
export LD_LIBRARY_PATH=build/cir/lib EDG_CIR_TEE=/tmp/fe.ecil
build/gcc-cir/bin/cpfe-cir -DTEST_NUMBER=1 --edg_base bases/docker/cir-env/gcc --gnu_version=999999 -e 200 --no_wrap --gcc "$@" "$f" > /tmp/fe.out 2>&1; head -4 /tmp/fe.out
build/cir/tools/ecil-dump/ecil-dump /tmp/fe.ecil 2>&1 | grep -n -B$ctx -A3 -i "$pat" | head -${LINES_MAX:-60}
