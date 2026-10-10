#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Inside cir-env: run lit directly (arguments: extra lit args / filters).
cd build/cir/test/lit && python3 /lit/lit.py -v "$@" . 2>&1 | tail -${LINES_MAX:-80}
