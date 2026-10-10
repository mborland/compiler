# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The front end with the ClangIR (CIR) back end.  This directory is added by
# src/CMakeLists.txt only when EDG_BUILD_CIR is ON.
#
# Options (set in src/CMakeLists.txt):
#   EDG_CIR_LINK_LIBRARY  ON: link libedg_cir.so (the cir_src library) and
#                             define EDG_CIR_LIBRARY_LINKED=1 so the ECIL
#                             sink can hand streams to it in-process.
#                         OFF: writer-only front end; needs no LLVM.
#   EDG_CIR_DIR           Where EdgCirConfig.cmake lives (build/cir by
#                         default, or $ENV{EDG_CIR_DIR}).

# List of all historic files for the front end.
set(ALL_C_FILES "${CORE_FRONT_END_SOURCE_FILES}")

# The ECIL writer and the CIR back end shim.  They are added by the CIR
# project and are absent from older trees.
foreach(cir_file ecil_write.c ecil_type.c ecil_const.c ecil_code.c
                  ecil_sink.c cir_be.c)
  if(EXISTS "${PROJECT_SOURCE_DIR}/${cir_file}")
    list(APPEND ALL_C_FILES "${PROJECT_SOURCE_DIR}/${cir_file}")
  endif()
endforeach()

# Load the specified macro configuration for cpfe-cir.
macro_configuration_to_compile_definitions("cpfe-cir")

# compile .c files as C++.
set_source_files_properties(${ALL_C_FILES} PROPERTIES LANGUAGE CXX)

add_executable(cpfe-cir ${ALL_C_FILES} ${TXT_FILES})
add_dependencies(cpfe-cir error-info)

# Expose softfloat.
add_softfloat_to_target("cpfe-cir")

# The only extra header directory: the public contract headers of cir_src
# (edg_cir/ecil_format.h and edg_cir/edg_cir.h).  Nothing else of cir_src may
# be visible to the front end.
target_include_directories(cpfe-cir PRIVATE
                           ${CMAKE_SOURCE_DIR}/cir_src/include)

if($CACHE{EDG_CIR_LINK_LIBRARY})
  find_package(EdgCir CONFIG REQUIRED PATHS "${EDG_CIR_DIR}" NO_DEFAULT_PATH)
  target_link_libraries(cpfe-cir EdgCir::edg_cir)
  target_compile_definitions(cpfe-cir PRIVATE EDG_CIR_LIBRARY_LINKED=1)
  # Changes to the shared library's contents do not relink the front end.
  set_target_properties(cpfe-cir PROPERTIES LINK_DEPENDS_NO_SHARED ON)
  # Look next to the binary first (edgy copies libedg_cir.so there), then in
  # the library build directory.
  get_filename_component(edg_cir_lib_dir "${EDG_CIR_DIR}/lib" ABSOLUTE)
  set_target_properties(cpfe-cir PROPERTIES
                        BUILD_WITH_INSTALL_RPATH OFF
                        BUILD_RPATH "$ORIGIN;${edg_cir_lib_dir}"
                        INSTALL_RPATH "$ORIGIN")
  message(STATUS "cpfe-cir: linking EdgCir from ${EDG_CIR_DIR}")
else()
  message(STATUS "cpfe-cir: writer-only (EDG_CIR_LINK_LIBRARY is OFF)")
endif()
