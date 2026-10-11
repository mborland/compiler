# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Run EVERY bad_* format fixture through edg-cir (not only ecil-dump): the
# stream must be rejected with a FORMAT (2) or VERSION (3) status, never an
# INTERNAL error (9) or a crash, so that the emitter never has to fall back on
# its own internal checks for what the validator should have caught.
cmake_policy(SET CMP0057 NEW)
file(GLOB fixtures "${FIXTURES}/bad_*.txt")
file(MAKE_DIRECTORY "${WORK}/bad")
set(count 0)
foreach(f ${fixtures})
  get_filename_component(name "${f}" NAME_WE)
  set(bin "${WORK}/bad/${name}.ecil")
  execute_process(COMMAND "${ECIL_ASM}" -o "${bin}" "${f}" RESULT_VARIABLE rc
    ERROR_VARIABLE err)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "ecil-asm ${name}: ${err}")
  endif()
  execute_process(COMMAND "${EDG_CIR}" --emit=cir -o /dev/null "${bin}"
    RESULT_VARIABLE rc OUTPUT_QUIET ERROR_VARIABLE err)
  # The C++ (bad_v1_*) fixtures carry C++ records the emitter does not
  # implement yet (status 4, "unsupported"); it meets them before the stream
  # reaches the record that makes the fixture bad.  Anything but a clean
  # rejection (INTERNAL, a crash) fails.
  set(want 2 3)
  if(name MATCHES "^bad_v1_")
    list(APPEND want 4)
  endif()
  if(NOT rc IN_LIST want)
    message(FATAL_ERROR "edg-cir ${name}: exit status ${rc} (want one of ${want}): ${err}")
  endif()
  math(EXPR count "${count} + 1")
endforeach()
message(STATUS "${count} bad fixtures rejected by edg-cir")
