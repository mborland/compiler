#!/bin/bash
# usage: spike_cir.sh file.cpp  -> file.raw.cir (CIRGen only), file.cir (default pipeline), *.txt = stripped
f=$1; b=${f%.cpp}; C=/edg/llvm-cir/bin/clang
$C -fclangir -fno-exceptions -O0 -emit-cir -Xclang -clangir-disable-passes $f -o $b.raw.cir 2>&1 | grep -v warning | head
$C -fclangir -fno-exceptions -O0 -emit-cir $f -o $b.cir 2>&1 | grep -v warning | head
for x in $b.raw.cir $b.cir; do
  grep -v "^#loc\|^module\|^!\|^} loc\|^#" $x | sed -E 's/ loc\([^)]*\)//g; s/ attributes \{"cir.target-cpu"[^}]*(\}|, )//g; s/"cir.target-cpu" = "x86-64", "cir.target-features" = "[^"]*", "cir.tune-cpu" = "generic", //' > $x.txt
done
