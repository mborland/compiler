#!/bin/bash
# Inside cir-env: sample where edg-cir spends time (gdb interrupt after N s).
# usage: box_gdb.sh kind n seconds [edg-cir args]
k=$1; n=$2; t=$3; shift 3
python3 cir_src/scripts/ecil-stress-gen.py $k $n > /tmp/$k.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/$k.txt -o /tmp/$k.ecil || exit 1
( sleep $t; kill -INT $(pgrep -n -x edg-cir) ) &
gdb -batch -ex "handle SIGINT stop nopass" -ex run -ex "thread apply all bt 25" --args build/cir/bin/edg-cir "$@" -o /dev/null /tmp/$k.ecil 2>&1 | grep -E "^#" | cut -c1-200 | head -${LINES_MAX:-60}
