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

# -ffreestanding says there is no hosted runtime; --specs=nano.specs picks
# newlib-nano.  Neither is needed to compile fms_core - which is the point of
# the exercise - but a target build has them, so the check has them.
set(CMAKE_C_FLAGS_INIT   "${fms_arm_flags} -ffreestanding")
set(CMAKE_CXX_FLAGS_INIT "${fms_arm_flags} -ffreestanding -fno-rtti")

# Look for programs on the host, and for everything else only in the target
# sysroot: a host header found by accident would make the check pass for the
# wrong reason.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM BEFORE)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
