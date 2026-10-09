# ClangIR toolchain spike (see README.md).  Standalone: it does not use
# anything from src/ and is not part of the front-end build.
#
#   cmake -S cir_src/spike -B build/cir-spike -G Ninja \
#         -DEDG_LLVM_CIR_PREFIX=/edg/llvm-cir
#
# EDG_LLVM_CIR_PREFIX defaults to the environment variable of the same name
# (set in the cir-env image).
cmake_minimum_required(VERSION 3.20)
project(edg_cir_spike CXX)

if(NOT EDG_LLVM_CIR_PREFIX)
  set(EDG_LLVM_CIR_PREFIX "$ENV{EDG_LLVM_CIR_PREFIX}")
endif()
if(NOT EDG_LLVM_CIR_PREFIX)
  message(FATAL_ERROR
    "Pass -DEDG_LLVM_CIR_PREFIX=<installed llvm+clang+mlir prefix>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# The three CMake packages of the installed toolchain.  ClangConfig.cmake
# itself finds LLVM; set all three dirs so a stray system LLVM is never used.
set(LLVM_DIR  "${EDG_LLVM_CIR_PREFIX}/lib/cmake/llvm"  CACHE PATH "" FORCE)
set(MLIR_DIR  "${EDG_LLVM_CIR_PREFIX}/lib/cmake/mlir"  CACHE PATH "" FORCE)
set(Clang_DIR "${EDG_LLVM_CIR_PREFIX}/lib/cmake/clang" CACHE PATH "" FORCE)
find_package(LLVM  CONFIG REQUIRED NO_DEFAULT_PATH)
find_package(MLIR  CONFIG REQUIRED NO_DEFAULT_PATH)
find_package(Clang CONFIG REQUIRED NO_DEFAULT_PATH)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

separate_arguments(llvm_definitions NATIVE_COMMAND "${LLVM_DEFINITIONS}")
separate_arguments(mlir_definitions NATIVE_COMMAND "${MLIR_DEFINITIONS}")

add_executable(spike spike.cpp)
target_include_directories(spike SYSTEM PRIVATE
  ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(spike PRIVATE
  ${llvm_definitions} ${mlir_definitions})
target_compile_options(spike PRIVATE -Wall -Wno-unused-parameter)

# LLVM and MLIR are built -fno-rtti -fno-exceptions; the consumer must match.
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(spike PRIVATE -fno-rtti)
endif()
if(NOT LLVM_ENABLE_EH)
  target_compile_options(spike PRIVATE -fno-exceptions)
endif()
# The toolchain has assertions on; keep inline asserts in its headers on too.
if(LLVM_ENABLE_ASSERTIONS)
  target_compile_options(spike PRIVATE -UNDEBUG)
endif()

# Back-end components: InitializeAll*() references every enabled target
# (the toolchain is built X86;AArch64), so both must be linked.
llvm_map_components_to_libnames(spike_llvm_libs
  core support targetparser analysis passes codegen asmprinter mc
  x86codegen x86asmparser x86desc x86info
  aarch64codegen aarch64asmparser aarch64desc aarch64info)

# The exact set cir-translate links (clang/tools/cir-translate/CMakeLists.txt).
# clangCIR comes in transitively through clangCIRLoweringCommon anyway; it is
# listed because cir::setMLIRDataLayout lives there.
target_link_libraries(spike PRIVATE
  clangCIR
  clangCIRLoweringDirectToLLVM
  clangCIRLoweringCommon
  CIRRegisterAllDialects
  MLIRCIR
  MLIRCIRTransforms
  MLIRAnalysis
  MLIRBuiltinToLLVMIRTranslation
  MLIRDialect
  MLIRDLTIDialect
  MLIRFuncDialect
  MLIRIR
  MLIRLLVMDialect
  MLIRLLVMToLLVMIRTranslation
  MLIROpenMPToLLVMIRTranslation
  MLIRTargetLLVMIRExport
  MLIRParser
  MLIRPass
  MLIRSupport
  MLIRTransforms
  MLIRTransformUtils
  ${spike_llvm_libs})

# Print what the link actually resolved to, for the research notes.
add_custom_target(spike_link_info
  COMMAND ${CMAKE_COMMAND} -E echo
          "link line: ninja -C ${CMAKE_BINARY_DIR} -t commands spike | tail -1"
  VERBATIM)
