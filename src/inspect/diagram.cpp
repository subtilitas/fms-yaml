// SPDX-License-Identifier: MIT
#include "fms/inspect/diagram.hpp"

#include "text.hpp"

namespace fms::diagram {
namespace {

using inspect::append_condition;
using inspect::cstr;

/// The sink, bound to its user pointer, so emitting a fragment reads as one
/// call instead of three arguments repeated across the file.
class Out {
 public:
  Out(Sink sink, void* user) noexcept : sink_(sink), user_(user) {}

  void operator()(StringView text) const noexcept {
    if (!text.empty()) {
      sink_(user_, text);
    }
  }
  void operator()(const char* text) const noexcept { (*this)(cstr(text)); }

 private:
  Sink  sink_;
  void* user_;
};

/// Emits text with whatever the target format cannot take literally.
///
/// Mermaid reads a label as HTML, so an angle bracket in `pedal > 5` would be
/// swallowed as a tag; its own entity escapes survive that.  Dot labels are
/// quoted strings, so the quote and the backslash are what have to be spelled
/// out.  Everything else in a name or a guard is already safe in both.
void emit_escaped(const Out& out, StringView text, Format format) noexcept {
  std::size_t run = 0;  // start of the stretch that needs no escaping

  for (std::size_t i = 0; i < text.size(); ++i) {
    const char  symbol      = text[i];
    const char* replacement = nullptr;

    if (format == Format::Mermaid) {
      if (symbol == '<') {
        replacement = "#lt;";
      } else if (symbol == '>') {
        replacement = "#gt;";
      } else if (symbol == '"') {
        replacement = "#quot;";
      }
    } else {
      if (symbol == '"') {
        replacement = "\\\"";
      } else if (symbol == '\\') {
        replacement = "\\\\";
      }
    }

    if (replacement != nullptr) {
      out(StringView(text.data() + run, i - run));
      out(replacement);
      run = i + 1;
    }
  }
  out(StringView(text.data() + run, text.size() - run));
}

/// `trigger`, then the guard it carries.  A fallback among several
/// alternatives is named as one: the file's order is what makes it the
/// fallback, and order is the one thing a diagram cannot show.
/// `separator` goes between the trigger and its guard: a space, or a line
/// break in a Dot edge label.
void emit_label(const Out& out, const Model& model, TriggerId trigger,
                const Alternative& alternative, bool has_siblings, Format format,
                const char* separator) noexcept {
  emit_escaped(out, cstr(model.trigger_name(trigger)), format);

  if (alternative.condition_count == 0) {
    if (has_siblings) {
      out(separator);
      out("[otherwise]");
    }
    return;
  }

  out(separator);
  out("[");

  const Model::ConditionPool& pool = model.conditions();
  for (std::uint8_t i = 0; i < alternative.condition_count; ++i) {
    const std::size_t index = static_cast<std::size_t>(alternative.first_condition) + i;
    if (index >= pool.size()) {
      break;
    }
    if (i > 0) {
      out(" and ");
    }
    // Fixed capacity, and a condition cannot outgrow it: two names and an
    // operator.  Built first so it can be escaped as one piece of text.
    Message text;
    append_condition(text, pool[index]);
    emit_escaped(out, view(text), format);
  }
  out("]");
}

/// True when at least one state belongs to `group`.  A group without members
/// has nothing to draw a box around, so it is left out of the picture.
bool has_members(const Model& model, GroupId group) noexcept {
  for (const auto& entry : model.states()) {
    if (entry.second.group == group) {
      return true;
    }
  }
  return false;
}

/// Where an edge from `from` ends: kSelfState is `from` itself.
StateId resolve(StateId from, const Alternative& alternative) noexcept {
  return (alternative.target == kSelfState) ? from : alternative.target;
}

void render_mermaid(const Out& out, const Model& model, StateId initial) noexcept {
  out("stateDiagram-v2\n");

  if (initial != kNoState && model.has_state(initial)) {
    out("    [*] --> ");
    emit_escaped(out, cstr(model.state_name(initial)), Format::Mermaid);
    out("\n");
  }

  // Each group is a composite state holding its members.  Only the membership
  // goes inside the block; every edge is written at the top level, which is
  // where Mermaid draws an edge between a member and a state outside it.
  for (const auto& group : model.groups()) {
    if (!has_members(model, group.first)) {
      continue;
    }
    out("    state ");
    emit_escaped(out, view(group.second.name), Format::Mermaid);
    out(" {\n");
    for (const auto& entry : model.states()) {
      if (entry.second.group == group.first) {
        out("        ");
        emit_escaped(out, view(entry.second.name), Format::Mermaid);
        out("\n");
      }
    }
    out("    }\n");
  }

  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        out("    ");
        emit_escaped(out, cstr(model.state_name(entry.first)), Format::Mermaid);
        out(" --> ");
        emit_escaped(out, cstr(model.state_name(resolve(entry.first, alternative))),
                     Format::Mermaid);
        out(": ");
        emit_label(out, model, transition.first, alternative, has_siblings, Format::Mermaid, " ");
        out("\n");
      }
    }
  }

  // A group's transition leaves the box once, for every member.  `~self` is a
  // loop on the box: a group is never entered, so the loop can only mean that
  // each member stays where it is.
  for (const auto& group : model.groups()) {
    if (!has_members(model, group.first)) {
      continue;
    }
    for (const auto& transition : group.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        out("    ");
        emit_escaped(out, view(group.second.name), Format::Mermaid);
        out(" --> ");
        emit_escaped(out,
                     (alternative.target == kSelfState)
                         ? view(group.second.name)
                         : cstr(model.state_name(alternative.target)),
                     Format::Mermaid);
        out(": ");
        emit_label(out, model, transition.first, alternative, has_siblings, Format::Mermaid, " ");
        out("\n");
      }
    }
  }
}

void render_dot(const Out& out, const Model& model, StateId initial) noexcept {
  out("digraph \"");
  emit_escaped(out, model.name().empty() ? cstr("machine") : view(model.name()), Format::Dot);
  out("\" {\n");
  out("  rankdir=LR;\n");

  bool clustered = false;
  for (const auto& group : model.groups()) {
    clustered = clustered || has_members(model, group.first);
  }
  if (clustered) {
    out("  compound=true;\n");  // lets an edge start at a cluster's border
  }
  out("  node [shape=box, style=rounded, fontname=\"sans-serif\"];\n");
  out("  edge [fontname=\"sans-serif\", fontsize=10];\n");

  if (initial != kNoState && model.has_state(initial)) {
    out("  __start [shape=point, width=0.12, label=\"\"];\n");
    out("  __start -> \"");
    emit_escaped(out, cstr(model.state_name(initial)), Format::Dot);
    out("\";\n");
  }

  // A group is a cluster.  Graphviz cannot draw an edge from a cluster to
  // itself, so a `~self` transition goes into the cluster's label instead, one
  // line each, the way UML writes a transition that does not leave the state.
  for (const auto& group : model.groups()) {
    if (!has_members(model, group.first)) {
      continue;
    }
    out("  subgraph \"cluster_");
    emit_escaped(out, view(group.second.name), Format::Dot);
    out("\" {\n    label=\"");
    emit_escaped(out, view(group.second.name), Format::Dot);
    for (const auto& transition : group.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        if (alternative.target == kSelfState) {
          out("\\n");
          emit_label(out, model, transition.first, alternative, has_siblings, Format::Dot, " ");
          out(" / stay");
        }
      }
    }
    out("\";\n    style=rounded;\n");
    for (const auto& entry : model.states()) {
      if (entry.second.group == group.first) {
        out("    \"");
        emit_escaped(out, view(entry.second.name), Format::Dot);
        out("\";\n");
      }
    }
    out("  }\n");
  }

  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        out("  \"");
        emit_escaped(out, cstr(model.state_name(entry.first)), Format::Dot);
        out("\" -> \"");
        emit_escaped(out, cstr(model.state_name(resolve(entry.first, alternative))), Format::Dot);
        out("\" [label=\"");
        emit_label(out, model, transition.first, alternative, has_siblings, Format::Dot, "\\n");
        out("\"];\n");
      }
    }
  }

  // An edge leaving a cluster is drawn from one member with `ltail`, which
  // clips it at the cluster's border.  Which member does not matter; the first
  // is used.
  for (const auto& group : model.groups()) {
    StateId anchor = kNoState;
    for (const auto& entry : model.states()) {
      if (entry.second.group == group.first) {
        anchor = entry.first;
        break;
      }
    }
    if (anchor == kNoState) {
      continue;
    }
    for (const auto& transition : group.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        if (alternative.target == kSelfState) {
          continue;  // in the cluster's label
        }
        out("  \"");
        emit_escaped(out, cstr(model.state_name(anchor)), Format::Dot);
        out("\" -> \"");
        emit_escaped(out, cstr(model.state_name(alternative.target)), Format::Dot);
        out("\" [ltail=\"cluster_");
        emit_escaped(out, view(group.second.name), Format::Dot);
        out("\", label=\"");
        emit_label(out, model, transition.first, alternative, has_siblings, Format::Dot, "\\n");
        out("\"];\n");
      }
    }
  }
  out("}\n");
}

}  // namespace

Status render(const Model& model, StateId initial, Format format, Sink sink,
              void* user) noexcept {
  if (sink == nullptr) {
    return Status::InvalidArgument;
  }
  const Out out(sink, user);

  if (format == Format::Mermaid) {
    render_mermaid(out, model, initial);
  } else {
    render_dot(out, model, initial);
  }
  return Status::Ok;
}

bool parse_format(StringView name, Format& out) noexcept {
  if (name == StringView("mermaid", 7)) {
    out = Format::Mermaid;
    return true;
  }
  if (name == StringView("dot", 3)) {
    out = Format::Dot;
    return true;
  }
  return false;
}

}  // namespace fms::diagram
