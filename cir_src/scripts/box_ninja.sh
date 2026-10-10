#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env, from the repo root: reconfigure (lit enabled) and build,
# showing compiler errors.   cir_src/scripts/box_ninja.sh [ninja args]
lit_arg=
[ -f /lit/lit.py ] && lit_arg=-DEDG_CIR_LIT=/lit/lit.py
(cd cir_src && cmake --preset linux-cir-relwithdebinfo $lit_arg > /dev/null)
ninja -C build/cir -k 0 "$@" 2>&1 | grep -v "^\[" | head -${LINES_MAX:-100}
