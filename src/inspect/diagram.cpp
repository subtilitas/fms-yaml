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
void emit_label(const Out& out, const Model& model, TriggerId trigger,
                const Alternative& alternative, bool has_siblings, Format format) noexcept {
  emit_escaped(out, cstr(model.trigger_name(trigger)), format);

  if (alternative.condition_count == 0) {
    if (has_siblings) {
      out((format == Format::Mermaid) ? " " : "\\n");
      out("[otherwise]");
    }
    return;
  }

  out((format == Format::Mermaid) ? " " : "\\n");
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

void render_mermaid(const Out& out, const Model& model, StateId initial) noexcept {
  out("stateDiagram-v2\n");

  if (initial != kNoState && model.has_state(initial)) {
    out("    [*] --> ");
    emit_escaped(out, cstr(model.state_name(initial)), Format::Mermaid);
    out("\n");
  }

  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        out("    ");
        emit_escaped(out, cstr(model.state_name(entry.first)), Format::Mermaid);
        out(" --> ");
        emit_escaped(out, cstr(model.state_name(alternative.target)), Format::Mermaid);
        out(": ");
        emit_label(out, model, transition.first, alternative, has_siblings, Format::Mermaid);
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
  out("  node [shape=box, style=rounded, fontname=\"sans-serif\"];\n");
  out("  edge [fontname=\"sans-serif\", fontsize=10];\n");

  if (initial != kNoState && model.has_state(initial)) {
    out("  __start [shape=point, width=0.12, label=\"\"];\n");
    out("  __start -> \"");
    emit_escaped(out, cstr(model.state_name(initial)), Format::Dot);
    out("\";\n");
  }

  for (const auto& entry : model.states()) {
    for (const auto& transition : entry.second.transitions) {
      const bool has_siblings = transition.second.size() > 1;
      for (const Alternative& alternative : transition.second) {
        out("  \"");
        emit_escaped(out, cstr(model.state_name(entry.first)), Format::Dot);
        out("\" -> \"");
        emit_escaped(out, cstr(model.state_name(alternative.target)), Format::Dot);
        out("\" [label=\"");
        emit_label(out, model, transition.first, alternative, has_siblings, Format::Dot);
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
