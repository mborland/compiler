#!/bin/bash
# usage: box_cir2.sh <name> <header-file-name> : like box_cir.sh with another header
n=$1; h=$2; shift 2
cat cir_src/test/cirgen/Inputs/$h cir_src/test/cirgen/$n.test > /tmp/$n.txt || exit 1
build/cir/tools/ecil-asm/ecil-asm /tmp/$n.txt -o /tmp/$n.ecil || exit 1
build/cir/bin/edg-cir "${@:---emit=cir}" /tmp/$n.ecil
