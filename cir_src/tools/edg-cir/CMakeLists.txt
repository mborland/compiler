# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
add_executable(edg-cir edg_cir_main.cpp)
target_link_libraries(edg-cir PRIVATE edg_cir_options edg_cir)
set_target_properties(edg-cir PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  BUILD_RPATH "\$ORIGIN/../lib" INSTALL_RPATH "\$ORIGIN/../lib")
