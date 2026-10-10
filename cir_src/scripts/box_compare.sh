#!/bin/bash
# Inside cir-env: cir-compare.py on test/cirgen/<name>.test against a C twin.
# usage: box_compare.sh <name> <twin.c> [cir-compare args]
n=$1; c=$2; shift 2
cat cir_src/test/cirgen/Inputs/header.txt cir_src/test/cirgen/$n.test > /tmp/$n.txt || exit 1
build/cir/tools/ecil-asm/ecil-asm /tmp/$n.txt -o /tmp/$n.ecil || exit 1
python3 cir_src/scripts/cir-compare.py --ecil /tmp/$n.ecil --c $c "$@"
