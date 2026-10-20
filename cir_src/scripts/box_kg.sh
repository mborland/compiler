# usage: box_kg.sh [emit]  -- c/pr114907 with --keep-going
S=build/streams/c/imported__gnu__c__pr114907.sft.c.0.ecil
build/cir/bin/edg-cir --emit=${1:-cir} --keep-going -o /tmp/k.out $S 2>&1 | tail -6
[ "$1" = "" ] && grep -n "cir.func\|cir.call\|cir.cast" /tmp/k.out | head -30
