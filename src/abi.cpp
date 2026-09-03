// SPDX-License-Identifier: MIT
//
// The definition side of the link-time capacity guard described in fms/abi.hpp.
// This translation unit is part of fms_core, so the name it defines records the
// capacities the library itself was built with.
#include "fms/abi.hpp"

extern "C" void FMS_ABI_SYMBOL() noexcept {}
