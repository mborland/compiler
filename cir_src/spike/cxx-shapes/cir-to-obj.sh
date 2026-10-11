#!/bin/bash
# cir-to-obj.sh x.cir -> x.o through the pin's pipeline (prints the first error only)
f=$1; b=${f%.cir}; T=/edg/llvm-cir/bin
$T/cir-opt $f --cir-canonicalize --cir-target-lowering --cir-cxxabi-lowering --cir-lowering-prepare --cir-call-conv-lowering=target=x86_64 -o $b.abi.cir 2>&1 | head -c 1500; [ ${PIPESTATUS[0]} = 0 ] || { echo "FAIL abi"; exit 1; }
$T/cir-opt $b.abi.cir --cir-hoist-allocas --cir-flatten-cfg --cir-eh-abi-lowering --cir-goto-solver -o $b.flat.cir 2>&1 | head -c 1500; [ ${PIPESTATUS[0]} = 0 ] || { echo "FAIL flatten"; exit 1; }
$T/cir-translate --cir-to-llvmir $b.flat.cir -o $b.ll 2>&1 | head -c 1500; [ ${PIPESTATUS[0]} = 0 ] || { echo "FAIL translate"; exit 1; }
sed -i 's|^target datalayout = .*|target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"|' $b.ll
$T/clang -O0 -c $b.ll -o $b.o 2>&1 | grep -v warning | head -5
