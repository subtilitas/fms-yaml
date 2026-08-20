cmake_minimum_required(VERSION 3.20)
project(fms_yaml VERSION 0.2.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FMS_BUILD_TESTS    "Build unit tests"                        ON)
option(FMS_BUILD_EXAMPLES "Build the example applications"          ON)
option(FMS_BUILD_CONSOLE  "Build the std::cin/std::cout port"       ON)
option(FMS_FETCH_DEPS     "Download dependencies with FetchContent" ON)

# Quality-gate options.  Off by default so a plain build stays a plain build;
# CI turns them on one at a time, because coverage counters and sanitizer
# shadow memory measure different things and interfere when combined.
option(FMS_ENABLE_COVERAGE   "Instrument the fms targets for gcov"       OFF)
option(FMS_ENABLE_SANITIZERS "Build the fms targets with ASan + UBSan"   OFF)
option(FMS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"        OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------------------
# Helper: turn C++ exceptions off for a target.
#
# The whole project is compiled -fno-exceptions except for `fms_config`, which
# is the only translation unit that talks to yaml-cpp (yaml-cpp signals errors
# by throwing).  `fms_config` catches everything at its own boundary and never
# lets an exception escape into the rest of the system.
# ---------------------------------------------------------------------------
function(fms_disable_exceptions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /EHs-c-)
    target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
  else()
    target_compile_options(${target} PRIVATE -fno-exceptions)
  endif()
endfunction()

function(fms_enable_exceptions target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /EHsc)
  else()
    target_compile_options(${target} PRIVATE -fexceptions)
  endif()
endfunction()

function(fms_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
    if(FMS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
    if(FMS_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Helper: coverage and sanitizer instrumentation.
#
# Applied per target rather than with add_compile_options, so the FetchContent
# dependencies built in this tree stay uninstrumented: ETL, yaml-cpp and doctest
# are not our code, and counting their lines would only dilute the numbers.
#
# The link flags are PUBLIC: an instrumented static library has to pull libgcov
# (and the sanitizer runtimes) into whatever links it.
# ---------------------------------------------------------------------------
function(fms_instrument target)
  if(FMS_ENABLE_COVERAGE)
    if(MSVC)
      message(FATAL_ERROR "FMS_ENABLE_COVERAGE requires GCC or Clang")
    endif()
    # -O0 overrides whatever the build type asked for.  Optimised code has no
    # honest line mapping: inlining and folding make the report a work of
    # fiction, so a coverage build is always an unoptimised one.
    target_compile_options(${target} PRIVATE --coverage -O0 -g)
    target_link_options(${target} PUBLIC --coverage)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
      # Absolute paths in the .gcno files, so gcovr finds the sources no matter
      # where it is run from; atomic counters so a parallel test run cannot lose
      # increments.
      target_compile_options(${target} PRIVATE -fprofile-abs-path -fprofile-update=atomic)
    endif()
  endif()

  if(FMS_ENABLE_SANITIZERS)
    if(MSVC)
      message(FATAL_ERROR "FMS_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
    target_compile_options(${target} PRIVATE
      -fsanitize=address,undefined
      -fno-sanitize-recover=all
      -fno-omit-frame-pointer
      -g)
    target_link_options(${target} PUBLIC -fsanitize=address,undefined)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Dependencies: ETL for the containers, yaml-cpp for the config, doctest for
# the tests.  Nothing else - the core has no transport dependency at all.
# ---------------------------------------------------------------------------
include(FetchContent)

# yaml-cpp 0.8.0 still declares cmake_minimum_required(3.4), which CMake 4
# refuses outright.  Ask CMake to keep the old floor for it.
if(CMAKE_VERSION VERSION_GREATER_EQUAL 4.0 AND NOT DEFINED CMAKE_POLICY_VERSION_MINIMUM)
  set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
endif()

# --- Embedded Template Library ---------------------------------------------
if(FMS_FETCH_DEPS)
  FetchContent_Declare(etl
    GIT_REPOSITORY https://github.com/ETLCPP/etl.git
    GIT_TAG        20.39.4
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(etl)
else()
  find_package(etl REQUIRED)
endif()

# --- yaml-cpp ---------------------------------------------------------------
if(FMS_FETCH_DEPS)
  set(YAML_CPP_BUILD_TESTS   OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS   OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_INSTALL       OFF CACHE BOOL "" FORCE)
  set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(yaml-cpp)
else()
  find_package(yaml-cpp REQUIRED)
endif()

# ---------------------------------------------------------------------------
# fms_core : the state machine itself.  No exceptions, no heap after setup,
# no transport.  fms::IPort is an interface with no implementation here.
# ---------------------------------------------------------------------------
add_library(fms_core STATIC
  src/status.cpp
  src/args.cpp
  src/condition.cpp
  src/model.cpp
  src/setup.cpp
  src/state_machine.cpp
  src/runtime.cpp)
add_library(fms::core ALIAS fms_core)

target_include_directories(fms_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>)

# ETL_LOG_ERRORS must be visible to every TU that includes an ETL header,
# otherwise ETL_ASSERT expands differently in different TUs (ODR).  ETL only
# throws when ETL_THROW_EXCEPTIONS is defined - we never define it.
target_compile_definitions(fms_core PUBLIC ETL_LOG_ERRORS)
target_link_libraries(fms_core PUBLIC etl::etl)
fms_disable_exceptions(fms_core)
fms_warnings(fms_core)
fms_instrument(fms_core)

# ---------------------------------------------------------------------------
# fms_config : the *only* place yaml-cpp (and therefore exceptions) is used.
# ---------------------------------------------------------------------------
add_library(fms_config STATIC src/config/yaml_loader.cpp)
add_library(fms::config ALIAS fms_config)
target_link_libraries(fms_config PUBLIC fms_core PRIVATE yaml-cpp::yaml-cpp)
fms_enable_exceptions(fms_config)
fms_warnings(fms_config)
fms_instrument(fms_config)

# ---------------------------------------------------------------------------
# fms_console : an fms::IPort over std::cin / std::cout.  Separate target
# because it pulls in <iostream>, which a firmware build does not want.
# ---------------------------------------------------------------------------
if(FMS_BUILD_CONSOLE)
  add_library(fms_console STATIC src/port/console_port.cpp)
  add_library(fms::console ALIAS fms_console)
  target_link_libraries(fms_console PUBLIC fms_core)
  fms_disable_exceptions(fms_console)
  fms_warnings(fms_console)
fms_instrument(fms_console)
endif()

# ---------------------------------------------------------------------------
# fms_alloc_guard : optional link-in that traps heap use after setup.
# ---------------------------------------------------------------------------
add_library(fms_alloc_guard STATIC src/alloc_guard.cpp)
add_library(fms::alloc_guard ALIAS fms_alloc_guard)
target_link_libraries(fms_alloc_guard PUBLIC fms_core)
fms_disable_exceptions(fms_alloc_guard)
fms_warnings(fms_alloc_guard)
fms_instrument(fms_alloc_guard)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(FMS_BUILD_EXAMPLES AND FMS_BUILD_CONSOLE)
  add_executable(car_console examples/car/main.cpp)
  target_link_libraries(car_console PRIVATE fms_core fms_config fms_console)
  fms_disable_exceptions(car_console)
  fms_warnings(car_console)
fms_instrument(car_console)
  # Note what is deliberately absent here: nothing copies, reads, validates or
  # depends on the YAML.  The configuration is a run-time input, so the build
  # must not have an opinion about it - a machine file the build never opened
  # cannot be a machine file the build got wrong.  Pass the paths to the binary,
  # or run it from the directory the files live in.
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FMS_BUILD_TESTS)
  enable_testing()
  if(FMS_FETCH_DEPS)
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(doctest
      GIT_REPOSITORY https://github.com/doctest/doctest.git
      GIT_TAG        v2.4.11
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(doctest)
  else()
    find_package(doctest REQUIRED)
  endif()

  add_executable(fms_tests
    tests/test_main.cpp
    tests/test_args.cpp
    tests/test_condition.cpp
    tests/test_state_machine.cpp
    tests/test_setup.cpp
    tests/test_loader.cpp
    tests/test_guards.cpp
    tests/test_runtime.cpp
    tests/test_no_alloc.cpp)
  target_link_libraries(fms_tests PRIVATE
    fms_core fms_config fms_alloc_guard doctest::doctest)
  target_compile_definitions(fms_tests PRIVATE
    FMS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    fms_warnings(fms_tests)   # tests keep exceptions: doctest needs them
  fms_instrument(fms_tests)
  add_test(NAME fms_tests COMMAND fms_tests)

  # The console example is exercised through a pipe, which also proves the
  # port works with redirected stdin.  The YAML paths are passed to the test
  # command, so the files are opened when the test runs - not while configuring.
  if(FMS_BUILD_EXAMPLES AND FMS_BUILD_CONSOLE)
    add_test(NAME car_console_pipe
      COMMAND ${CMAKE_COMMAND}
              -DEXE=$<TARGET_FILE:car_console>
              -DSETUP=${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.setup.yaml
              -DMACHINE=${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.machine.yaml
              -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/car_session.txt
              -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/car_session.expected
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_console_test.cmake)

    # Loading the shipped configuration is itself a test, run by the binary.
    add_test(NAME car_config_check
      COMMAND car_console
              ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.setup.yaml
              ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.machine.yaml
              --check)
  endif()
endif()
