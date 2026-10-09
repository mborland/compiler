# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# Holds only the installed LLVM/Clang/MLIR (ClangIR enabled) prefix used by
# the EDG CIR back end.  The tree is produced by prebuild.py (which runs
# dev_tools/bin/edg-build-llvm-cir inside edgcpp/dev-env with bind mounts, so
# the build is incremental across edits to this file) and copied in here.
# The cir-env image COPYs /edg/llvm-cir from this image.

FROM fedora:43

COPY llvm-cir /edg/llvm-cir
