#!/bin/bash
k=$1; n=$2; shift 2
python3 cir_src/scripts/ecil-stress-gen.py $k $n > /tmp/$k.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/$k.txt -o /tmp/$k.ecil || exit 1
build/cir/bin/edg-cir "$@" -o /dev/null /tmp/$k.ecil 2>&1 | head -40
which perf gdb valgrind 2>&1 | head -3
