// SPDX-License-Identifier: MIT
//
// The deployment half of the configuration: everything that is *not* states and
// triggers.  It says what this instance is called, where it starts, and how it
// talks to the world.
//
// Keeping it apart from the Model means one machine definition can be deployed
// several times - a test rig, a bench, a car - by swapping setup files, without
// touching the file that describes the behaviour.  It also means the machine
// file can be reviewed as behaviour alone, with no endpoints in it.
#ifndef FMS_SETUP_HPP
#define FMS_SETUP_HPP

#include "fms/io_config.hpp"
#include "fms/model.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms {

class Setup {
 public:
  Setup() noexcept = default;

  Setup(const Setup&) = delete;
  Setup& operator=(const Setup&) = delete;

  void clear() noexcept;

  Status set_name(StringView name) noexcept;
  Status set_initial(StringView state_name) noexcept;

  IoConfig&       mutable_io() noexcept { return io_; }
  const IoConfig& io() const noexcept { return io_; }

  const Name& name() const noexcept { return name_; }

  /// The *name* of the initial state.  It is only a string here: the setup file
  /// is loaded on its own and knows nothing about which states exist.
  const Name& initial_name() const noexcept { return initial_; }

  /// Looks the initial state up in `model`.  Returns kNoState when the machine
  /// has no such state, which is the one cross-file check that matters.
  StateId initial_in(const Model& model) const noexcept;

  /// Ok, or UnknownState / SchemaError describing why this setup cannot drive
  /// this model.  Called by StateMachine::init.
  Status validate_against(const Model& model) const noexcept;

 private:
  Name     name_{};
  Name     initial_{};
  IoConfig io_{};
};

}  // namespace fms

#endif  // FMS_SETUP_HPP
