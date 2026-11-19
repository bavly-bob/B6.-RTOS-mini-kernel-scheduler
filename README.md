# B6 RTOS mini-kernel scheduler

A C++17 educational scheduler simulator under construction. Existing custom data
structures provide storage; the new kernel foundation supplies shared task/job
records, a stable registry and scenario validation.

Start with [the shared contracts](docs/shared-contracts.md) for Moataz's first
handoff, field meanings, ownership rules, supported inputs and team integration.
Mutex ownership, semaphore permits and ordered waiters are now implemented in
`kernel_resources`. See [the resource integration guide](docs/resource-manager.md)
and [producer/consumer oracle](scenarios/semaphore_signal.md). Timers (sorted-list
and heap backends), bounded trace recording and the JSON run exporter are in
`kernel_timers`, `kernel_trace` and `run_export`; see
[the timer/export integration guide](docs/timers-and-export.md) and
[the run-file contract](docs/run-schema-v1.md). The production scheduler loop and
priority inheritance are not implemented yet, so no real engine run exists yet.

## Build and test

Prerequisites: CMake 3.16+, a C++17 compiler, and a build tool such as Make/Ninja.
Run from this repository (Linux/WSL, or a configured native CMake environment):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

For multi-configuration generators, build with `--config Debug` and run CTest
with `-C Debug`. Debug keeps the older assertion-based data-structure tests active.

Run the foundation and resource suites:

```sh
ctest --test-dir build -R "kernel_foundation|resource_manager" --output-on-failure
```

Every data-structure suite (shared DS suite, heap, sorted list, DynamicArray) is
registered in CMake. If Node.js is installed, `run_export_dashboard_validation`
also checks exported run files with the dashboard's own validator. A passing
suite only covers the exercised paths; it does not imply the scheduler works.

## Layout

- `ds/include`, `ds/src`: committed custom data structures.
- `kernel/include`, `kernel/src`: shared records, registry, validation and resources.
- `tests/test_kernel_foundation.cpp`: prerequisite behavior and invalid inputs.
- `tests/test_resource_manager.cpp`: resources, scheduler hooks and semaphore oracle.
- `docs/shared-contracts.md`: field, lifetime, timing and ownership handoff.
- `docs/resource-manager.md`: mutex/semaphore API and scheduler integration.
- `app/include`, `app/src`: run exporter (JSON, schemaVersion 1).
- `tests/test_timer_manager.cpp`, `tests/test_run_export.cpp`: timers on both
  backends; trace recording and export, cross-checked by `tests/validate_export.js`.
- `docs/timers-and-export.md`, `docs/run-schema-v1.md`: timer/trace calls per tick
  phase and the run-file contract shared with the dashboard.

The PDF completion plan is local reference material and is ignored by Git.
