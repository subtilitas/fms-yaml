// SPDX-License-Identifier: MIT
//
// Compile-time capacities.  The whole machine is sized from these numbers, so
// it is one fixed object with a footprint known at build time.  Override them
// from the build system, e.g.
//     target_compile_definitions(app PRIVATE FMS_MAX_STATES=8)
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

// A channel is whatever address the port uses: an MQTT topic, a CAN id, a word
// typed on stdin.  The core never interprets it.
#ifndef FMS_MAX_CHANNEL_LENGTH
#define FMS_MAX_CHANNEL_LENGTH 95
#endif

#ifndef FMS_MAX_MESSAGE_LENGTH
#define FMS_MAX_MESSAGE_LENGTH 127
#endif

namespace fms::limits {

inline constexpr std::size_t kMaxStates              = FMS_MAX_STATES;
inline constexpr std::size_t kMaxTriggers            = FMS_MAX_TRIGGERS;
inline constexpr std::size_t kMaxTransitionsPerState = FMS_MAX_TRANSITIONS_PER_STATE;
inline constexpr std::size_t kMaxNameLength          = FMS_MAX_NAME_LENGTH;
inline constexpr std::size_t kMaxChannelLength       = FMS_MAX_CHANNEL_LENGTH;
inline constexpr std::size_t kMaxMessageLength       = FMS_MAX_MESSAGE_LENGTH;

}  // namespace fms::limits

#endif  // FMS_LIMITS_HPP
