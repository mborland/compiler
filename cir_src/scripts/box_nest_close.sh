#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: module destruction of a deeply nested (unflattened) CIR
# module under a 256 KiB main stack.  usage: box_nest_close.sh [depth]
n=${1:-10000}
python3 cir_src/scripts/ecil-stress-gen.py nested_blocks $n > /tmp/nb.txt
build/cir/tools/ecil-asm/ecil-asm /tmp/nb.txt -o /tmp/nb.ecil || exit 1
( ulimit -s 256; build/cir/bin/edg-cir --emit=cir --disable-cir-passes -o /dev/null /tmp/nb.ecil; echo "rc=$?" )
