# What a version number promises

Versions follow [Semantic Versioning 2.0.0](https://semver.org/). The major is
the breaking number: 1.2 is compatible with 1.0, and 2.0 may break anything on
this page. The minor is additive, the patch neither.
`write_basic_package_version_file` is configured to match — `SameMajorVersion` —
so `find_package(fms_yaml 1.0)` accepts a 1.2 install and refuses a 2.0 one.
`tools/install_check.sh` asserts that against the installed package rather than
leaving it here as prose.

Through 0.x the minor carried that meaning instead, which is why 0.9 and 0.10
are not interchangeable.

## What is covered

The public interface is the headers under `include/fms/`, and nothing else:

| | |
|---|---|
| `abi.hpp` | the capacity tag and `abi::pin()` |
| `alloc_guard.hpp` | the optional heap trap |
| `args.hpp` | `Args` |
| `condition.hpp` | `Condition`, `CompareOp` |
| `io_config.hpp` | `IoConfig` |
| `limits.hpp` | the `FMS_MAX_*` macros and `fms::limits::*` |
| `model.hpp` | `Model`, `StateNode`, `TriggerDef`, `Alternative`, `Decision` |
| `port.hpp` | `IPort`, `Input` |
| `port/console_port.hpp`, `port/memory_port.hpp` | the two shipped ports |
| `runtime.hpp` | `Runtime`, `TraceFn` |
| `setup.hpp` | `Setup` |
| `state_machine.hpp` | `StateMachine`, `TransitionEvent` |
| `status.hpp` | `Status`, `is_ok`, `to_string` |
| `types.hpp` | `Name`, `Channel`, `Message`, `StringView`, `StateId`, `TriggerId` |
| `version.hpp` | the version macros and `fms::version()` |
| `yaml_loader.hpp` | `load_setup_file`, `load_machine_file`, `Diagnostics` |
| `inspect/lint.hpp`, `inspect/diagram.hpp` | the linter and the exporter |

Also covered:

* **The YAML schema.** A machine or setup file that loads under one version
  loads under every later version with the same breaking number.
  [docs/schema.md](schema.md) is the specification.
* **The exported CMake targets** and their names.
* **The `IPort` contract**, in the sense that a port written against one version
  still compiles and behaves against a later compatible one. Adding a method
  with a default implementation is additive; adding one without is not.

Anything in `src/` is internal, including `src/inspect/text.hpp`. So are the
`FMS_ABI_*` macros: `fms::abi::tag()` is the supported way to read the
configuration.

## What is not covered

* **Binary compatibility.** There is none, in either direction. The capacities
  in `limits.hpp` are template arguments of the containers inside `Model`,
  `Setup`, `Args`, `Runtime` and `lint::Report`, so two builds of the same
  version with different capacities are different layouts. The library is
  distributed as source for that reason, and `fms/abi.hpp` makes a mismatch a
  link error — see
  [the capacity guard](architecture.md#the-capacity-guard).
* **The numeric values of `Status` and `lint::Check`.** The enumerators are
  named and their `to_string()` slugs are stable; the underlying numbers are
  not, and a new enumerator may be inserted anywhere. Switch on the names,
  compare with `is_ok()`, and do not serialise the integer.
* **The exact text of a diagnostic.** `Diagnostics::message` and
  `lint::describe()` are for people to read. `Status`, `lint::Check` and the
  slug from `to_string()` are what a script should match on.
* **The `--check`, `--lint` and `--export` output of `car_console`.** It is an
  example, not a product; the exit codes are the part worth depending on
  (0 clean, 1 the configuration was rejected or the linter found an error,
  2 the arguments were wrong).
* **The default capacity values.** They may change in any release. Set the ones
  your machine needs.

## What a change to a covered thing looks like

| Change | Version |
|---|---|
| a new function, header, target, `Status` enumerator or lint check | minor |
| a new YAML key, or a new spelling of an existing one | minor |
| a new `IPort` method with a default implementation | minor |
| removing or renaming anything in the table above | breaking |
| a YAML file that used to load and no longer does | breaking |
| a `Status` a call could not previously return | breaking |
| a defect fix that does not change a signature or a schema | patch |

A removal is announced in [CHANGELOG.md](../CHANGELOG.md) one minor release
before it happens, and the thing being removed is marked `[[deprecated]]` in
that release where the language allows it.

## The build

These move only with a breaking version:

| | |
|---|---|
| Language | C++17, no exceptions outside `fms_config`, no RTTI requirement |
| CMake | 3.20 |
| Toolchains | GCC 13, Clang 18, MSVC 2022. Each is built and tested on every push |
| ETL | 20.39.4 |
| yaml-cpp | 0.8.0, and only `fms_config` links it |
| doctest | 2.4.11, tests only |

A newer version of a dependency may work and is not tested. The pinned
versions are what CI builds and what `tools/install_check.sh` installs.
