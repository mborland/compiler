# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
add_executable(ecil-dump ecil_dump.cpp)
target_link_libraries(ecil-dump PRIVATE ecil_format)
