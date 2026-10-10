#!/bin/bash
k=$1; n=$2; shift 2
python3 cir_src/scripts/ecil-stress-gen.py $k $n > /tmp/$k.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/$k.txt -o /tmp/$k.ecil || exit 1
perf record -o /tmp/perf.data -F 499 -g build/cir/bin/edg-cir "$@" -o /dev/null /tmp/$k.ecil > /dev/null 2>/tmp/perf.err && perf report -i /tmp/perf.data --no-children --stdio 2>/dev/null | grep -v "^#" | grep "%" | head -${TOP:-25}
head -3 /tmp/perf.err
