// SPDX-License-Identifier: MIT
//
// The machine as a picture, generated from the machine.
//
// A hand-drawn diagram is a second description of the behaviour, and the
// moment the YAML changes it becomes a wrong one.  This renders the diagram
// from the loaded Model instead, so it cannot disagree with the file it came
// from - and a committed diagram can be checked for drift by regenerating it
// rather than by reading it.
//
// Two formats, both plain text:
//
//   Mermaid   stateDiagram-v2, which GitHub renders inline in Markdown
//   Dot       graphviz, for `dot -Tsvg`
//
// Output goes to a sink one fragment at a time rather than into a buffer, so
// there is no maximum diagram size and nothing here allocates.  A caller that
// wants the whole thing in memory appends to a string of its own; the console
// example just writes each fragment to stdout.
#ifndef FMS_INSPECT_DIAGRAM_HPP
#define FMS_INSPECT_DIAGRAM_HPP

#include <cstdint>

#include "fms/model.hpp"
#include "fms/status.hpp"
#include "fms/types.hpp"

namespace fms::diagram {

enum class Format : std::uint8_t {
  Mermaid,  ///< stateDiagram-v2
  Dot,      ///< graphviz digraph
};

/// Receives the rendered text in pieces, in order.  Never called with an empty
/// view.  It must not throw; whether it may allocate is the caller's business.
using Sink = void (*)(void* user, StringView text);

/// Renders `model`.  `initial` draws the entry arrow and may be `kNoState`
/// when a machine file is being looked at on its own.
///
/// Every alternative becomes one edge, labelled with its trigger and, where it
/// has one, its guard.  An unguarded alternative among several is labelled
/// `[otherwise]`, because on the page the file's ordering is the only thing
/// that says which edge is the fallback.
///
///   Status::Ok               rendered
///   Status::InvalidArgument  no sink
Status render(const Model& model, StateId initial, Format format, Sink sink,
              void* user) noexcept;

/// Parses a format name: "mermaid" or "dot".  False when it is neither.
bool parse_format(StringView name, Format& out) noexcept;

}  // namespace fms::diagram

#endif  // FMS_INSPECT_DIAGRAM_HPP
