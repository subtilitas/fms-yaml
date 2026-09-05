// SPDX-License-Identifier: MIT
//
// The definition side of the link-time capacity guard described in fms/abi.hpp.
// This translation unit is part of fms_core, so the name it defines records the
// capacities the library itself was built with.
#include "fms/abi.hpp"

extern "C" void FMS_ABI_SYMBOL() noexcept {}

// And the ETL half: defined for the numbers this translation unit's ETL
// produced, which are the ones the library's own types were laid out with.
template <>
void fms::abi::etl_pin_here::pin() noexcept {}
