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
[the run-file contract](docs/run-schema-v1.md). The scheduler (clock, priority
rings with the bitmap, round-robin, EDF, script steps, suspend/resume) and the
`b6_sim` program are in `kernel_scheduler`, `b6_app` and `b6_sim`; see
[the scheduler guide](docs/scheduler.md). Real engine runs are in `runs/`.
Not implemented yet: priority inheritance (Moataz), RM priority assignment and
metrics (Marc). `b6_sim` refuses those modes with a clear message.

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

## Run the simulator

```sh
./build/b6_sim --list
./build/b6_sim --scenario rr_preemption --policy fixed_priority --timer heap
./build/b6_sim --scenario priority_inversion --inheritance off
./build/b6_sim --scenario rm_edf_compare --policy edf --output runs/edf.json
```

Options: `--policy fixed_priority|edf`, `--timer sorted_list|heap`,
`--inheritance off|on`, `--quantum N`, `--output FILE` (default
`runs/<scenario>-<policy>-<timer>.json`). The program prints the CPU timeline
and each job's outcome, then writes the run file. Open `dashboard/index.html`,
click **Import run(s)** and choose the file. Exit code 0 means a valid run,
1 an invalid run (still written, for inspection), 2 a bad option or scenario.

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
- `kernel/include/scheduler.h`, `kernel/src/scheduler.cpp`: clock, ready rings,
  bitmap, EDF heap, dispatch, script steps, suspend/resume.
- `app/src/simulation.cpp`, `app/src/presets.cpp`, `app/src/main.cpp`: one-run
  wrapper, named scenarios and the `b6_sim` command-line program.
- `tests/test_scheduler.cpp`, `tests/validate_runs.js`: golden traces, invariants,
  both timer backends, repeatability, errors; dashboard check of real runs.
- `docs/scheduler.md`: tick phases, ready structures, rules, costs and limits.
- `runs/`: real engine runs (`evidenceType: engine_run`) for the demonstration.

The PDF completion plan is local reference material and is ignored by Git.
