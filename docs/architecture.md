# Architecture notes

## Two objects

```
car.setup.yaml     fsm (name, initial) + io       -->  fms::Setup   where it runs
car.machine.yaml   triggers + states + guards     -->  fms::Model   what it does
                                                        |
                          StateMachine::init(model, setup)
                                                        |
                                         the one cross-file check:
                                    does the machine have that initial state?
```

Separate entry points; neither file references the other. Each loader rejects
the other's sections with a diagnostic naming the file they belong in.

`Setup::initial_name()` is a name, not an id — a setup loaded on its own cannot
resolve one. `StateMachine::init` resolves it once and fails there, so a
mismatched pair is caught at start-up rather than on the first trigger.

## Configuration is a run-time input

**Invariant:** nothing in the build system opens, copies, parses, validates or
depends on either YAML file. The only readers are `load_setup_file` and
`load_machine_file`, called by the application.

Consequences:

* a config is edited, reviewed, signed or swapped without rebuilding
* one binary serves every deployment; only the files differ
* mistakes surface at start-up, so every diagnostic carries a line number and
  the example has `--check`

The `car_config_check` ctest case is `car_console <setup> <machine> --check`.

## The linter

`fms::lint::analyse(model, initial, report)` walks the loaded `Model` and
appends to a fixed-capacity `Report`. Read-only: it never rejects, never
allocates, and is safe to run on a target. Pass `kNoState` as `initial` to skip
reachability when looking at a machine file alone.

| Check | Severity | Condition |
|---|---|---|
| `unreachable-state` | error | no trigger path from `fsm.initial` |
| `unreachable-alternative` | error | follows an unguarded alternative |
| `impossible-guard` | error | the ANDed conditions cannot all hold |
| `shadowed-alternative` | error | an earlier guard holds whenever this one does |
| `dead-end-state` | warning | no transition to another state |
| `unused-trigger` | warning | declared, listed by no state |

Severity is a property of the check, not of the machine; the caller decides the
exit code. Reachability ignores guards — whether a guard ever holds is
`impossible-guard`'s question, and merging the two would report one mistake in
two places.

Guard satisfiability is decided once per argument: fold every condition naming
it into a range, the values excluded from it, and any word it must or must not
be, then test whether anything is left.

| Guard | Verdict |
|---|---|
| `pedal > 60`, `pedal < 5` | range closes |
| `gear >= 1`, `gear <= 1`, `gear != 1` | emptied by exhaustion |
| `mode == sport`, `mode > 2` | `Condition::evaluate` is false for a numeric compare unless the value parses as an integer, and a text literal never does |

Anything not provably impossible is left alone. Shadowing is the same test
applied to ordering: if every condition of an earlier alternative also appears
in a later one, the later one is dead. One finding per alternative; the most
fundamental wins.

## The diagram is generated

`fms::diagram::render` builds the picture from the loaded `Model`, one edge per
alternative, so it cannot disagree with the file it came from.

Output goes to a `Sink` one fragment at a time rather than into a buffer: no
maximum size, no allocation. Escaping is per format — Mermaid reads a label as
HTML, so `>` becomes `#gt;`; a Dot label is a quoted string, so `"` and `\` are
escaped.

An unguarded alternative among several is labelled `[otherwise]`: file order is
what makes it the fallback, and order is the one thing a picture cannot show.

`tools/diagram_sync.py` splices the output into the README. Its default is
`--check`, not `--write` — CI regenerates and fails on a difference.

## The two phases

```
                setup phase                     |            run phase
  (allocation allowed, happens once)            |   (no allocation, no exceptions)
-------------------------------------------------------------------------------------
  load_setup_file   --> yaml-cpp heap document  |   port.receive()
  load_machine_file --> copied into Setup/Model |     -> Model::find_trigger_for_channel()
                    --> documents destroyed     |       -> StateMachine::fire()
  StateMachine::init(model, setup)              |         -> port.publish_state(), or
  Runtime::init / start                         |            port.publish_error()
    (configure, open, listen)                   |
```

The boundary is `Runtime::start()`. Everything the run phase touches was sized
at compile time from `fms/limits.hpp` and filled during setup.

`Runtime::init` takes the machine and the port, not four separate objects: the
model and setup come from the machine that already bound them, so a mismatched
pair cannot be passed.

## The engine

```cpp
Decision Model::evaluate(StateId from, TriggerId trigger, const Args& args,
                         StateId& target) const {
  const auto it = state(from)->transitions.find(trigger);   // binary search
  if (it == end) return Decision::NoTransition;

  for (const Alternative& a : it->second) {                 // file order
    if (all conditions of a hold for args) {                // ANDed
      target = a.target;
      return Decision::Accepted;
    }
  }
  return Decision::GuardRejected;
}
```

`StateMachine::fire()` assigns the result to `current_` or returns
`NoTransition` / `GuardRejected`. Cost of one input: one `find` from channel to
trigger id, one from trigger id to alternatives, then at most
`FMS_MAX_ALTERNATIVES × FMS_MAX_CONDITIONS_PER_GUARD` comparisons, all over
contiguous memory.

## Guards

A guard is a comparison against the arguments the trigger carried — integers
and text, no arithmetic, no calls into application state, so the YAML alone says
when `throttle_pressed` leads to `accelerating`. Where a decision needs outside
state, the sender reads it and puts the value in an argument.

**A missing or unparsable argument makes a condition false, never an error.**
That removes a class of error paths and makes the fallback alternative the place
to say what happens instead.

## Arguments

`Args` is a `flat_map<Name, StringView>` over the port's buffer: keys copied
into fixed storage, values are views. Parsing `pedal=42 mode=sport` moves no
characters, and `Runtime` holds the map as a member, so a trigger with
arguments costs exactly as much as one without — nothing.

Lifetime: an `Args` is valid only while the input it was parsed from is. That
is one dispatch, which is why `Runtime` parses it, uses it, and never stores it.

## The transition table

The machine comes from a file, so states cannot be types — which rules out
`etl::fsm` — and the table is data:

```
flat_map<StateId, StateNode>
                    └── flat_map<TriggerId, Alternatives>
                                              └── {first_condition, count, target}
vector<Condition>   the pool those slices point into
```

ETL flat containers throughout: a sorted vector plus a pool, so lookup is a
binary search, insertion happens only during setup, and there are no nodes, no
hashing and no allocation. A plain `flat_map` suffices because all outcomes of
one trigger live in one `Alternatives` vector, which also makes their order the
file's order. Conditions are interned and referenced by index, so an
`Alternative` is 6 bytes rather than the 136 an inlined `Condition` would cost.

## The exception firewall

yaml-cpp reports every problem by throwing, and the project may not use
exceptions. `src/config/yaml_loader.cpp` is therefore its own static library
built with `-fexceptions`, and its entry points are `noexcept` with
`try`/`catch(...)` around the whole body. Callers get a `Status` and a
`Diagnostics` record with a line number.

Enforced in CMake, not by convention: `fms_disable_exceptions()` is applied to
every target except `fms_config`.

ETL is configured consistently on both sides. `ETL_THROW_EXCEPTIONS` is never
defined, so ETL never throws; `ETL_LOG_ERRORS` is a `PUBLIC` compile definition
so `ETL_ASSERT` expands identically in every TU, which is an ODR requirement.
`fms::install_etl_error_handler()` routes ETL's error reports to stderr and
`abort()`; they fire only on programming errors such as overrunning a fixed
container.

## The port seam

```
        Runtime
           |  (one virtual call to read, one to publish)
        fms::IPort
       /     |      \
ConsolePort  |    MemoryPort        …and whatever you write
 (fms_console)      (tests)
```

`IPort` is the only virtual interface in the system, and `fms_core` contains no
implementation of it. The core deals in **channels**: opaque strings it matches
and never interprets, so an MQTT topic, a CAN identifier and a word typed on a
terminal are the same thing to it.

`Runtime` pulls rather than being called back: `service()` asks the port for
input and blocks only there. No threads, no callbacks, no queues, no clock.

## Writing a port

Eight methods, five with usable defaults:

| Method | Called | Must do |
|---|---|---|
| `configure(io)` | once, from `start()`, before `open()` | keep what you need from the setup file's `io` block |
| `open()` | once, from `start()` | connect / open the device |
| `listen(channel)` | once per trigger, from `start()` | subscribe, or ignore it |
| `receive(input, timeout_ms)` | every `service()` | block up to the timeout; fill in `input` |
| `publish_state(state)` | on every state change | announce it |
| `publish_error(message)` | on a refused trigger or unknown channel | report it |
| `close()` | from `stop()` | disconnect |
| `last_error()` | after a failure, by the caller | describe it; never return null |

Contract:

* No method may throw or allocate.
* `receive()` is the only call that may block, and only up to `timeout_ms`.
* `receive()` fills in an `Input`: the `channel` something arrived on and the
  `arguments` it carried (`key=value key=value`, empty when there are none).
* Both views must stay valid until the next call on the port — the port owns
  the buffer, the core only reads it. `ConsolePort` points them into its line
  buffer; `MemoryPort` into the entry it is handing out.
* Return `Status::Timeout` when nothing arrived (normal) and
  `Status::EndOfInput` when the source is exhausted; `Runtime::service()`
  passes the latter up so the caller can leave the loop.

`io.endpoint` and `io.identity` are carried from the file to `configure()`
untouched. `ConsolePort` (`src/port/console_port.cpp`) is the worked example:
`std::cin.getline` into a fixed buffer, no `std::string`, no allocation.

## Proving the allocation claim

`fms_alloc_guard` replaces global `operator new`/`delete`. `alloc_guard::arm()`
makes any subsequent allocation a counted violation, and in fatal mode an
`abort()` with a message on stderr.

`tests/test_no_alloc.cpp` loads a config, starts the runtime, arms the guard and
runs 1500 service calls covering routing, accepted and rejected triggers,
unknown channels, idle polls and publishing. It asserts zero violations; a
second test allocates on purpose to prove the guard would have noticed.

Not covered: the machine it drives is unguarded and its input carries no
arguments, so `Args::parse` and `Condition::evaluate` never execute under the
armed guard. Both are allocation-free by construction — a fixed-capacity map of
views, and a compare — but neither is *proven* so here.

The test uses `MemoryPort` deliberately. `ConsolePort` allocates nothing itself
but pulls in `<iostream>`, whose internals are not ours to vouch for; that is
why it is a separate optional target. `fms_core` references no allocator symbol
at all. Production builds do not link the guard.

## Footprint

Default limits (32 states, 32 triggers, 8 transitions per state), x86-64:

| Type | Size | |
|---|---|---|
| `fms::Model` | 49 728 B | states, triggers and the condition pool |
| `fms::StateNode` | 736 B | |
| `fms::Condition` | 136 B | two fixed-size names dominate |
| `fms::Alternative` | 6 B | which is the point of interning conditions |
| `fms::Setup` | 576 B | |
| `fms::Args` | 464 B | four keys plus four views |
| `fms::Runtime` | 688 B | includes the Args and the message buffer |

The condition pool is `FMS_MAX_CONDITIONS × sizeof(Condition)`, 8.7 KB by
default, and each trigger holds a vector of alternatives rather than a single
id — together these roughly doubled `Model` when guards were added.
`FMS_MAX_CONDITIONS` is the one to watch, being a machine-wide total.

Tuned to the car config (`-DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
-DFMS_MAX_TRANSITIONS_PER_STATE=5 -DFMS_MAX_CHANNEL_LENGTH=31
-DFMS_MAX_CONDITIONS=16`), `fms::Model` is 11 328 B.

Verifying the two hard constraints on the built artefacts:

```sh
nm -C libfms_core.a | grep -cE '__cxa_throw|_Unwind_Resume'      # 0
nm -C libfms_core.a | grep -E ' U (operator new|malloc)'         # empty
nm -C libfms_config.a | grep -cE '__cxa_throw|_Unwind_Resume'    # non-zero: the firewall
```

## The capacity guard

The capacities are template arguments of the containers inside `Model`, `Setup`,
`Args`, `Runtime` and `lint::Report`, so they decide the layout of those types.
Two translation units that disagree about one see different types under the same
names, and the link succeeds:

```
sizeof(fms::Model)   49 728 B   defaults
sizeof(fms::Model)   21 120 B   FMS_MAX_STATES=8 FMS_MAX_TRIGGERS=12
```

The library then writes 49 728 bytes into an object the caller reserved 21 120
for. Nothing in the type system, the compiler or the linker objects.

Two things prevent it. Inside the build, a capacity is a cache variable that
becomes a `PUBLIC` compile definition on `fms_core`, so `cmake -DFMS_MAX_STATES=8`
reaches every target that links it — the same treatment `ETL_LOG_ERRORS` gets,
for the same reason. Outside the build, `fms/abi.hpp` pastes all eleven
capacities into the name of a symbol that `fms_core` defines once and the
constructors of `Model` and `Setup` reference:

```
undefined reference to `fms_abi_8_12_8_4_3_64_4_31_95_127_32'
```

The name of the missing symbol is the configuration the caller compiled with,
and the one `fms_core` defines is the configuration it was built with.

The guard fires when a `Model` or a `Setup` is constructed, which every program
using the library does. A translation unit that only declares a reference or a
pointer to one constructs nothing and is not covered, and neither is a mismatch
in `FMS_MAX_FINDINGS` alone in a program that lints without loading — the
constructor is the hook, so something has to be constructed.

`tools/abi_guard_check.sh` is the gate, run by the `abi_guard` test. It checks
that every `FMS_MAX_*` in `limits.hpp` appears in the tag, that a probe built
with the library's own capacities links and runs, and that one built with a
changed capacity does not link and says which symbol it could not resolve. The
changed capacity is derived from what the probe reports, so the gate holds for a
tuned tree as well as for the defaults. GCC and Clang only: it is a statement
about linking, and one toolchain proving it is enough.

## Testing

| Suite | Covers |
|---|---|
| `test_state_machine.cpp` | accept, reject, capacity, duplicates, dangling references |
| `test_args.cpp` | argument parsing, typed accessors, malformed input |
| `test_condition.cpp` | guard parsing and evaluation, including a missing argument |
| `test_guards.cpp` | the three transition spellings, alternative order, ANDed conditions, guard-rejection reporting, the shipped car machine through every branch |
| `test_setup.cpp` | binding, swapping setups, sections in the wrong file, a setup that does not fit its machine |
| `test_loader.cpp` | every machine-file schema and reference error, malformed YAML, oversized names |
| `test_runtime.cpp` | channel routing, error feedback, `configure()` before `open()`, trace hook, end of input |
| `test_no_alloc.cpp` | the heap trap |
| `test_lint.cpp` | every check, on machines that load without complaint, plus a full report |
| `test_diagram.cpp` | both formats, guard labels, the fallback label, escaping |
| `test_abi.cpp` | the capacity tag lists every capacity, in the order the symbol name pastes them |
| `car_console_pipe` (ctest) | the example driven by a scripted session on stdin, stdout compared with `tests/car_session.expected` |
| `car_config_check` (ctest) | the shipped configuration loaded *and linted* by the real binary |
| `car_diagram_check` (ctest) | the README's diagram regenerated from the machine file and compared |
| `abi_guard` (ctest) | a probe compiled with a changed capacity fails to link, and one with the library's own capacities does not |

## Threading

Single threaded by design. `Runtime::service()` is the only place anything
happens. To raise a trigger from another thread, post it to your own queue and
drain it from the same loop — do not call `fire()` concurrently.

## Porting to a bare-metal target

1. Keep `fms_core`; it needs only `<cstdint>`, `<cstddef>`, `<cstring>` and ETL.
2. Write an `IPort` for your link (CAN, UART, shared memory).
3. Load the config on the host and ship the `Model`, or keep yaml-cpp on a
   target that can afford a heap during boot. Nothing downstream cares where the
   `Model` came from. Shipping a pre-built `Model` is the one case where the
   config is read ahead of time; do it in a tool you run, not in the firmware
   build, or the invariant above quietly stops holding.
4. Tune `fms/limits.hpp` down, then check `sizeof(fms::Model)` against your
   budget.
