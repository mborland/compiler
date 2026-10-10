#!/bin/bash
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
B=build/cir-spike
$B/spike -triple aarch64-unknown-linux-gnu -o $B/a64.o -print-cir-post 2>&1 | tail -5; echo "aarch64 rc=${PIPESTATUS[0]}"
