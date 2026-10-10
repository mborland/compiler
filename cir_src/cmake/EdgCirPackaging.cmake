# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
# Install rules and the hand-written EdgCirConfig.cmake (build tree and
# install tree flavors).  The front end does
#   find_package(EdgCir CONFIG PATHS ${EDG_CIR_DIR})
# and links EdgCir::edg_cir; LLVM's CMake never enters the front-end
# configure.
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS edg_cir LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS edg_cir_rt ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(TARGETS edg-cir ecil-dump ecil-asm RUNTIME
        DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY "${CMAKE_SOURCE_DIR}/include/edg_cir"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.h")

write_basic_package_version_file(
  "${CMAKE_BINARY_DIR}/EdgCirConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMinorVersion)
configure_file("${CMAKE_SOURCE_DIR}/cmake/EdgCirConfig-build.cmake.in"
  "${CMAKE_BINARY_DIR}/EdgCirConfig.cmake" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/cmake/EdgCirConfig-install.cmake.in"
  "${CMAKE_BINARY_DIR}/install-config/EdgCirConfig.cmake" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/install-config/EdgCirConfig.cmake"
              "${CMAKE_BINARY_DIR}/EdgCirConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/EdgCir)
