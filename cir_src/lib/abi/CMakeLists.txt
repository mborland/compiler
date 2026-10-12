# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# The ABI part of the CIR library: Itanium vtable layouts, VTTs, special names,
# RTTI layouts and the class-fact builder.  LLVM-free C++17.  Part of the
# cir_src build (target edg_abi, linked by the code generator and the tools);
# it can still be built alone:
#
#   cmake -S cir_src/lib/abi -B build/cir-abi -G Ninja
#   ninja -C build/cir-abi && ctest --test-dir build/cir-abi
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  cmake_minimum_required(VERSION 3.20)
  project(edg_abi CXX)
  set(CMAKE_CXX_STANDARD 17)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
  endif()
  set(EDG_ABI_STANDALONE ON)
  set(EDG_CIR_BUILD_TESTS ON)
endif()

add_library(edg_abi STATIC
  abi_facts.cpp abi_vtable.cpp abi_vtt.cpp abi_print.cpp abi_check.cpp
  abi_text.cpp abi_builder.cpp abi_validate.cpp abi_names.cpp abi_rtti.cpp)
target_include_directories(edg_abi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(EDG_ABI_STANDALONE)
  target_compile_options(edg_abi PRIVATE -Wall -Wextra)
else()
  target_link_libraries(edg_abi PRIVATE edg_cir_options)
endif()

add_executable(abi_dump tools/abi_dump.cpp)
target_link_libraries(abi_dump PRIVATE edg_abi)

if(EDG_CIR_BUILD_TESTS)
  enable_testing()
  add_executable(abi_tests test/abi_tests.cpp)
  target_link_libraries(abi_tests PRIVATE edg_abi)
  add_test(NAME abi_tests COMMAND abi_tests)
  add_test(NAME abi_deep_chain_small_stack
    COMMAND sh -c "ulimit -s 256; $<TARGET_FILE:abi_tests> deep")
endif()
