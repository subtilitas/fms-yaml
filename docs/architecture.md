# Architecture notes

## The two phases

```
                setup phase                     |            run phase
  (allocation allowed, happens once)            |   (no allocation, no exceptions)
-------------------------------------------------------------------------------------
  config::load_file --> yaml-cpp heap document  |   port.receive()
                    --> copied into Model       |     -> Model::find_trigger_for_channel()
                    --> document destroyed      |       -> StateMachine::fire()
  StateMachine::init                            |         -> port.publish_state(), or
  Runtime::init / start (open, listen)          |            port.publish_error()
```

The boundary is `Runtime::start()`. Everything the run phase touches was sized at
compile time from `fms/limits.hpp` and filled during setup.

## The engine

```cpp
StateId Model::target_of(StateId from, TriggerId trigger) const {
  const StateNode* node = state(from);
  const auto it = node->transitions.find(trigger);   // binary search
  return (it == node->transitions.end()) ? kNoState : it->second;
}
```

`StateMachine::fire()` calls that, and either assigns the result to `current_` or
returns `Status::NoTransition`. There is no other decision anywhere: no guard to
evaluate, no action to dispatch, no payload to parse, no clock to consult. That
is deliberate — a state machine you can hold in your head is one you can trust in
a car.

The cost of an input is two `flat_map::find` calls: channel to trigger id, then
trigger id to next state. Both are binary searches over contiguous memory.

## Why a data-driven table instead of `etl::fsm`

`etl::fsm` models states as classes with compile-time ids, which is excellent
when the machine is known at compile time. Here the machine comes from a file, so
states cannot be types. The transition table is therefore data:

```
flat_map<StateId, StateNode>
                    └── flat_map<TriggerId, StateId>
```

Both are ETL flat containers: a sorted vector of pointers plus a pool, so lookup
is a binary search, insertion happens only during setup, and there are no nodes,
no hashing and no allocation. A plain `flat_map` (not a multimap) is enough
because a trigger has exactly one meaning in a given state.

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

Six methods, four of which have usable defaults:

| Method | Called | Must do |
|---|---|---|
| `open()` | once, from `start()` | connect / open the device |
| `listen(channel)` | once per trigger, from `start()` | subscribe, or ignore it |
| `receive(channel, timeout_ms)` | every `service()` | block up to the timeout; set `channel` |
| `publish_state(state)` | on every state change | announce it |
| `publish_error(message)` | on a refused trigger or unknown channel | report it |
| `close()` | from `stop()` | disconnect |

Rules:

* No method may throw or allocate.
* `receive()` is the only call that may block, and only up to `timeout_ms`.
* The view handed to `receive()` must stay valid until the next call on the port
  — the port owns the buffer, the core only reads it. `ConsolePort` returns a
  view into its own line buffer; `MemoryPort` into a member `Channel`.
* Return `Status::Timeout` when nothing arrived (normal), `Status::EndOfInput`
  when the source is exhausted (`Runtime::service()` passes it up so the caller
  can leave the loop).

`io.endpoint` and `io.identity` from the config are there for you: a broker URI,
a serial device, a client id. The core never looks at them.

`ConsolePort` is the worked example, at about 100 lines
(`src/port/console_port.cpp`). It reads with `std::cin.getline` into a fixed
buffer — no `std::string`, no allocation — trims whitespace and a trailing CR,
and handles `help` and `quit` itself.

## Proving the allocation claim

`fms_alloc_guard` replaces global `operator new`/`delete`. `alloc_guard::arm()`
makes any subsequent allocation a counted violation (and, in fatal mode, an
`abort()` with a message on stderr).

`tests/test_no_alloc.cpp` loads a config, starts the runtime, arms the guard and
runs 1500 dispatch cycles covering routing, accepted triggers, rejected triggers,
unknown channels, idle polls and publishing. It asserts zero violations, and a
second test allocates on purpose to prove the guard would have noticed.

The test uses `MemoryPort` on purpose. `fms_core` references no allocator symbol
at all, but `fms_console` does reference `operator delete` — that comes from
`<iostream>`, which is exactly why the console port is a separate optional
target and not part of the core.

Production builds simply do not link the guard.

## Footprint

With the default limits (32 states, 32 triggers, 8 transitions per state), on
x86-64:

| Type | Size |
|---|---|
| `fms::Model` | 27 112 B |
| `fms::StateNode` | 288 B |
| `fms::StateMachine` | 24 B |
| `fms::Runtime` | 216 B |

Tuned to the car config (`-DFMS_MAX_STATES=8 -DFMS_MAX_TRIGGERS=12
-DFMS_MAX_TRANSITIONS_PER_STATE=5 -DFMS_MAX_CHANNEL_LENGTH=31`), `fms::Model`
drops to 7 144 B. Set the macros to what the config actually needs rather than
leaving the defaults.

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
| `test_loader.cpp` | every schema and reference error, malformed YAML, oversized names |
| `test_runtime.cpp` | channel routing, error feedback, trace hook, end of input |
| `test_no_alloc.cpp` | the heap trap |
| `car_console_pipe` (ctest) | the example driven by a scripted session on stdin, stdout compared with `tests/car_session.expected` |

## Threading

Single threaded by design. `Runtime::service()` is the only place anything
happens. To raise a trigger from another thread, post it to your own queue and
drain it from the same loop — do not call `fire()` concurrently.

## Porting to a bare-metal target

1. Keep `fms_core`; it needs only `<cstdint>`, `<cstddef>`, `<cstring>` and ETL.
2. Write an `IPort` for your link (CAN, UART, shared memory).
3. Load the config on the host and ship the `Model`, or keep yaml-cpp on a target
   that can afford a heap during boot. Nothing downstream cares where the `Model`
   came from — it is a plain object.
4. Tune `fms/limits.hpp` down, then check `sizeof(fms::Model)` against your
   budget.
