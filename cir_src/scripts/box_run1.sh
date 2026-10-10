#!/bin/bash
# Inside cir-env: run edg-cir on a stress case and show stderr.
# usage: box_run1.sh kind n [edg-cir args]
k=$1; n=$2; shift 2
python3 cir_src/scripts/ecil-stress-gen.py $k $n > /tmp/$k.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/$k.txt -o /tmp/$k.ecil || exit 1
build/cir/bin/edg-cir "${@:---emit=cir}" /tmp/$k.ecil 2>&1 | tail -${LINES_MAX:-12}
