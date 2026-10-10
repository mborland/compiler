#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: run ctest in build/cir (arguments are passed to ctest).
cd build/cir && ctest "$@" 2>&1 | tail -${LINES_MAX:-40}
