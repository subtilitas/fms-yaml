# SPDX-License-Identifier: MIT
#
# A consumer of the installed package, and nothing else: this project knows
# where fms-yaml is only through find_package.  It is built by
# tools/install_check.sh, which installs the library first and then points
# CMAKE_PREFIX_PATH here.
#
# What it proves is not that the code works - the suite in tests/ does that -
# but that the package is usable: the config file resolves, the dependencies it
# names are found, and the headers are where the targets say they are.  It is
# built twice, the second time against a prefix installed with a non-default
# FMS_MAX_ARGUMENTS, and the capacity tag it prints is what says the exported
# targets carried that capacity across.
cmake_minimum_required(VERSION 3.20)
project(fms_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(fms_yaml REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE fms::core fms::config fms::inspect)
