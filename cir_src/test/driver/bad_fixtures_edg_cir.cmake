# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Run EVERY bad_* format fixture through edg-cir (not only ecil-dump): the
# stream must be rejected with a FORMAT (2) or VERSION (3) status, never an
# INTERNAL error (9) or a crash, so that the emitter never has to fall back on
# its own internal checks for what the validator should have caught.
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
  if(NOT (rc EQUAL 2 OR rc EQUAL 3))
    message(FATAL_ERROR "edg-cir ${name}: exit status ${rc} (want 2 or 3): ${err}")
  endif()
  math(EXPR count "${count} + 1")
endforeach()
message(STATUS "${count} bad fixtures rejected by edg-cir")
