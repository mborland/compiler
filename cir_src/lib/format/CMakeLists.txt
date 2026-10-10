# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
add_library(ecil_format STATIC
  ecil_decode.cpp
  ecil_schema.cpp
  ecil_refs.cpp
  ecil_stream.cpp
  ecil_text.cpp)
target_include_directories(ecil_format PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ecil_format PUBLIC edg_cir_options)
