#!/bin/bash
# Inside cir-env: dump clang's record layouts and CIR for a corpus.
# usage: box_layout_dump.sh <file.c>
/edg/llvm-cir/bin/clang -cc1 -triple x86_64-unknown-linux-gnu -fdump-record-layouts -emit-llvm-only "$1" 2>&1 | head -${LINES_MAX:-120}
