# Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
# Exceptions.
# See https://edgcpp.org/LICENSE.txt for license information.
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

# The following is code for generating supporting files for the CMake
# build.

string(JOIN "\n" static_env
    "export CPFE=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpfe\""
    "export CPFE_CP=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpfe-cp\""
    "export ECCP_LIBDIR=\"${CMAKE_LIBRARY_OUTPUT_DIRECTORY}\""
    "export EDG_MUNCH_PATH=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/edg_munch\""
    "export EDG_DECODE_PATH=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/edg_decode\""
    "export EDG_PRELINK_PATH=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/edg_prelink\""
    "export ECCP=\"${CMAKE_SOURCE_DIR}/util/eccp.sh\""
    "export EDG_BASE=\"$CACHE{EDG_BASE}\"")

string(JOIN "\n" lazy_static_env
  "export CPFE=\"\${CPFE:-${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpfe}\""
  "export CPFE_CP=\"\${CPFE_CP:-${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpfe-cp}\""
  "export ECCP_LIBDIR=\"\${ECCP_LIBDIR:-${CMAKE_LIBRARY_OUTPUT_DIRECTORY}}\""
  "export EDG_MUNCH_PATH=\"\${EDG_MUNCH_PATH:-${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/edg_munch}\""
  "export EDG_DECODE_PATH=\"\${EDG_DECODE_PATH:-${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/edg_decode}\""
  "export EDG_PRELINK_PATH=\"\${EDG_PRELINK_PATH:-${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/edg_prelink}\""
  "export ECCP=\"\${ECCP:-${CMAKE_SOURCE_DIR}/util/eccp.sh}\""
  "export EDG_BASE=\"\${EDG_BASE:-$CACHE{EDG_BASE}}\"")

# When cpfe-cir is built, also export the paths the edg_x86_64_cir test config
# needs: the front end itself, the cir_src library (copied next to the front
# end by edgy; it is the SONAME file, since that is the name the front end
# asks for), ecil-dump, and the runtime shim directory.
if($CACHE{EDG_BUILD_CIR})
  set(cir_static_env
      "export CPFE_CIR=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpfe-cir\""
      "export EDG_CIR_LIB=\"$CACHE{EDG_CIR_DIR}/lib/libedg_cir.so.0\""
      "export ECIL_DUMP=\"$CACHE{EDG_CIR_DIR}/tools/ecil-dump/ecil-dump\""
      "export EDG_CIR_RT_LIBDIR=\"$CACHE{EDG_CIR_DIR}/lib\""
      "export EDG_CIR_TOOL=\"$CACHE{EDG_CIR_DIR}/bin/edg-cir\"")
  set(cir_lazy_static_env
      "export CPFE_CIR=\"\${CPFE_CIR:-${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/cpfe-cir}\""
      "export EDG_CIR_LIB=\"\${EDG_CIR_LIB:-$CACHE{EDG_CIR_DIR}/lib/libedg_cir.so.0}\""
      "export ECIL_DUMP=\"\${ECIL_DUMP:-$CACHE{EDG_CIR_DIR}/tools/ecil-dump/ecil-dump}\""
      "export EDG_CIR_RT_LIBDIR=\"\${EDG_CIR_RT_LIBDIR:-$CACHE{EDG_CIR_DIR}/lib}\""
      "export EDG_CIR_TOOL=\"\${EDG_CIR_TOOL:-$CACHE{EDG_CIR_DIR}/bin/edg-cir}\"")
  string(JOIN "\n" static_env "${static_env}" ${cir_static_env})
  string(JOIN "\n" lazy_static_env "${lazy_static_env}" ${cir_lazy_static_env})
endif()

FILE(WRITE ${CMAKE_BINARY_DIR}/environment.sh "${static_env}\n")
FILE(WRITE ${CMAKE_BINARY_DIR}/.envrc
     "${static_env}\nsource_env \"${CMAKE_SOURCE_DIR}/.envrc\"\n")
FILE(WRITE ${CMAKE_BINARY_DIR}/lazy_environment.sh "${lazy_static_env}\n")

function(add_edg_wrapper_script script_name working_dir delegate_script_path)
  # Write a wrapper that sets environment variables for EDG scripts to
  # look at what we've built
  FILE(WRITE ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${script_name}
    "#!/bin/sh\n"
    ". \"${CMAKE_BINARY_DIR}/environment.sh\"\n"
    "cd \"${working_dir}\"\n"
    "exec \"${delegate_script_path}\" \"$@\"\n")

  # File permission 755
  file(CHMOD ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${script_name}
       PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                   GROUP_READ GROUP_EXECUTE
                   WORLD_READ WORLD_EXECUTE)
endfunction()

