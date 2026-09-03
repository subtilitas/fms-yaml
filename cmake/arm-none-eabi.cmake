# SPDX-License-Identifier: MIT
#
# Cross-compiling toolchain file for a bare-metal ARM target, used by
# tools/cross_check.sh and the `cross` CI job.
#
#   cmake -S . -B build-arm \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
#         -DFMS_BUILD_CONFIG=OFF -DFMS_BUILD_CONSOLE=OFF \
#         -DFMS_BUILD_TESTS=OFF -DFMS_BUILD_EXAMPLES=OFF
#
# docs/architecture.md says fms_core needs only <cstdint>, <cstddef>, <cstring>
# and ETL.  This is what turns that from a claim into a compile: Cortex-M4,
# newlib-nano, no operating system, and no host headers on the include path.
#
# Not -ffreestanding, though it would be the obvious flag.  ETL 20.39.4's
# etl/limits.h includes <math.h> unconditionally (line 47, reached from
# etl/string.h through etl/binary.h), and libstdc++ 13 routes that to <cmath>,
# which refuses to be included when __STDC_HOSTED__ is 0:
#
#   bits/requires_hosted.h:34: error: "This header is not available in
#   freestanding mode."
#
# So the library cannot be compiled strictly freestanding as long as it uses
# ETL's containers, whatever it includes itself.  A firmware build with
# newlib-nano is a hosted C++ library on a target with no operating system,
# which is what this file describes and what the claim in the porting section
# now says.
#
# It compiles; it does not link an image.  There is no startup code, no linker
# script and no board here, because none of that is the library's business - so
# the check is CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY below, which stops
# CMake trying to link a test executable during configuration.
set(CMAKE_SYSTEM_NAME      Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_COMPILER   arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER arm-none-eabi-g++)

# Cortex-M4 with a single-precision FPU: an ordinary target for a machine whose
# whole model is a few tens of kilobytes.
set(fms_arm_flags "-mcpu=cortex-m4 -mthumb -mfloat-abi=softfp -mfpu=fpv4-sp-d16")

# -fno-rtti because nothing here needs it and a target build would not pay for
# it.  No -ffreestanding, for the reason above.
set(CMAKE_C_FLAGS_INIT   "${fms_arm_flags}")
set(CMAKE_CXX_FLAGS_INIT "${fms_arm_flags} -fno-rtti")

# Look for programs on the host, and for everything else only in the target
# sysroot: a host header found by accident would make the check pass for the
# wrong reason.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
