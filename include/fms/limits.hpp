// SPDX-License-Identifier: MIT
//
// Compile-time capacities.  The whole machine is sized from these numbers, so
// it is one fixed object with a footprint known at build time.
//
// They are template arguments of the containers inside Model, Setup, Args,
// Runtime and lint::Report, so they are part of the layout of those types and
// every translation unit in a program must agree on them.  Set them once, on
// the command line, and the build carries them to every target as PUBLIC
// compile definitions on fms_core:
//
//     cmake -S . -B build -DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
//
// Setting one on a single target instead is an ODR violation the linker does
// not see: the caller and the library then disagree about how large a Model is.
// fms/abi.hpp makes that a link error; a capacity added here has to be added
// there too, and tools/abi_guard_check.sh fails the build if it is not.
#ifndef FMS_LIMITS_HPP
#define FMS_LIMITS_HPP

#include <cstddef>

#ifndef FMS_MAX_STATES
#define FMS_MAX_STATES 32
#endif

#ifndef FMS_MAX_TRIGGERS
#define FMS_MAX_TRIGGERS 32
#endif

#ifndef FMS_MAX_TRANSITIONS_PER_STATE
#define FMS_MAX_TRANSITIONS_PER_STATE 8
#endif

#ifndef FMS_MAX_NAME_LENGTH
#define FMS_MAX_NAME_LENGTH 31
#endif

// How many key=value pairs one trigger may carry.
#ifndef FMS_MAX_ARGUMENTS
#define FMS_MAX_ARGUMENTS 4
#endif

// How many guarded alternatives one trigger may have in one state.  The last
// one may be unguarded, which makes it the fallback.
#ifndef FMS_MAX_ALTERNATIVES
#define FMS_MAX_ALTERNATIVES 4
#endif

// Conditions in one guard.  They are ANDed; use several alternatives for OR.
#ifndef FMS_MAX_CONDITIONS_PER_GUARD
#define FMS_MAX_CONDITIONS_PER_GUARD 3
#endif

// Size of the machine-wide condition pool.  Conditions are interned there and
// referenced by index, so a transition stays a handful of bytes.
#ifndef FMS_MAX_CONDITIONS
#define FMS_MAX_CONDITIONS 64
#endif

// A channel is whatever address the port uses: an MQTT topic, a CAN id, a word
// typed on stdin.  The core never interprets it.
#ifndef FMS_MAX_CHANNEL_LENGTH
#define FMS_MAX_CHANNEL_LENGTH 95
#endif

#ifndef FMS_MAX_MESSAGE_LENGTH
#define FMS_MAX_MESSAGE_LENGTH 127
#endif

// How many findings one lint run records before it gives up.  This is not part
// of the machine - a Model does not contain a report - so only a caller that
// asks for one pays for it.
#ifndef FMS_MAX_FINDINGS
#define FMS_MAX_FINDINGS 32
#endif

namespace fms::limits {

inline constexpr std::size_t kMaxStates              = FMS_MAX_STATES;
inline constexpr std::size_t kMaxTriggers            = FMS_MAX_TRIGGERS;
inline constexpr std::size_t kMaxTransitionsPerState = FMS_MAX_TRANSITIONS_PER_STATE;
inline constexpr std::size_t kMaxNameLength          = FMS_MAX_NAME_LENGTH;
inline constexpr std::size_t kMaxChannelLength       = FMS_MAX_CHANNEL_LENGTH;
inline constexpr std::size_t kMaxMessageLength       = FMS_MAX_MESSAGE_LENGTH;
inline constexpr std::size_t kMaxArguments           = FMS_MAX_ARGUMENTS;
inline constexpr std::size_t kMaxAlternatives        = FMS_MAX_ALTERNATIVES;
inline constexpr std::size_t kMaxConditionsPerGuard  = FMS_MAX_CONDITIONS_PER_GUARD;
inline constexpr std::size_t kMaxConditions          = FMS_MAX_CONDITIONS;
inline constexpr std::size_t kMaxFindings            = FMS_MAX_FINDINGS;

}  // namespace fms::limits

#endif  // FMS_LIMITS_HPP
