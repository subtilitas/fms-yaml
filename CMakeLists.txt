cmake_minimum_required(VERSION 3.20)
project(fms_yaml VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FMS_BUILD_TESTS    "Build unit tests"                        ON)
option(FMS_BUILD_EXAMPLES "Build the example applications"          ON)
option(FMS_BUILD_CONSOLE  "Build the std::cin/std::cout port"       ON)
# The YAML loader is the only part that needs yaml-cpp, a filesystem and
# exceptions.  A firmware build does not want any of the three: it loads the
# configuration on the host and ships the Model, which is the porting route
# docs/architecture.md describes.  Turning it off is what lets the tree be
# cross-compiled for a target at all.
option(FMS_BUILD_CONFIG   "Build the YAML loader"                    ON)
option(FMS_BUILD_INSPECT  "Build the linter and the diagram exporter"  ON)
option(FMS_FETCH_DEPS     "Download dependencies with FetchContent" ON)

# Installing exports the targets, and an exported target may only refer to
# things that are themselves exported or found.  A dependency FetchContent
# built inside this tree is neither, so the two are mutually exclusive and the
# default follows FMS_FETCH_DEPS rather than surprising anyone.
if(FMS_FETCH_DEPS)
  set(fms_install_default OFF)
else()
  set(fms_install_default ON)
endif()
option(FMS_INSTALL "Generate install and package rules" ${fms_install_default})

# Everything that reads a config file goes through fms_config, so without it
# there is no example to run and most of the suite has nothing to load.  Saying
# so here beats a link error several hundred lines later.
if(NOT FMS_BUILD_CONFIG)
  if(FMS_BUILD_TESTS OR FMS_BUILD_EXAMPLES)
    message(FATAL_ERROR
      "FMS_BUILD_CONFIG=OFF needs -DFMS_BUILD_TESTS=OFF -DFMS_BUILD_EXAMPLES=OFF: "
      "both load a configuration, which is what fms_config is.")
  endif()
endif()

if(FMS_INSTALL AND FMS_FETCH_DEPS)
  message(FATAL_ERROR
    "FMS_INSTALL requires -DFMS_FETCH_DEPS=OFF: a dependency fetched into this "
    "build tree cannot be exported with it.  Install ETL and yaml-cpp first, "
    "then point CMAKE_PREFIX_PATH at them.")
endif()

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
    # /external:anglebrackets puts everything included with <> outside the
    # warning level, and /external:W0 silences it.  Without them /W4 reports on
    # the standard library, ETL, yaml-cpp and doctest as though we had written
    # them - the same observation the -isystem treatment makes on GCC and Clang,
    # which MSVC does not take from CMake's SYSTEM include dirs alone.
    target_compile_options(${target} PRIVATE /W4 /external:anglebrackets /external:W0)
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

# ---------------------------------------------------------------------------
# Helper: demote a dependency's headers to -isystem.
#
# Without this, -Wall -Wextra -Wshadow apply to yaml-cpp's and ETL's headers as
# though we had written them.  Clang's -Wshadow, for instance, objects to
# YAML::EmitterStyle::Block shadowing YAML::Block - a real observation about
# yaml-cpp that we cannot act on and did not ask for.  With -Werror it fails
# the build outright, which is a warning system reporting on the wrong code.
#
# FetchContent_Declare gained a SYSTEM keyword in CMake 3.25, but this project
# supports 3.20, so set the property directly - it works on every version and
# on ALIAS targets, which is how all three dependencies are consumed.
# ---------------------------------------------------------------------------
function(fms_make_dependency_system target)
  if(NOT TARGET ${target})
    return()
  endif()
  get_target_property(aliased ${target} ALIASED_TARGET)
  if(aliased)
    set(target ${aliased})
  endif()
  get_target_property(dirs ${target} INTERFACE_INCLUDE_DIRECTORIES)
  if(dirs)
    set_target_properties(${target} PROPERTIES
      INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${dirs}")
  endif()
endfunction()

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
fms_make_dependency_system(etl::etl)

# --- yaml-cpp ---------------------------------------------------------------
# Only fms_config uses it, so a build without the loader neither fetches nor
# looks for it - which is the difference between cross-compiling and not.
if(NOT FMS_BUILD_CONFIG)
  # nothing to do
elseif(FMS_FETCH_DEPS)
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
fms_make_dependency_system(yaml-cpp::yaml-cpp)

# ---------------------------------------------------------------------------
# Every target below states its own exception policy, so drop the default one.
#
# MSVC's default flags carry /EHsc.  fms_disable_exceptions appends /EHs-c-,
# which does the right thing and reports `command line warning D9025:
# overriding '/EHs' with '/EHs-'` for every target, ten lines a build.  Removing
# it here rather than overriding it per target means each target says what it
# wants once.
#
# After the dependencies, deliberately: yaml-cpp is built in this tree and needs
# exceptions, and it took the flags at the FetchContent call above.
# ---------------------------------------------------------------------------
if(MSVC)
  string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# ---------------------------------------------------------------------------
# fms_core : the state machine itself.  No exceptions, no heap after setup,
# no transport.  fms::IPort is an interface with no implementation here.
# ---------------------------------------------------------------------------
add_library(fms_core STATIC
  src/abi.cpp
  src/status.cpp
  src/args.cpp
  src/condition.cpp
  src/model.cpp
  src/setup.cpp
  src/state_machine.cpp
  src/runtime.cpp)
add_library(fms::core ALIAS fms_core)

# The version is declared by project() and nowhere else; the header is written
# from it.  A header that is generated cannot drift from the tag the release
# workflow builds, which a hand-maintained one did once already.
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/include/fms/version.hpp.in
  ${PROJECT_BINARY_DIR}/generated/fms/version.hpp
  @ONLY)

# GNUInstallDirs here rather than beside the install rules: CMAKE_INSTALL_INCLUDEDIR
# is part of the interface below, and hardcoding "include" makes the exported
# target name a directory the headers are not in under any other layout.
include(GNUInstallDirs)

target_include_directories(fms_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# ETL_LOG_ERRORS must be visible to every TU that includes an ETL header,
# otherwise ETL_ASSERT expands differently in different TUs (ODR).  ETL only
# throws when ETL_THROW_EXCEPTIONS is defined - we never define it.
target_compile_definitions(fms_core PUBLIC ETL_LOG_ERRORS)

# ---------------------------------------------------------------------------
# Capacities.
#
# The FMS_MAX_* values in include/fms/limits.hpp are template arguments of the
# containers inside Model, Setup, Args, Runtime and lint::Report, so they decide
# the layout of those types.  Every translation unit in a program must therefore
# see the same values - the same ODR requirement as ETL_LOG_ERRORS above, and
# with a worse failure: the link succeeds and the two halves disagree about how
# large a Model is.
#
# So a capacity is set once, on the command line, and travels as a PUBLIC
# compile definition on fms_core to everything that links it, including the
# installed package's consumers:
#
#     cmake -S . -B build -DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
#
# Unset means "whatever limits.hpp says", so the header stays the one place the
# defaults are written down.  fms/abi.hpp catches the case this cannot reach: a
# translation unit compiled by hand, outside this build, against a prebuilt
# library.
set(FMS_CAPACITIES
  FMS_MAX_STATES
  FMS_MAX_TRIGGERS
  FMS_MAX_TRANSITIONS_PER_STATE
  FMS_MAX_ALTERNATIVES
  FMS_MAX_CONDITIONS_PER_GUARD
  FMS_MAX_CONDITIONS
  FMS_MAX_ARGUMENTS
  FMS_MAX_NAME_LENGTH
  FMS_MAX_CHANNEL_LENGTH
  FMS_MAX_MESSAGE_LENGTH
  FMS_MAX_FINDINGS)

foreach(capacity IN LISTS FMS_CAPACITIES)
  if(DEFINED ${capacity})
    if(NOT "${${capacity}}" MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR "${capacity} must be a positive integer, got '${${capacity}}'")
    endif()
    target_compile_definitions(fms_core PUBLIC ${capacity}=${${capacity}})
    list(APPEND fms_capacity_summary "${capacity}=${${capacity}}")
  endif()
endforeach()

if(fms_capacity_summary)
  list(JOIN fms_capacity_summary " " fms_capacity_line)
  message(STATUS "fms capacities: ${fms_capacity_line}")
  # The same list as compiler flags, for tools/abi_guard_check.sh: its probe is
  # compiled outside this build and has to be told what the tree was configured
  # with, or it would differ from the library it is meant to match.
  list(TRANSFORM fms_capacity_summary PREPEND "-D" OUTPUT_VARIABLE fms_capacity_flags)
  list(JOIN fms_capacity_flags " " fms_capacity_flags)
endif()
target_link_libraries(fms_core PUBLIC etl::etl)
fms_disable_exceptions(fms_core)
fms_warnings(fms_core)
fms_instrument(fms_core)

# ---------------------------------------------------------------------------
# fms_config : the *only* place yaml-cpp (and therefore exceptions) is used.
# ---------------------------------------------------------------------------
if(FMS_BUILD_CONFIG)
  add_library(fms_config STATIC src/config/yaml_loader.cpp)
  add_library(fms::config ALIAS fms_config)
  target_link_libraries(fms_config PUBLIC fms_core PRIVATE yaml-cpp::yaml-cpp)
  fms_enable_exceptions(fms_config)
  fms_warnings(fms_config)
  fms_instrument(fms_config)
endif()

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
# fms_inspect : reads a machine and reports on it - the linter and the diagram
# exporter.  Its own target because neither is needed to *run* a machine: a
# firmware build links fms_core and stops there, and nothing here is reachable
# from the run phase.
# ---------------------------------------------------------------------------
if(FMS_BUILD_INSPECT)
  add_library(fms_inspect STATIC
    src/inspect/lint.cpp
    src/inspect/diagram.cpp)
  add_library(fms::inspect ALIAS fms_inspect)
  target_link_libraries(fms_inspect PUBLIC fms_core)
  fms_disable_exceptions(fms_inspect)
  fms_warnings(fms_inspect)
  fms_instrument(fms_inspect)
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
if(FMS_BUILD_EXAMPLES AND FMS_BUILD_CONSOLE AND FMS_BUILD_CONFIG)
  add_executable(car_console examples/car/main.cpp)
  target_link_libraries(car_console PRIVATE fms_core fms_config fms_console)
  if(FMS_BUILD_INSPECT)
    # --lint and --export exist only when the analysis library is built; the
    # example says so at compile time rather than shipping flags that do nothing.
    target_link_libraries(car_console PRIVATE fms_inspect)
    target_compile_definitions(car_console PRIVATE FMS_WITH_INSPECT=1)
  endif()
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
  fms_make_dependency_system(doctest::doctest)

  add_executable(fms_tests
    tests/test_main.cpp
    tests/test_abi.cpp
    tests/test_args.cpp
    tests/test_condition.cpp
    tests/test_status.cpp
    tests/test_state_machine.cpp
    tests/test_setup.cpp
    tests/test_loader.cpp
    tests/test_guards.cpp
    tests/test_runtime.cpp
    tests/test_no_alloc.cpp)
  target_link_libraries(fms_tests PRIVATE
    fms_core fms_config fms_alloc_guard doctest::doctest)
  # The console port is an optional target, so its suite is optional too: it
  # drives ConsolePort in process by swapping the standard stream buffers.
  if(FMS_BUILD_CONSOLE)
    target_sources(fms_tests PRIVATE tests/test_console_port.cpp)
    target_link_libraries(fms_tests PRIVATE fms_console)
  endif()
  if(FMS_BUILD_INSPECT)
    target_sources(fms_tests PRIVATE tests/test_lint.cpp tests/test_diagram.cpp)
    target_link_libraries(fms_tests PRIVATE fms_inspect)
  endif()
  # FMS_BINARY_DIR is where a test may create something: the source tree is read
  # by every other gate and stays as checked out.
  target_compile_definitions(fms_tests PRIVATE
    FMS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    FMS_BINARY_DIR="${CMAKE_BINARY_DIR}")
  # doctest reports a failed assertion by throwing, so this is the second target
  # that keeps exceptions.  Said outright rather than inherited from a default,
  # which is also what stops MSVC's D9025 above.
  fms_enable_exceptions(fms_tests)
  fms_warnings(fms_tests)
  fms_instrument(fms_tests)
  add_test(NAME fms_tests COMMAND fms_tests)

  # ---------------------------------------------------------------------------
  # The capacity guard is a claim about linking, so proving it needs a second
  # link rather than a second test case: one probe with the library's own
  # capacities, which must link, and one with different ones, which must not.
  # tools/abi_guard_check.sh does both and reads its paths from this file.
  #
  # Skipped for instrumented builds - coverage and the sanitizers add link
  # requirements of their own, so a probe linked without them would fail for a
  # reason that has nothing to do with capacities.
  # ---------------------------------------------------------------------------
  # The script lives in tools/, which the source distribution does not carry,
  # so its absence is a package built from a release tarball rather than a
  # mistake - the same reason car_diagram_check tests for its script below.  A
  # test that fails because its own file is missing tests nothing.
  if(NOT MSVC AND NOT FMS_ENABLE_COVERAGE AND NOT FMS_ENABLE_SANITIZERS
     AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
     AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_guard_check.sh)
    file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/abi_probe.env CONTENT
"CXX=\"${CMAKE_CXX_COMPILER}\"
FMS_INCLUDE=\"${CMAKE_CURRENT_SOURCE_DIR}/include\"
ETL_INCLUDE=\"$<JOIN:$<TARGET_PROPERTY:etl::etl,INTERFACE_INCLUDE_DIRECTORIES>,;>\"
FMS_CORE_LIB=\"$<TARGET_FILE:fms_core>\"
FMS_CAPACITY_FLAGS=\"${fms_capacity_flags}\"
")
    add_test(NAME abi_guard
      COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/abi_guard_check.sh
              ${CMAKE_BINARY_DIR})
    set_tests_properties(abi_guard PROPERTIES DEPENDS fms_tests)
  endif()

  # ---------------------------------------------------------------------------
  # "No exceptions" and "no allocation" are properties of the built archives,
  # and until now only of the flags that are supposed to produce them.  A
  # -fno-exceptions that stopped being applied, or a container that started
  # calling operator new, would leave every test green.  tools/symbol_check.sh
  # runs the nm invocations docs/architecture.md has always printed.
  #
  # Skipped for instrumented builds and on MSVC for the same reasons as
  # abi_guard: the coverage and sanitizer runtimes bring symbols of their own,
  # and the names here are the platform's.
  # ---------------------------------------------------------------------------
  if(NOT MSVC AND NOT FMS_ENABLE_COVERAGE AND NOT FMS_ENABLE_SANITIZERS
     AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"
     AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/symbol_check.sh")
    set(fms_symbol_env "NM=\"${CMAKE_NM}\"\n")
    string(APPEND fms_symbol_env "FMS_CORE_LIB=\"$<TARGET_FILE:fms_core>\"\n")
    if(FMS_BUILD_CONFIG)
      string(APPEND fms_symbol_env "FMS_CONFIG_LIB=\"$<TARGET_FILE:fms_config>\"\n")
    endif()
    string(APPEND fms_symbol_env
      "FMS_ALLOC_GUARD_LIB=\"$<TARGET_FILE:fms_alloc_guard>\"\n")
    if(FMS_BUILD_CONSOLE)
      string(APPEND fms_symbol_env "FMS_CONSOLE_LIB=\"$<TARGET_FILE:fms_console>\"\n")
    endif()
    if(FMS_BUILD_INSPECT)
      string(APPEND fms_symbol_env "FMS_INSPECT_LIB=\"$<TARGET_FILE:fms_inspect>\"\n")
    endif()
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/symbol_check.env"
         CONTENT "${fms_symbol_env}")

    add_test(NAME symbol_check
      COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/tools/symbol_check.sh"
              "${CMAKE_BINARY_DIR}")
  endif()

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

    # The version the binary reports comes from the header CMake generated from
    # project(), so this is the check that a release tag, the CMake project and
    # the running program are still the same number.
    string(REPLACE "." "\\." fms_version_pattern "${PROJECT_VERSION}")
    add_test(NAME car_version COMMAND car_console --version)
    set_tests_properties(car_version PROPERTIES PASS_REGULAR_EXPRESSION
      "fms-yaml ${fms_version_pattern} \\(capacities [0-9_]+\\)")

    # Loading the shipped configuration is itself a test, run by the binary.
    # --check also lints what it loaded, so this fails on a machine that is
    # valid and still cannot mean what it says.
    add_test(NAME car_config_check
      COMMAND car_console
              ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.setup.yaml
              ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.machine.yaml
              --check)

    # The diagram in the README is generated from the machine file; this is the
    # check that keeps it that way.  It regenerates the picture and compares,
    # rather than trusting that whoever last changed the YAML also redrew it.
    # The script lives in tools/, which a source distribution does not carry:
    # keeping the README honest is the repository's business, not a consumer's.
    if(FMS_BUILD_INSPECT AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/diagram_sync.py)
      find_package(Python3 COMPONENTS Interpreter QUIET)
      if(Python3_FOUND)
        add_test(NAME car_diagram_check
          COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/diagram_sync.py
                  --check
                  --binary  $<TARGET_FILE:car_console>
                  --setup   ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.setup.yaml
                  --machine ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.machine.yaml
                  --readme  ${CMAKE_CURRENT_SOURCE_DIR}/README.md)
      else()
        message(STATUS "python3 not found: the README diagram will not be checked")
      endif()
    endif()
  endif()
endif()

# ---------------------------------------------------------------------------
# Install and package
#
# The library is source-distributed, but a consumer still needs a way to say
# "use it" that is not add_subdirectory: install here, then
#
#   find_package(fms_yaml REQUIRED)
#   target_link_libraries(app PRIVATE fms::core fms::config)
#
# The exported targets carry the capacities this tree was configured with as
# INTERFACE compile definitions, so a consumer inherits them and cannot end up
# in the mismatch fms/abi.hpp exists to catch.
#
# Which targets exist depends on the options, so the export set is built up
# rather than written out: a package that promised fms::console when
# FMS_BUILD_CONSOLE was off would fail at the consumer's configure step.
# ---------------------------------------------------------------------------
if(FMS_INSTALL)
  include(CMakePackageConfigHelpers)

  set(fms_install_targets fms_core fms_alloc_guard)
  if(FMS_BUILD_CONFIG)
    list(APPEND fms_install_targets fms_config)
  endif()
  if(FMS_BUILD_CONSOLE)
    list(APPEND fms_install_targets fms_console)
  endif()
  if(FMS_BUILD_INSPECT)
    list(APPEND fms_install_targets fms_inspect)
  endif()

  # NAMESPACE alone would export fms_core as fms::fms_core.  EXPORT_NAME makes
  # the installed names the same as the ALIAS targets this tree defines, so the
  # target_link_libraries line in a consumer is the same either way.
  foreach(target IN LISTS fms_install_targets)
    string(REPLACE "fms_" "" export_name "${target}")
    set_target_properties(${target} PROPERTIES EXPORT_NAME ${export_name})
  endforeach()

  # No INCLUDES DESTINATION: fms_core already carries
  # $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>, and naming the directory
  # in both places puts it in the exported target twice.
  install(TARGETS ${fms_install_targets}
    EXPORT fms_yamlTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

  # A header whose target was not built is a promise the package cannot keep,
  # so the optional ones follow their targets.  memory_port.hpp is not among
  # them: it is header-only and belongs to the core.
  set(fms_header_excludes)
  if(NOT FMS_BUILD_CONSOLE)
    list(APPEND fms_header_excludes PATTERN "console_port.hpp" EXCLUDE)
  endif()
  if(NOT FMS_BUILD_INSPECT)
    list(APPEND fms_header_excludes PATTERN "inspect" EXCLUDE)
  endif()

  # version.hpp.in is the template for a header generated into the build tree;
  # shipping it too would put two spellings of the version in one directory.
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/fms
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp" ${fms_header_excludes})
  install(FILES ${PROJECT_BINARY_DIR}/generated/fms/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fms)

  install(EXPORT fms_yamlTargets
    FILE      fms_yamlTargets.cmake
    NAMESPACE fms::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fms_yaml)

  # The config file asks for yaml-cpp only when fms_config is part of the
  # package, which -DFMS_BUILD_CONFIG=OFF is exactly the case it is not: a
  # consumer of that package needs neither yaml-cpp nor a find_dependency that
  # would fail without it.
  set(FMS_INSTALLED_CONFIG ${FMS_BUILD_CONFIG})
  configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fms_yamlConfig.cmake.in
    ${PROJECT_BINARY_DIR}/fms_yamlConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fms_yaml)

  # SameMajorVersion: from 1.0 the major is the breaking number.  The rule is
  # directional - a 1.2 install satisfies a consumer asking for 1.0, and a 1.0
  # install does not satisfy one asking for 1.2 - so it is backward
  # compatibility within a major, not interchangeability.  It was
  # SameMinorVersion through 0.x, where the minor carried that meaning;
  # tools/install_check.sh asserts whichever of the two applies to the version
  # project() declares, and that this line agrees with it.
  write_basic_package_version_file(
    ${PROJECT_BINARY_DIR}/fms_yamlConfigVersion.cmake
    VERSION       ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)

  install(FILES
    ${PROJECT_BINARY_DIR}/fms_yamlConfig.cmake
    ${PROJECT_BINARY_DIR}/fms_yamlConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fms_yaml)

  install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/licenses/fms_yaml)
endif()
