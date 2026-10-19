# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# One object file per source: the archive links a member only when a program
# refers to a symbol it defines (debug_level.cpp is a member of its own).
add_library(edg_cir_rt STATIC edg_cir_rt.cpp debug_level.cpp)
target_link_libraries(edg_cir_rt PRIVATE edg_cir_options)
# The runtime is linked into programs that are not built with a sanitizer
# (the asan preset instruments the library under test, not the programs): an
# instrumented global would need the sanitizer's runtime at link time.
target_compile_options(edg_cir_rt PRIVATE -fno-sanitize=all)
set_target_properties(edg_cir_rt PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
