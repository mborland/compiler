#!/bin/bash
# Inside cir-env: time the stages for one stress case.  usage: box_time.sh kind n
k=$1; n=$2
python3 cir_src/scripts/ecil-stress-gen.py $k $n > /tmp/$k.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/$k.txt -o /tmp/$k.ecil || exit 1
for mode in "--emit=cir --disable-cir-passes" "--emit=cir" "--emit=llvm" "--emit=obj"; do
  s=$(date +%s%N)
  build/cir/bin/edg-cir $mode -o /dev/null /tmp/$k.ecil > /dev/null 2>&1
  e=$(date +%s%N)
  printf '%-34s %6d ms\n' "$mode" $(( (e - s) / 1000000 ))
done
