// SPDX-License-Identifier: MIT
//
// The capacities in fms/limits.hpp are template arguments of the ETL containers
// inside Model, Setup, Args, Runtime and lint::Report, so they are part of the
// layout of those types.  A translation unit compiled with different values
// sees different types under the same names and links against the library
// anyway: with the defaults sizeof(fms::Model) is 49728, with
// FMS_MAX_STATES=8 FMS_MAX_TRIGGERS=12 it is 21120, and nothing diagnoses the
// difference.  The library then writes 49728 bytes into an object the caller
// allocated 21120 for.
//
// So the capacities are pasted into the name of a symbol that fms_core defines
// once and every Model, Setup and Report constructor references.  A mismatch is
// an undefined reference naming both configurations, at link time, rather than
// a corrupted object at run time.
//
// The capacities are set from the build system and reach every target through
// fms_core, which carries them as PUBLIC compile definitions:
//
//     cmake -S . -B build -DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
//
#ifndef FMS_ABI_HPP
#define FMS_ABI_HPP

#include "fms/limits.hpp"

// Two levels: the outer macro expands its arguments, the inner one pastes them.
#define FMS_ABI_PASTE(a, b) a##_##b
#define FMS_ABI_JOIN(a, b)  FMS_ABI_PASTE(a, b)

// Every capacity that appears in the layout of a type the caller allocates.
// FMS_MAX_FINDINGS is one of them: lint::Report is an etl::vector alias the
// caller declares and fms_inspect appends to.  Report is an alias and cannot
// carry a constructor of its own, so it is covered indirectly - analyse() takes
// the Model the report is about, and constructing that Model is what pins the
// configuration.
#define FMS_ABI_ID_01 FMS_ABI_JOIN(FMS_MAX_STATES,     FMS_MAX_TRIGGERS)
#define FMS_ABI_ID_02 FMS_ABI_JOIN(FMS_ABI_ID_01, FMS_MAX_TRANSITIONS_PER_STATE)
#define FMS_ABI_ID_03 FMS_ABI_JOIN(FMS_ABI_ID_02, FMS_MAX_ALTERNATIVES)
#define FMS_ABI_ID_04 FMS_ABI_JOIN(FMS_ABI_ID_03, FMS_MAX_CONDITIONS_PER_GUARD)
#define FMS_ABI_ID_05 FMS_ABI_JOIN(FMS_ABI_ID_04, FMS_MAX_CONDITIONS)
#define FMS_ABI_ID_06 FMS_ABI_JOIN(FMS_ABI_ID_05, FMS_MAX_ARGUMENTS)
#define FMS_ABI_ID_07 FMS_ABI_JOIN(FMS_ABI_ID_06, FMS_MAX_NAME_LENGTH)
#define FMS_ABI_ID_08 FMS_ABI_JOIN(FMS_ABI_ID_07, FMS_MAX_CHANNEL_LENGTH)
#define FMS_ABI_ID_09 FMS_ABI_JOIN(FMS_ABI_ID_08, FMS_MAX_MESSAGE_LENGTH)
#define FMS_ABI_ID    FMS_ABI_JOIN(FMS_ABI_ID_09, FMS_MAX_FINDINGS)

/// The capacity configuration as an identifier, e.g. `32_32_8_4_3_64_4_31_95_127_32`.
#define FMS_ABI_SYMBOL FMS_ABI_JOIN(fms_abi, FMS_ABI_ID)

#define FMS_ABI_STRINGIFY_(x) #x
#define FMS_ABI_STRINGIFY(x)  FMS_ABI_STRINGIFY_(x)

/// The same configuration as a string literal, for diagnostics.
#define FMS_ABI_TAG FMS_ABI_STRINGIFY(FMS_ABI_ID)

extern "C" {
/// Defined once, by fms_core, in a translation unit compiled with the
/// capacities its own name records.  It does nothing; only its name matters.
void FMS_ABI_SYMBOL() noexcept;
}

namespace fms::abi {

/// The capacities this translation unit was compiled with, in the order
/// FMS_ABI_TAG records them: states, triggers, transitions per state,
/// alternatives, conditions per guard, conditions, arguments, name length,
/// channel length, message length, findings.
constexpr const char* tag() noexcept { return FMS_ABI_TAG; }

/// Called by Model's and Setup's constructors.  Every program builds one of
/// each, so every program resolves this name, and it resolves only against an
/// fms_core built with the same capacities.
inline void pin() noexcept { FMS_ABI_SYMBOL(); }

}  // namespace fms::abi

#endif  // FMS_ABI_HPP
