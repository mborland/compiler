# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Tests of the C API and the edg-cir tool (need the full library).
add_executable(capi_tests capi_tests.cpp)
target_link_libraries(capi_tests PRIVATE edg_cir_options ecil_format edg_cir)
set_target_properties(capi_tests PROPERTIES
  BUILD_RPATH "${CMAKE_BINARY_DIR}/lib")
if(EDG_CIR_TEST_HOOKS)
  target_compile_definitions(capi_tests PRIVATE EDG_CIR_TEST_HOOKS=1)
else()
  target_compile_definitions(capi_tests PRIVATE EDG_CIR_TEST_HOOKS=0)
endif()
add_test(NAME capi_tests
  COMMAND capi_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/../writer/golden/empty.hex"
    "${CMAKE_CURRENT_SOURCE_DIR}/../writer/golden/abort.hex"
    $<TARGET_FILE:edg-cir>)

set_tests_properties(capi_tests PROPERTIES TIMEOUT 300 ENVIRONMENT
  "LSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/lsan.supp")

# Every bad_* format fixture must be rejected by edg-cir as FORMAT or VERSION.
add_test(NAME bad_fixtures_edg_cir
  COMMAND "${CMAKE_COMMAND}"
    -DECIL_ASM=$<TARGET_FILE:ecil-asm> -DEDG_CIR=$<TARGET_FILE:edg-cir>
    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/../format/fixtures
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/bad_fixtures_edg_cir.cmake")

# A consumer built only from the exported package, to prove the package (an
# ASan-built library cannot be loaded by a non-ASan executable, so the asan
# preset skips it).
if(NOT CMAKE_CXX_FLAGS MATCHES "sanitize")
add_test(NAME package_consumer
  COMMAND "${CMAKE_COMMAND}"
    -DSRC=${CMAKE_CURRENT_SOURCE_DIR}/package_consumer
    -DBIN=${CMAKE_CURRENT_BINARY_DIR}/package_consumer
    -DEDG_CIR_DIR=${CMAKE_BINARY_DIR}
    -DCXX=${CMAKE_CXX_COMPILER}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/package_consumer/run.cmake")
endif()

# Exported symbols: only edg_cir_*.
add_test(NAME exported_symbols
  COMMAND "${CMAKE_COMMAND}"
    -DLIB=$<TARGET_FILE:edg_cir>
    -P "${CMAKE_CURRENT_SOURCE_DIR}/check_exports.cmake")

# lit tests (need lit.py, e.g. -DEDG_CIR_LIT=<llvm-project>/llvm/utils/lit/lit.py
# or a pip-installed lit, and FileCheck/not in the toolchain prefix).
set(EDG_CIR_LIT "" CACHE FILEPATH "Path of lit.py (enables the lit tests)")
if(EDG_CIR_LIT AND EXISTS "${EDG_LLVM_CIR_PREFIX}/bin/FileCheck")
  configure_file("${CMAKE_SOURCE_DIR}/test/lit/lit.site.cfg.py.in"
    "${CMAKE_BINARY_DIR}/test/lit/lit.site.cfg.py" @ONLY)
  add_test(NAME lit
    COMMAND python3 "${EDG_CIR_LIT}" -v "${CMAKE_BINARY_DIR}/test/lit")
endif()

# Benchmark: per-frame consume cost (see consume_bench.cpp); not a test.
add_executable(consume_bench consume_bench.cpp)
target_link_libraries(consume_bench PRIVATE edg_cir_options edg_cir)
set_target_properties(consume_bench PROPERTIES
  BUILD_RPATH "${CMAKE_BINARY_DIR}/lib")
