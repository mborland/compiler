# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Round trip every good fixture through the command-line tools:
#   ecil-asm x.txt -> a.bin ; ecil-dump a.bin -> d.txt ; ecil-asm d.txt -> b.bin
# and require a.bin == b.bin.  Bad fixtures must make ecil-dump fail.
file(GLOB fixtures "${FIXTURES}/*.txt")
file(MAKE_DIRECTORY "${WORK}/cli")
foreach(f ${fixtures})
  get_filename_component(name "${f}" NAME_WE)
  set(a "${WORK}/cli/${name}.a.bin")
  set(d "${WORK}/cli/${name}.d.txt")
  set(b "${WORK}/cli/${name}.b.bin")
  execute_process(COMMAND "${ECIL_ASM}" -o "${a}" "${f}" RESULT_VARIABLE rc
    ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ecil-asm ${name}: ${err}")
  endif()
  set(dump_args)
  if(name MATCHES "^nocheck_")
    set(dump_args --no-check)
  endif()
  execute_process(COMMAND "${ECIL_DUMP}" ${dump_args} -o "${d}" "${a}"
    RESULT_VARIABLE rc
    ERROR_VARIABLE err)
  if(name MATCHES "^bad_")
    if(rc EQUAL 0)
      message(FATAL_ERROR "ecil-dump accepted ${name}")
    endif()
  else()
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "ecil-dump ${name}: ${err}")
    endif()
    execute_process(COMMAND "${ECIL_ASM}" -o "${b}" "${d}" RESULT_VARIABLE rc
      ERROR_VARIABLE err)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "ecil-asm (dump of ${name}): ${err}")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" -E compare_files "${a}" "${b}"
      RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
      message(FATAL_ERROR "round trip differs for ${name}")
    endif()
  endif()
endforeach()
message(STATUS "cli round trip ok")
