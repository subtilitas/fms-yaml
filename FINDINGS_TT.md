# Torture test findings

Results of an adversarial test suite run against this library. The suite is
kept outside the repository and is not part of CI. No fix in this file is
applied to the tree.

Commit tested: `abc58d3`, branch `main`.
Build: GCC 13.3.0, C++17, Debug, `-fsanitize=address,undefined`, LeakSanitizer
on.

3,391,096 checks, 0 check failures. One defect, in the file-loading path.

---

## 1. 2,048 bytes leak per config load from a path that opens but cannot be read

Location: `src/config/yaml_loader.cpp:520` (`load_setup_file`) and `:542`
(`load_machine_file`), through `YAML::LoadFile`.
Root cause: yaml-cpp 0.8.0, `src/stream.cpp:190`.
Severity: low for a loader that runs once at start-up; unbounded for one that
retries.

The leak fires only when the path opens and then fails to read. A path that
does not exist, an empty file and a malformed but readable file are all clean:

| Entry point | Path | Status returned | Leak |
|---|---|---|---|
| `load_machine_file` | a path that does not exist | `config file not found` | none |
| `load_machine_file` | a directory | `YAML parse error` | 2,048 bytes |
| `load_machine_file` | `/dev/null` | `config does not match the schema` | none |
| `load_machine_file` | `""` | `config file not found` | none |
| `load_machine_file` | `/proc/self/mem` | `YAML parse error` | 2,048 bytes |
| `load_machine_file` | a valid file | `ok` | none |
| `load_machine_file` | a malformed file | `YAML parse error` | none |
| `load_setup_file` | a malformed file | `YAML parse error` | none |
| `load_machine_string` | any input | various | none |

`YAML::Stream`'s constructor allocates its prefetch buffer in the
member-initializer list:

```cpp
Stream::Stream(std::istream& input)
    : m_input(input),
      ...
      m_pPrefetched(new unsigned char[YAML_PREFETCH_SIZE]),   // 2048 bytes
      ...
{
  if (!input) return;
  ... reads from input to detect the byte order mark ...
}
```

`m_pPrefetched` is a raw pointer. When the byte-order-mark read in the
constructor body throws — which a directory or an unreadable file produces —
the constructor does not complete, `~Stream` never runs, and the buffer is
never freed. The `guarded()` wrapper catches the exception and returns
`Status::ParseError` correctly; the memory is gone by then.

Loading configuration is the one phase this library documents as allocating,
and everything after it is guarded. A program that loads once and exits on
failure loses 2 KB. A supervisor that retries — waiting for removable media
whose mount point is a directory until the medium appears — leaks 2 KB per
attempt with nothing to reclaim it.

Two fixes, neither applied. Upstream: make `m_pPrefetched` a
`std::unique_ptr<unsigned char[]>`. Here: check that the path names a regular,
readable file before calling `YAML::LoadFile`, which also turns two of the rows
above from `YAML parse error` into the more accurate `Status::FileNotFound`.

---

## What held

- **The no-allocation claim.** `fms_alloc_guard` armed after `Runtime::start()`
  records 0 allocations across 2,000 rounds driving every path the run phase
  has: accepted transitions, guards that hold, guards that do not hold,
  unparsable argument values, unknown channels, malformed argument text,
  duplicate keys, and argument lists past `FMS_MAX_ARGUMENTS` — plus 20,000
  rounds of `StateMachine::fire` directly. The guard is checked to be
  non-vacuous first: a deliberate `new` inside an armed region is caught.
- **The state machine against a shadow model.** 3,000 randomly generated
  models, 40 triggers fired at each, compared against an independent
  implementation in ordinary containers. Every `fire()` status matches, every
  target matches, a rejected trigger never changes the state, and
  `transition_count() + rejection_count()` equals the number of `fire()` calls
  in every run.
- **YAML bombs.** A document nested 100,000 levels deep is a parse error, not a
  stack overflow — yaml-cpp caps its own recursion. A six-level nested alias
  expansion is rejected. A 50,000-trigger document returns `CapacityExceeded`
  with the model left empty.
- **Every compile-time ceiling.** States, triggers, transitions per state,
  alternatives per trigger, conditions per guard, name length and channel
  length were each loaded at exactly the limit and one past it. All accept at
  the limit and return the documented status one past it, and a failed load
  always leaves the `Model` and `Setup` empty rather than half-built.
- **`parse_int`.** Agrees with `strtoll` plus the documented rules over the
  int32 boundaries spelled every way and 200,000 random strings. `2147483648`,
  `-2147483649` and a twenty-digit number are refused; `-2147483648` is
  accepted; a failed parse never writes to its output.
- **Guards are total.** Over 200,000 randomized condition and argument pairs,
  `Condition::evaluate` returns the same answer twice for the same input and
  never holds when the argument it names is absent, including for `!=` where
  treating a missing value as a value would make it true.
- **Argument views.** Every value `Args` hands back points inside the text it
  parsed, checked with that text placed at the end of a heap allocation so a
  read past it is a hard failure.

## Case pinned after a wrong expectation

One assertion in the suite was wrong before this library was: a single `=` in a
guard reads as equality, which `find_operator` states outright. It is now a
test of that behaviour, alongside the longest-match rule that keeps `<=` from
reading as `<`.
