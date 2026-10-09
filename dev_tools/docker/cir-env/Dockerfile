# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

FROM edgcpp/dev-env:latest

# Install the ClangIR-enabled LLVM toolchain.  Its bin directory is
# deliberately NOT on PATH, so the image's regular clang/lld (and every
# existing EDG test) keep resolving to the Fedora toolchain.
COPY --from=edgcpp/llvm-cir:latest /edg/llvm-cir /edg/llvm-cir

# Tell cir_src's CMake (EdgCirConfig.cmake) where LLVM/MLIR/Clang live.
ENV EDG_LLVM_CIR_PREFIX="/edg/llvm-cir"
