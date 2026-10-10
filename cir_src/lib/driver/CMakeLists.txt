# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# The driver: C API, session, module construction, worker pipeline.
# Libraries: the exact set of clang/tools/cir-translate plus the X86 back end.
llvm_map_components_to_libnames(edg_cir_llvm_libs
  core support targetparser analysis passes codegen asmprinter mc
  bitwriter ipo scalaropts instcombine transformutils
  x86codegen x86desc x86info)

set(edg_cir_mlir_clang_libs
  clangCIR clangCIRLoweringDirectToLLVM clangCIRLoweringCommon
  CIRRegisterAllDialects MLIRCIR MLIRCIRTransforms
  MLIRAnalysis MLIRBuiltinToLLVMIRTranslation MLIRDialect MLIRDLTIDialect
  MLIRFuncDialect MLIRIR MLIRLLVMDialect MLIRLLVMToLLVMIRTranslation
  MLIROpenMPToLLVMIRTranslation MLIRTargetLLVMIRExport MLIRParser MLIRPass
  MLIRSupport MLIRTransforms MLIRTransformUtils)

# Our objects (also reusable by the static bundle).
add_library(edg_cir_objects OBJECT
  capi.cpp
  cir_module.cpp
  pipeline.cpp
  session.cpp
  ../cirgen/expr.cpp
  ../cirgen/function.cpp
  ../cirgen/globals.cpp
  ../cirgen/types.cpp)
target_include_directories(edg_cir_objects PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../cirgen")
target_link_libraries(edg_cir_objects PUBLIC edg_cir_options ecil_format)
target_include_directories(edg_cir_objects SYSTEM PRIVATE
  ${LLVM_INCLUDE_DIRS} ${MLIR_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(edg_cir_objects PRIVATE
  EDG_CIR_BUILDING_LIBRARY=1)
if(EDG_CIR_TEST_HOOKS)
  target_compile_definitions(edg_cir_objects PRIVATE EDG_CIR_TEST_HOOKS=1)
else()
  target_compile_definitions(edg_cir_objects PRIVATE EDG_CIR_TEST_HOOKS=0)
endif()
separate_arguments(edg_cir_llvm_defs NATIVE_COMMAND "${LLVM_DEFINITIONS}")
separate_arguments(edg_cir_mlir_defs NATIVE_COMMAND "${MLIR_DEFINITIONS}")
target_compile_definitions(edg_cir_objects PRIVATE
  ${edg_cir_llvm_defs} ${edg_cir_mlir_defs})
set_target_properties(edg_cir_objects PROPERTIES
  CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON)
if(LLVM_ENABLE_ASSERTIONS)
  target_compile_options(edg_cir_objects PRIVATE -UNDEBUG)
endif()

# The shared library: static LLVM/MLIR/clang inside, only edg_cir_* exported.
add_library(edg_cir SHARED $<TARGET_OBJECTS:edg_cir_objects>)
target_link_libraries(edg_cir PRIVATE
  ecil_format
  ${edg_cir_mlir_clang_libs} ${edg_cir_llvm_libs}
  -Wl,--exclude-libs,ALL
  -Wl,-Bsymbolic-functions
  -Wl,--version-script=${CMAKE_SOURCE_DIR}/cmake/edg_cir.map
  -Wl,--gc-sections
  -Wl,--no-undefined)
set_target_properties(edg_cir PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  VERSION ${PROJECT_VERSION} SOVERSION 0
  LINK_DEPENDS "${CMAKE_SOURCE_DIR}/cmake/edg_cir.map"
  CXX_VISIBILITY_PRESET hidden BUILD_RPATH "\$ORIGIN"
  INSTALL_RPATH "\$ORIGIN")

# Static bundle (optional in M1): our objects only; consumers link the LLVM
# set themselves (a merged archive is an M6 item).
add_library(edg_cir_static STATIC $<TARGET_OBJECTS:edg_cir_objects>)
target_link_libraries(edg_cir_static PUBLIC ecil_format)
set_target_properties(edg_cir_static PROPERTIES
  OUTPUT_NAME edg_cir_nolinked
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
