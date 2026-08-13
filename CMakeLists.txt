cmake_minimum_required(VERSION 3.20)
project(fms_yaml VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(FMS_BUILD_TESTS      "Build unit tests"                      ON)
option(FMS_BUILD_EXAMPLES   "Build the example applications"        ON)
option(FMS_WITH_PAHO        "Build the paho MQTT transport"         ON)
option(FMS_FETCH_DEPS       "Download dependencies with FetchContent" ON)

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

# Same, minus -Wpedantic: for targets that include third-party C headers
# (paho's MQTTProperties.h uses an anonymous struct).
function(fms_warnings_relaxed target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wshadow)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# yaml-cpp 0.8.0 and paho.mqtt.c 1.3.x still declare cmake_minimum_required(3.4),
# which CMake 4 refuses outright.  Ask CMake to keep the old floor for them.
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

# --- paho.mqtt.c ------------------------------------------------------------
set(FMS_PAHO_TARGET "")
if(FMS_WITH_PAHO)
  if(FMS_FETCH_DEPS)
    set(PAHO_BUILD_STATIC   ON  CACHE BOOL "" FORCE)
    set(PAHO_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
    set(PAHO_WITH_SSL       OFF CACHE BOOL "" FORCE)
    set(PAHO_BUILD_SAMPLES  OFF CACHE BOOL "" FORCE)
    set(PAHO_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
    set(PAHO_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(paho
      GIT_REPOSITORY https://github.com/eclipse/paho.mqtt.c.git
      GIT_TAG        v1.3.13
      GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(paho)
  else()
    find_package(eclipse-paho-mqtt-c REQUIRED)
  endif()

  if(TARGET eclipse-paho-mqtt-c::paho-mqtt3c-static)
    set(FMS_PAHO_TARGET eclipse-paho-mqtt-c::paho-mqtt3c-static)
  elseif(TARGET paho-mqtt3c-static)
    set(FMS_PAHO_TARGET paho-mqtt3c-static)
  elseif(TARGET eclipse-paho-mqtt-c::paho-mqtt3c)
    set(FMS_PAHO_TARGET eclipse-paho-mqtt-c::paho-mqtt3c)
  else()
    message(WARNING "paho.mqtt.c target not found - disabling the paho transport")
    set(FMS_WITH_PAHO OFF)
  endif()
endif()

# ---------------------------------------------------------------------------
# fms_core : the state machine itself.  No exceptions, no heap after setup.
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
# fms_paho : paho.mqtt.c binding for fms::mqtt::ITransport.
# ---------------------------------------------------------------------------
if(FMS_WITH_PAHO)
  add_library(fms_paho STATIC src/mqtt/paho_transport.cpp)
  add_library(fms::paho ALIAS fms_paho)
  target_link_libraries(fms_paho PUBLIC fms_core PRIVATE ${FMS_PAHO_TARGET})
  target_compile_definitions(fms_paho PUBLIC FMS_WITH_PAHO=1)
  fms_disable_exceptions(fms_paho)
  fms_warnings_relaxed(fms_paho)
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
if(FMS_BUILD_EXAMPLES)
  add_executable(car_ecu examples/car/main.cpp)
  target_link_libraries(car_ecu PRIVATE fms_core fms_config)
  if(FMS_WITH_PAHO)
    target_link_libraries(car_ecu PRIVATE fms_paho)
  endif()
  fms_disable_exceptions(car_ecu)
  fms_warnings(car_ecu)
  add_custom_command(TARGET car_ecu POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/examples/car/car.yaml
            $<TARGET_FILE_DIR:car_ecu>/car.yaml)
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
endif()
