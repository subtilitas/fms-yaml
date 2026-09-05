# Contributing

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFMS_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`-DFMS_WARNINGS_AS_ERRORS=ON` is what CI uses on all three toolchains, so a
warning is a failure here before it is one there.

## The gates

Every gate CI runs is a script in `tools/`, so a local run is the run that
decides the build. A tool that is not installed fails the run rather than being
skipped: a gate that could not run has not passed.

```sh
bash tools/analyze.sh          # clang-tidy, cppcheck, and the tooling lint
bash tools/coverage.sh         # build, test, HTML report, per-file summary
bash tools/install_check.sh    # install, then build a consumer through find_package
bash tools/cross_check.sh      # compile fms_core and fms_inspect for a Cortex-M4
bash tools/capacity_sweep.sh   # the suite at three capacity configurations
```

`ctest` runs the rest: `abi_guard`, `symbol_check`, `car_config_check`,
`car_version`, `car_diagram_check` and the scripted console session. What each
one answers is in the table at the end of
[docs/architecture.md](docs/architecture.md#testing).

Line coverage has a floor of 90%, enforced by `tools/coverage_report.py` in the
tree rather than by the reporting service.

## Things that need doing in more than one place

| If you change | Also |
|---|---|
| a capacity in `include/fms/limits.hpp` | add it to the tag in `include/fms/abi.hpp` and to the list in `tests/test_abi.cpp`; `abi_guard` fails until you do |
| an ETL container that appears in `Model`, `Setup`, `Args` or `Runtime` | add its size to `fms::abi::etl_layout` in `include/fms/abi.hpp`, so a consumer whose ETL lays it out differently fails to link rather than silently |
| a test that names a capacity | derive it from `fms::limits::` rather than writing the number out, or skip the case with a printed reason where the configuration cannot express it; `tools/capacity_sweep.sh` is what notices |
| `examples/car/car.machine.yaml` | `python3 tools/diagram_sync.py --write` to redraw the README diagram |
| a public header | check the table in [docs/stability.md](docs/stability.md) still matches |
| anything user-visible | add a line to [CHANGELOG.md](CHANGELOG.md) under Unreleased |
| the version | only `project()` in `CMakeLists.txt`; `fms/version.hpp` is generated from it |

## Writing

Documentation is treated as a specification, so a claim that is false about the
code is a defect and gets fixed like one.

* Present tense, describing the system as it is. No development history, no
  "previously" or "now" — that is what `CHANGELOG.md` and git are for.
* No self-assessment. A defect that still affects someone is documented as a
  current limitation, with the condition that triggers it.
* Numbers, not adjectives. An adjective that could be a number is a missing
  number.
* Short sentences. Plain words. Expand an acronym on first use per document.
* Say what a thing does and what it costs before saying how to use it. A command
  or a code block beats a paragraph describing one.
* An unknown is stated as unknown — "not measured", "untested above 40 V" —
  never asserted and never quietly left out.

Comments follow the same rules, and a comment that describes something the code
does not do is a defect.

## Commits and pull requests

Imperative mood, the change and its effect. Not the path you took to find it.

A pull request says what was wrong, what it does about it, and what was
measured — the numbers you actually saw, from the commands above.

## Adding a gate

A new gate is a script in `tools/` plus a `ctest` case or a CI job, not a `run:`
block in a workflow. Two reasons: the flags exist once, and a gate has to be
runnable by whoever is trying to make it pass.

Prove the gate fails. A check that has only ever been seen to pass has not been
tested — every gate here has had its failure reproduced deliberately, and the
pull request that added it says how.
