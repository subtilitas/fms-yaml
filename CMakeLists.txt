cmake_minimum_required(VERSION 3.20)
project(fms_yaml VERSION 0.2.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FMS_BUILD_TESTS    "Build unit tests"                        ON)
option(FMS_BUILD_EXAMPLES "Build the example applications"          ON)
option(FMS_BUILD_CONSOLE  "Build the std::cin/std::cout port"       ON)
option(FMS_FETCH_DEPS     "Download dependencies with FetchContent" ON)

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
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow)
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
  src/model.cpp
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

# ---------------------------------------------------------------------------
# fms_config : the *only* place yaml-cpp (and therefore exceptions) is used.
# ---------------------------------------------------------------------------
add_library(fms_config STATIC src/config/yaml_loader.cpp)
add_library(fms::config ALIAS fms_config)
target_link_libraries(fms_config PUBLIC fms_core PRIVATE yaml-cpp::yaml-cpp)
fms_enable_exceptions(fms_config)
fms_warnings(fms_config)

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
endif()

# ---------------------------------------------------------------------------
# fms_alloc_guard : optional link-in that traps heap use after setup.
# ---------------------------------------------------------------------------
add_library(fms_alloc_guard STATIC src/alloc_guard.cpp)
add_library(fms::alloc_guard ALIAS fms_alloc_guard)
target_link_libraries(fms_alloc_guard PUBLIC fms_core)
fms_disable_exceptions(fms_alloc_guard)
fms_warnings(fms_alloc_guard)

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(FMS_BUILD_EXAMPLES AND FMS_BUILD_CONSOLE)
  add_executable(car_console examples/car/main.cpp)
  target_link_libraries(car_console PRIVATE fms_core fms_config fms_console)
  fms_disable_exceptions(car_console)
  fms_warnings(car_console)
  add_custom_command(TARGET car_console POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.yaml
            $<TARGET_FILE_DIR:car_console>/car.yaml)
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
    tests/test_state_machine.cpp
    tests/test_loader.cpp
    tests/test_runtime.cpp
    tests/test_no_alloc.cpp)
  target_link_libraries(fms_tests PRIVATE
    fms_core fms_config fms_alloc_guard doctest::doctest)
  target_compile_definitions(fms_tests PRIVATE
    FMS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  fms_warnings(fms_tests)   # tests keep exceptions: doctest needs them
  add_test(NAME fms_tests COMMAND fms_tests)

  # The console example is exercised through a pipe, which also proves the
  # port works with redirected stdin.
  if(FMS_BUILD_EXAMPLES AND FMS_BUILD_CONSOLE)
    add_test(NAME car_console_pipe
      COMMAND ${CMAKE_COMMAND}
              -DEXE=$<TARGET_FILE:car_console>
              -DCONFIG=${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.yaml
              -DSCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/tests/car_session.txt
              -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/car_session.expected
              -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_console_test.cmake)
  endif()
endif()
