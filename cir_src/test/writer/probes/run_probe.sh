#!/bin/sh
f=$1; shift
B=/Users/mattborland/Documents/compiler/bases/cmake-native/macos-arm/clang
STDF=${STD:---c11}
/Users/mattborland/Documents/compiler/build/clang/bin/cpfe-cp --edg_base $B $STDF --il_display "$@" $f > ${f%.c}.il 2>&1
python3 -I /tmp/cirp/tools/sum.py ${f%.c}.il > ${f%.c}.sum
