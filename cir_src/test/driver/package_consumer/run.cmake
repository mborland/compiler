# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
file(REMOVE_RECURSE "${BIN}")
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${SRC}" -B "${BIN}" -G Ninja
  -DEDG_CIR_DIR=${EDG_CIR_DIR} -DCMAKE_CXX_STANDARD=14
  "-DCMAKE_CXX_FLAGS=-pedantic -Wconversion -Wsign-conversion -Werror"
  -DCMAKE_CXX_COMPILER=${CXX} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "configure of the package consumer failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${BIN}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "build of the package consumer failed")
endif()
execute_process(COMMAND "${BIN}/consumer" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "the package consumer failed")
endif()
