// SPDX-License-Identifier: MIT
//
// The configuration front end, and the boundary of the exception-free world:
// src/config/yaml_loader.cpp is the only TU built with -fexceptions, because
// yaml-cpp reports errors by throwing.  Every entry point below is noexcept and
// catches everything, so callers only ever see a Status plus a diagnostic.
//
// The configuration comes in two files, loaded independently:
//
//   setup    fsm (name, initial) + io      -> fms::Setup    where this instance
//                                                           runs and how it talks
//   machine  triggers + states             -> fms::Model    what it does
//
// Neither file knows about the other.  The one cross-file reference - the
// initial state named by the setup - is checked when the two are bound in
// StateMachine::init.
//
// This is also the only phase that allocates: yaml-cpp builds its document on
// the heap, we copy what we need into the fixed-capacity objects, and the
// document is destroyed before the function returns.
#ifndef FMS_YAML_LOADER_HPP
#define FMS_YAML_LOADER_HPP

#include "fms/model.hpp"
#include "fms/setup.hpp"
#include "fms/status.hpp"

namespace fms::config {

/// The path is opened once and parsed from that stream, so a source that cannot
/// be reopened - a FIFO, a socket - is read whole rather than from its second
/// byte.  **The open blocks as the platform blocks:** a FIFO with no writer
/// waits inside the call, which therefore does not return a Status at all.
/// Configuration is expected to be a regular file; anything else is the
/// caller's to make ready before the call.

/// Where and why a load failed.
struct Diagnostics {
  Status  status = Status::Ok;
  Message message{};   ///< fixed capacity, never allocates
  int     line   = -1; ///< YAML line, when one was reported

  void reset() noexcept {
    status = Status::Ok;
    message.clear();
    line = -1;
  }
};

// ---------------------------------------------------------------------------
// setup: fsm + io
// ---------------------------------------------------------------------------

/// Reads the setup file into `setup`, which is cleared on entry and left empty
/// if loading fails.
Status load_setup_file(const char* path, Setup& setup, Diagnostics& diagnostics) noexcept;

/// Same, from a document already in memory.
Status load_setup_string(const char* yaml, Setup& setup, Diagnostics& diagnostics) noexcept;

// ---------------------------------------------------------------------------
// machine: triggers + states
// ---------------------------------------------------------------------------

/// Reads the machine file into `model`, which is cleared on entry and left
/// empty if loading fails.
Status load_machine_file(const char* path, Model& model, Diagnostics& diagnostics) noexcept;

/// Same, from a document already in memory.
Status load_machine_string(const char* yaml, Model& model, Diagnostics& diagnostics) noexcept;

}  // namespace fms::config

#endif  // FMS_YAML_LOADER_HPP
