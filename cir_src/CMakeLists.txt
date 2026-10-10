# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.  See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
#
# Standalone CMake project of the CIR back-end library (libedg_cir).  It never
# looks at src/; the front end consumes only include/edg_cir/*.h and the
# library built here.
#
#   cmake --preset linux-cir-relwithdebinfo        (on the box, cir-env image)
#   cmake -S cir_src -B build/cir-fmt -DEDG_CIR_FORMAT_ONLY=ON   (anywhere)
cmake_minimum_required(VERSION 3.20)
project(edg_cir LANGUAGES CXX VERSION 0.1.0)

option(EDG_CIR_FORMAT_ONLY
  "Build only lib/format, ecil-dump and ecil-asm (needs no LLVM)" OFF)
option(EDG_CIR_WERROR "Compile cir_src code with -Werror" ON)
option(EDG_CIR_BUILD_TESTS "Build and register the tests" ON)
option(EDG_CIR_TEST_HOOKS
  "Compile the EDG_CIR_TEST_CRASH environment hook into the library" ON)
set(EDG_LLVM_CIR_PREFIX "$ENV{EDG_LLVM_CIR_PREFIX}" CACHE PATH
  "Prefix of the installed llvm+clang+mlir toolchain (see LLVM_REVISION)")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The library is optimized with debug info even for debug front ends.
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g")

if(EDG_CIR_FORMAT_ONLY)
  set(EDG_CIR_FORMAT_ONLY_INT 1)
else()
  set(EDG_CIR_FORMAT_ONLY_INT 0)
endif()
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/LLVM_REVISION" EDG_CIR_LLVM_REVISION
  LIMIT_COUNT 1)
configure_file(config/cir_config_generated.h.in
  "${CMAKE_BINARY_DIR}/config/cir_config_generated.h" @ONLY)

# Warnings and the LLVM-compatible language model, applied to our targets.
add_library(edg_cir_options INTERFACE)
target_compile_options(edg_cir_options INTERFACE
  -fno-rtti -fno-exceptions -Wall -Wextra -Wshadow
  -Wno-unused-parameter)
if(EDG_CIR_WERROR)
  target_compile_options(edg_cir_options INTERFACE -Werror)
endif()
target_include_directories(edg_cir_options INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/config"
  "${CMAKE_BINARY_DIR}/config")

add_subdirectory(lib/format)
add_subdirectory(tools/ecil-dump)
add_subdirectory(tools/ecil-asm)

if(NOT EDG_CIR_FORMAT_ONLY)
  find_package(LLVM  CONFIG REQUIRED PATHS "${EDG_LLVM_CIR_PREFIX}/lib/cmake/llvm"
               NO_DEFAULT_PATH)
  find_package(MLIR  CONFIG REQUIRED PATHS "${EDG_LLVM_CIR_PREFIX}/lib/cmake/mlir"
               NO_DEFAULT_PATH)
  find_package(Clang CONFIG REQUIRED PATHS "${EDG_LLVM_CIR_PREFIX}/lib/cmake/clang"
               NO_DEFAULT_PATH)
  message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} at ${LLVM_DIR}")
  add_subdirectory(lib/driver)
  add_subdirectory(runtime)
  add_subdirectory(tools/edg-cir)
  include(cmake/EdgCirPackaging.cmake)
endif()

if(EDG_CIR_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test/format)
  if(NOT EDG_CIR_FORMAT_ONLY)
    add_subdirectory(test/driver)
  endif()
endif()
