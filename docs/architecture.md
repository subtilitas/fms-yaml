# Architecture notes

## Two files, two objects

```
car.setup.yaml     fsm (name, initial) + io       -->  fms::Setup   where it runs
car.machine.yaml   triggers + states + guards     -->  fms::Model   what it does
                                                        |
                          StateMachine::init(model, setup)
                                                        |
                                         the one cross-file check:
                                    does the machine have that initial state?
```

The two files are loaded by separate entry points and know nothing about each
other. Each loader rejects the other's sections rather than ignoring them, so a
`states:` block that drifted into the setup file is reported with the name of the
file it belongs in, not silently dropped.

Why bother: the machine file is behaviour, and reviewing behaviour is easier
when no endpoint or client id is mixed into it. The setup file is deployment, and
swapping it lets the same machine run on a bench, in a test, or in a car —
including starting in a different state, which is how you resume after a reset or
drop a test straight into the situation it cares about.

`Setup::initial_name()` is deliberately a *name*, not an id: a file loaded on its
own cannot resolve it. `StateMachine::init` resolves it once and fails there if
the state does not exist, so a mismatched pair is caught at start-up rather than
on the first trigger.

## The configuration is a run-time input

Nothing in the build system opens the YAML. It is not copied next to the binary,
not parsed, not validated, and not a dependency of any target — the only place
either file is read is `load_setup_file` / `load_machine_file`, when the program
runs.

That is a deliberate line, and it is easy to cross by accident. An earlier
revision copied the example configs next to the binary with `configure_file`,
which also makes CMake re-run when they change: convenient, and wrong. It put the
config in the build graph, so the build had an opinion about a file that belongs
to the deployment, and a stale copy could differ from the source. Both are now
gone; the example takes paths, defaulting to the working directory.

What follows from the rule:

* a config can be edited, reviewed, signed or swapped without rebuilding
* the same binary serves every deployment - only the files differ
* a mistake surfaces at start-up, not at compile time

The last one is the price, and it is why every diagnostic carries a line number
and why the example has a `--check` mode: load both files, report what they
describe, exit. CI validates configurations by *running* that, which keeps the
check where it belongs - at run time, on the real loader - instead of duplicating
the schema in a build script.

```sh
car_console car.setup.yaml car.machine.yaml --check
```

The `car_config_check` ctest case is exactly that command.

## A second pass, over the machine rather than the text

The loader's question is whether a file can become a `Model`. It checks syntax,
schema, every name and every reference, and refuses anything that fails. What it
cannot ask is whether the machine that came out is the machine anybody wanted -
a state nothing leads to is a perfectly well-formed state.

That is a different question, asked of a different thing: the loaded `Model`,
not the text. So it lives in its own pass, `fms::lint::analyse`, in its own
target, and it never rejects anything. It appends findings to a fixed-capacity
`Report` and hands it back.

Three decisions shaped it.

**Severity belongs to the check, not to the loader.** A dead-end state is
exactly right for a terminal state and exactly wrong for one that forgot its way
back; nothing in the file distinguishes them. So the linter says which kind of
thing it found and how seriously that kind is normally taken, and the caller
decides what to do - `--check` exits 1 on an error and prints the warnings.
Making the loader refuse these would have meant refusing legitimate machines.

**Reachability ignores guards.** `fault` is often entered only when
`severity >= 2` holds. Whether that ever happens is a question about the sender,
not about the configuration: the file provides a path, so the state is
reachable. Treating an unsatisfiable guard as an unreachable state would report
one mistake as two, in two places, and the second report would move as soon as
the first was fixed.

**The guard checker proves rather than guesses.** Conditions on different
arguments cannot contradict each other, so the question is asked once per
argument: fold every condition that mentions it into a range plus the values
excluded from it by name plus any word it must or must not be, then ask whether
anything is left. `pedal > 60` with `pedal < 5` closes the range;
`gear >= 1, gear <= 1, gear != 1` empties it by exhaustion; `mode == sport` with
`mode > 2` is impossible because a numeric comparison is false unless the value
parses as an integer and a text literal never does - which is a fact about
`Condition::evaluate`, not a guess about intent. Anything it cannot prove
impossible it leaves alone. A linter that reported suspicions would be one
people learned to ignore.

Shadowing is the same argument applied to ordering: if every condition of an
earlier alternative also appears in a later one, the later one holds only when
the earlier one already did, so the earlier one wins every time. Identical
guards fall out of that as the special case they are; the interesting case is an
earlier guard that asks for *less*, which is much harder to see by eye.

One finding per alternative, and the most fundamental one wins. An alternative
behind an unguarded fallback is not also reported for its impossible guard -
fixing the reachability is what makes that guard matter again.

## The diagram is generated

A diagram drawn beside a configuration is a second description of the same
behaviour, and the first time the two disagree it is the diagram that is wrong
while still looking authoritative. `fms::diagram::render` therefore builds the
picture from the loaded `Model`, one edge per alternative.

It writes to a sink one fragment at a time rather than into a buffer. There is
no maximum diagram size that way, and nothing allocates: the console example
passes a sink that writes to stdout, the tests pass one that appends to a
string. The only escaping is per format - Mermaid reads a label as HTML, so
`pedal > 5` needs its angle bracket spelled `#gt;` or the label is swallowed as
a tag; a Dot label is a quoted string, so the quote and the backslash are what
have to be escaped.

The fallback is labelled `[otherwise]` when a trigger has more than one
alternative. File order is what makes an unguarded alternative the fallback, and
order is the one thing a picture cannot show - without the label the diagram
would suggest two edges that both always apply.

The README's diagram is the output of that renderer, spliced in by
`tools/diagram_sync.py`. The script's default is `--check`, not `--write`: CI
regenerates the picture and fails on a difference, rather than committing a new
one. A file that rewrites itself is a file whose diff nobody reads, and here the
diff - the machine changed, and this is how - is the whole point.

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

The boundary is `Runtime::start()`. Everything the run phase touches was sized at
compile time from `fms/limits.hpp` and filled during setup.

`Runtime::init` takes the machine and the port, not four separate objects: the
model and the setup come from the machine that already bound them, so there is no
way to hand the runtime a mismatched pair.

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

`StateMachine::fire()` calls that and either assigns the result to `current_` or
returns `NoTransition` / `GuardRejected`. There is still no action to dispatch, no
clock to consult and no application code in the decision — a guard is data, and
evaluating one is a map lookup plus a compare.

The cost of an input is: one `find` from channel to trigger id, one `find` from
trigger id to the alternatives, then at most `FMS_MAX_ALTERNATIVES ×
FMS_MAX_CONDITIONS_PER_GUARD` comparisons. All of it over contiguous memory.

## Guards, and why they are declarative

The obvious way to add guards is a registry of `bool(*)(const Context&)` that the
application fills in and the config refers to by name. It was rejected here for
one reason: it splits the description of the machine across two artefacts. Read
the YAML and you would still not know when `throttle_pressed` leads to
`accelerating`.

So a guard is a comparison against the arguments the trigger carried:

```yaml
throttle_pressed:
  - {when: "pedal > 5", target: accelerating}
  - {target: standing}
```

The cost of that choice is expressiveness — no arithmetic, no calls into
application state, integers and text only. The benefit is that the two files
remain the whole truth about the machine, that a guard cannot hang or crash, and
that a malformed guard is a load-time error with a line number.

Where a decision genuinely needs to look outside the trigger, the honest place
for that is the code that *sends* the trigger: read the sensor, put the value in
the argument, let the machine compare it. That keeps the machine total and
testable — `test_guards.cpp` drives every branch of the car with nothing but
strings.

Deliberate detail: a missing or unparsable argument makes a condition **false**
rather than raising an error. A guard's job is to decide; if the information it
needs is not there, it did not hold. That single rule removes a whole class of
error paths, and makes the fallback alternative the natural place to say what
should happen instead.

## Arguments cost nothing

`Args` is a `flat_map<Name, StringView>` over the port's own buffer: keys are
copied into fixed storage, values are views. Parsing `pedal=42 mode=sport` moves
no characters, and `Runtime` keeps the map as a member, so a trigger with
arguments allocates and copies exactly as much as one without - nothing.

The consequence to remember is lifetime: an `Args` is only valid while the input
it was parsed from is. That is one dispatch, which is why `Runtime` parses it,
uses it, and never stores it.

Values stay text until something asks for a number, because the machine itself
never needs one - only guards and application code do.

## Why a data-driven table instead of `etl::fsm`

`etl::fsm` models states as classes with compile-time ids, which is excellent
when the machine is known at compile time. Here the machine comes from a file, so
states cannot be types. The transition table is therefore data:

```
flat_map<StateId, StateNode>
                    └── flat_map<TriggerId, Alternatives>
                                              └── {first_condition, count, target}
vector<Condition>   the pool those slices point into
```

Both maps are ETL flat containers: a sorted vector of pointers plus a pool, so
lookup is a binary search, insertion happens only during setup, and there are no
nodes, no hashing and no allocation. A plain `flat_map` (not a multimap) is still
enough, because all the outcomes of one trigger live in one `Alternatives` vector
— which also means their order is the file's order, and guard evaluation is
deterministic.

Conditions are interned in one machine-wide pool and referenced by index. An
`Alternative` is therefore 6 bytes rather than the 136 an inlined `Condition`
would cost; embedding them would put a fully populated `Model` in the hundreds of
kilobytes.

## Why the loader is a separate target

yaml-cpp reports every problem by throwing, and the project may not use
exceptions. Rather than pretend otherwise, `src/config/yaml_loader.cpp` is built
as its own static library with `-fexceptions`, and its two entry points are
`noexcept` with a `try`/`catch(...)` around the whole body. An exception cannot
cross that boundary; callers get a `Status` and a `Diagnostics` record with a
line number.

This is enforced in CMake, not by convention: `fms_disable_exceptions()` is
applied to every target except `fms_config`.

ETL is configured consistently on both sides — `ETL_THROW_EXCEPTIONS` is never
defined, so ETL never throws anywhere, and `ETL_LOG_ERRORS` is a `PUBLIC` compile
definition so `ETL_ASSERT` expands identically in every TU (an ODR requirement).
`fms::install_etl_error_handler()` routes ETL's internal error reports to stderr
and `abort()`; they only fire on programming errors such as overrunning a fixed
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
implementation of it — the machine has no transport dependency to remove,
because it never had one. The core deals in **channels**: opaque strings that
say where input came from. It matches them and nothing else, so an MQTT topic, a
CAN identifier and a word typed on a terminal are all the same thing to it.

`Runtime` pulls rather than being called back: `service()` asks the port for
input and blocks only there. No threads, no callbacks, no queues, no clock.

## Writing a port

Eight methods, five of which have usable defaults:

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

Rules:

* No method may throw or allocate.
* `receive()` is the only call that may block, and only up to `timeout_ms`.
* `receive()` fills in an `Input`: the `channel` something arrived on, and the
  `arguments` it carried (`key=value key=value`, empty when there are none).
* Both views in that `Input` must stay valid until the next call on the port —
  the port owns the buffer, the core only reads it. `ConsolePort` points them
  into its own line buffer; `MemoryPort` into the entry it is handing out.
* Return `Status::Timeout` when nothing arrived (normal), `Status::EndOfInput`
  when the source is exhausted (`Runtime::service()` passes it up so the caller
  can leave the loop).

`io.endpoint` and `io.identity` from the setup file are there for you: a broker
URI, a serial device, a client id. The core never looks at them — it only carries
them from the file to `configure()`.

`ConsolePort` is the worked example, at about 100 lines
(`src/port/console_port.cpp`). It reads with `std::cin.getline` into a fixed
buffer — no `std::string`, no allocation — trims whitespace and a trailing CR,
and handles `help` and `quit` itself.

## Proving the allocation claim

`fms_alloc_guard` replaces global `operator new`/`delete`. `alloc_guard::arm()`
makes any subsequent allocation a counted violation (and, in fatal mode, an
`abort()` with a message on stderr).

`tests/test_no_alloc.cpp` loads a config, starts the runtime, arms the guard and
runs 1500 dispatch cycles covering routing, argument parsing, guard evaluation,
accepted triggers, rejected triggers, unknown channels, idle polls and publishing.
It asserts zero violations, and a second test allocates on purpose to prove the
guard would have noticed.

The test uses `MemoryPort` on purpose: `ConsolePort` allocates nothing itself
(`getline` into a fixed buffer), but it pulls in `<iostream>`, whose internals are
not ours to vouch for. That is exactly why it is a separate optional target and
not part of the core. `fms_core` references no allocator symbol at all.

Production builds simply do not link the guard.

## Footprint

With the default limits (32 states, 32 triggers, 8 transitions per state), on
x86-64:

| Type | Size | |
|---|---|---|
| `fms::Model` | 49 728 B | states, triggers and the condition pool |
| `fms::StateNode` | 736 B | |
| `fms::Condition` | 136 B | two fixed-size names dominate |
| `fms::Alternative` | 6 B | which is the point of interning conditions |
| `fms::Setup` | 576 B | |
| `fms::Args` | 464 B | four keys plus four views |
| `fms::Runtime` | 688 B | includes the Args and the message buffer |

Guards roughly doubled `Model`: the condition pool is
`FMS_MAX_CONDITIONS × sizeof(Condition)` (8.7 KB by default) and each trigger now
holds a vector of alternatives instead of a single id.

Tuned to the car config (`-DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
-DFMS_MAX_TRANSITIONS_PER_STATE=5 -DFMS_MAX_CHANNEL_LENGTH=31
-DFMS_MAX_CONDITIONS=16`), `fms::Model` drops to 11 328 B. Set the macros to what
the config actually needs rather than leaving the defaults — `FMS_MAX_CONDITIONS`
is the one to watch, since it is a machine-wide total.

Verifying the two hard constraints on the built artefacts:

```sh
nm -C libfms_core.a | grep -cE '__cxa_throw|_Unwind_Resume'      # 0
nm -C libfms_core.a | grep -E ' U (operator new|malloc)'         # empty
nm -C libfms_config.a | grep -cE '__cxa_throw|_Unwind_Resume'    # non-zero: the firewall
```

## Testing

| Suite | Covers |
|---|---|
| `test_state_machine.cpp` | accept, reject, capacity, duplicates, dangling references |
| `test_args.cpp` | argument parsing, typed accessors, malformed input |
| `test_condition.cpp` | guard parsing and evaluation, including what a missing argument means |
| `test_guards.cpp` | the three transition spellings, alternative order, ANDed conditions, guard-rejection reporting, and the shipped car machine driven through every branch |
| `test_setup.cpp` | the two-file split: binding, swapping setups, sections in the wrong file, a setup that does not fit its machine |
| `test_loader.cpp` | every machine-file schema and reference error, malformed YAML, oversized names |
| `test_runtime.cpp` | channel routing, error feedback, `configure()` before `open()`, trace hook, end of input |
| `test_no_alloc.cpp` | the heap trap |
| `test_lint.cpp` | every check, on machines that load without complaint, plus a full report |
| `test_diagram.cpp` | both formats, guard labels, the fallback label, and the escaping |
| `car_console_pipe` (ctest) | the example driven by a scripted session on stdin, stdout compared with `tests/car_session.expected` |
| `car_config_check` (ctest) | the shipped configuration loaded *and linted* by the real binary, via `--check` |
| `car_diagram_check` (ctest) | the README's diagram regenerated from the machine file and compared |

## Threading

Single threaded by design. `Runtime::service()` is the only place anything
happens. To raise a trigger from another thread, post it to your own queue and
drain it from the same loop — do not call `fire()` concurrently.

## Porting to a bare-metal target

1. Keep `fms_core`; it needs only `<cstdint>`, `<cstddef>`, `<cstring>` and ETL.
2. Write an `IPort` for your link (CAN, UART, shared memory).
3. Load the config on the host and ship the `Model`, or keep yaml-cpp on a target
   that can afford a heap during boot. Nothing downstream cares where the `Model`
   came from — it is a plain object. Note that shipping a pre-built `Model` is the
   one case where the config *is* read ahead of time; do it in a tool you run, not
   in the build of the firmware, or the rule above quietly stops holding.
4. Tune `fms/limits.hpp` down, then check `sizeof(fms::Model)` against your
   budget.
