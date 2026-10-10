#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: assemble test/cirgen/<name>.test and print CIR (or --emit=X).
# usage: box_cir.sh <name> [edg-cir args]
n=$1; shift
cat cir_src/test/cirgen/Inputs/header.txt cir_src/test/cirgen/$n.test > /tmp/$n.txt || exit 1
build/cir/tools/ecil-asm/ecil-asm /tmp/$n.txt -o /tmp/$n.ecil || exit 1
build/cir/bin/edg-cir "${@:---emit=cir}" /tmp/$n.ecil
