cmake_minimum_required(VERSION 3.20)
project(readability_pp VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set(READABILITY_IS_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(READABILITY_IS_TOP_LEVEL ON)
endif()

option(READABILITY_BUILD_TESTS "Build the Readability++ parity test suite" ${READABILITY_IS_TOP_LEVEL})
option(READABILITY_BUILD_EXAMPLES "Build the Readability++ examples" ${READABILITY_IS_TOP_LEVEL})
option(READABILITY_ENABLE_SANITIZERS "Enable address and undefined-behavior sanitizers" ${READABILITY_BUILD_TESTS})

add_library(readability
  src/dom.cpp
  src/lightweight_dom.cpp
  src/readerable.cpp
  src/readability.cpp
)
add_library(readability::readability ALIAS readability)
target_compile_features(readability PUBLIC cxx_std_23)
target_include_directories(readability PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

set(READABILITY_WARNINGS -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow)
target_compile_options(readability PRIVATE ${READABILITY_WARNINGS})

if(READABILITY_BUILD_TESTS)
  enable_testing()
  add_executable(readability_tests tests/tests.cpp)
  target_link_libraries(readability_tests PRIVATE readability::readability)
  target_compile_options(readability_tests PRIVATE ${READABILITY_WARNINGS})
  target_compile_definitions(readability_tests PRIVATE
    READABILITY_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  add_test(NAME readability-parity COMMAND readability_tests)
  set_tests_properties(readability-parity PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
endif()

if(READABILITY_BUILD_EXAMPLES)
  add_executable(readability_extract_example examples/extract.cpp)
  target_link_libraries(readability_extract_example PRIVATE readability::readability)
  target_compile_options(readability_extract_example PRIVATE ${READABILITY_WARNINGS})
endif()

if(READABILITY_ENABLE_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(readability PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(readability INTERFACE -fsanitize=address,undefined)
  if(TARGET readability_tests)
    target_compile_options(readability_tests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
  if(TARGET readability_extract_example)
    target_compile_options(readability_extract_example PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
  endif()
endif()

install(TARGETS readability EXPORT readability-targets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT readability-targets
  FILE readability-targets.cmake
  NAMESPACE readability::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/readability)
configure_package_config_file(
  cmake/readability-config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/readability-config.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/readability)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/readability-config-version.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/readability-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/readability-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/readability)
