# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
add_library(edg_cir_rt STATIC edg_cir_rt.cpp)
target_link_libraries(edg_cir_rt PRIVATE edg_cir_options)
set_target_properties(edg_cir_rt PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
