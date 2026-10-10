# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
cmake_minimum_required(VERSION 3.20)
project(edg_cir_package_consumer CXX)
find_package(EdgCir CONFIG REQUIRED PATHS "${EDG_CIR_DIR}" NO_DEFAULT_PATH)
add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE EdgCir::edg_cir)
