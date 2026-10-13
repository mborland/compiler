# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
add_executable(format_tests format_tests.cpp)
target_link_libraries(format_tests PRIVATE ecil_format)
add_executable(abi_decode_tests abi_decode_tests.cpp)
target_link_libraries(abi_decode_tests PRIVATE ecil_format)
add_test(NAME abi_decode_tests COMMAND abi_decode_tests)
file(GLOB fixtures CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/*.txt")
add_test(NAME format_tests
  COMMAND format_tests "${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
          "${CMAKE_CURRENT_SOURCE_DIR}/../writer/golden")
set(writer_golden "${CMAKE_CURRENT_SOURCE_DIR}/../writer/golden")
add_test(NAME format_cli_roundtrip
  COMMAND "${CMAKE_COMMAND}"
    -DECIL_DUMP=$<TARGET_FILE:ecil-dump> -DECIL_ASM=$<TARGET_FILE:ecil-asm>
    -DFIXTURES=${CMAKE_CURRENT_SOURCE_DIR}/fixtures
    -DWORK=${CMAKE_CURRENT_BINARY_DIR}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/cli_roundtrip.cmake")
