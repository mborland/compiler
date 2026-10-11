#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: compile tests/tests/cir/cirgen/<name>.sft.cpp with g++ (-std=c++17) and run it.
# usage: box_gcc1.sh name...   (the expected output of a self-checking test)
for n in "$@"; do
  f=tests/tests/cir/cirgen/$n.sft.cpp
  std=$(sed -n 's|^//options:.*--c++\([0-9]*\).*|\1|p' $f | head -1)
  mkdir -p /tmp/g1
  g++ -w -std=c++${std:-17} -o /tmp/g1/$n $f 2>&1 | head -10
  echo "=== $n"; /tmp/g1/$n; echo "rc=$?"
done
