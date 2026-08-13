// SPDX-License-Identifier: MIT
//
// The `io` block of the setup file, handed to the port verbatim.
//
// It lives in its own header so that fms/port.hpp can name it without dragging
// in the Model: a port needs to know where to connect, not what the machine
// does.
#ifndef FMS_IO_CONFIG_HPP
#define FMS_IO_CONFIG_HPP

#include "fms/types.hpp"

namespace fms {

/// Where the machine talks, and how to reach the world.  Every field is opaque
/// to the core - it is the port that decides what an endpoint string means, and
/// a port is free to ignore any of them.
struct IoConfig {
  Channel state_channel{};  ///< the new state's name is announced here
  Channel error_channel{};  ///< refused triggers are reported here
  Channel endpoint{};       ///< broker URI, device path, socket - port specific
  Name    identity{};       ///< client id, node name - port specific
};

}  // namespace fms

#endif  // FMS_IO_CONFIG_HPP
