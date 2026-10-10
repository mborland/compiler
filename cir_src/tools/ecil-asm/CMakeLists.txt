# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
add_executable(ecil-asm ecil_asm.cpp)
target_link_libraries(ecil-asm PRIVATE ecil_format)
