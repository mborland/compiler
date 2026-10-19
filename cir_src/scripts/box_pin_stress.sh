#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, at the tree root: the stress watch list (m4_stress.md and
# m31_reassessment.md section a) at one optimization level, for comparing two
# LLVM pins.   usage: box_pin_stress.sh OPT   (OPT: 0 or 2)
opt=${1:-0}
export EXTRA="-O$opt" TMO=${TMO:-400} STRESS_DIR=${STRESS_DIR:-/tmp/pinstress$opt}
echo "== M4 stress kinds at -O$opt"
bash cir_src/scripts/box_m4_stress.sh wide:10000 vchain:1000 vchain:2000 \
  diamond:80 diamond:160 vbases:400 tuple:2000 tries:500 tries:1000 \
  news:1250 news:2500 unwind:2000 unwind:4000 unwindcall:4000
echo "== C stress kinds (ecil-stress-gen) at -O$opt"
B=${EDG_CIR_BUILD:-build/cir}
mkdir -p /tmp/cstress$opt
for c in chain_left:50000 chain_right:50000 elseif:20000 nested_blocks:2000 \
         init_deep:5000 agg_init:5000 flat:300000 loops:2000 switch:5000 \
         ternary:5000 land:5000; do
  k=${c%%:*}; n=${c##*:}
  python3 cir_src/scripts/ecil-stress-gen.py $k $n > /tmp/cstress$opt/$k.txt &&
  $B/tools/ecil-asm/ecil-asm /tmp/cstress$opt/$k.txt -o /tmp/cstress$opt/$k.ecil || { echo "$c GEN-FAIL"; continue; }
  s=$(date +%s%N)
  ( ulimit -s 256; /usr/bin/env time -f "RSS %M" timeout -k 10 $TMO $B/bin/edg-cir $EXTRA --emit=obj -o /tmp/cstress$opt/$k.o /tmp/cstress$opt/$k.ecil ) 2> /tmp/cstress$opt/$k.err
  rc=$?
  e=$(date +%s%N)
  printf '%-14s %7d rc=%d total_ms=%d rss=%s %s\n' "$k" "$n" $rc $(( (e - s) / 1000000 )) "$(grep -h '^RSS' /tmp/cstress$opt/$k.err | cut -d' ' -f2)" "$(grep -hiE 'error' /tmp/cstress$opt/$k.err | head -1 | cut -c1-80)"
done
echo "== done"
