# B6 RTOS mini-kernel scheduler

A C++17 educational scheduler simulator with a local dashboard. The C++ engine runs
a named scenario and exports one JSON file; the dashboard replays that file so you can
see why each job ran, waited or missed its deadline.

- [Design](docs/design.md): how everything fits together, costs and trade-offs.
- [Scenarios](scenarios/README.md): what each preset shows and must produce.
- [Demonstration](demo/README.md): steps, files to import and screenshots.
- [Dashboard](dashboard/README.md): how to read it and which files it rejects.
- Component guides: [shared contracts](docs/shared-contracts.md),
  [scheduler](docs/scheduler.md), [resources](docs/resource-manager.md),
  [timers and export](docs/timers-and-export.md), [run-file schema](docs/run-schema-v1.md).
- [Results](docs/results.md): golden traces against measured runs, metrics, timer costs.
- [Changelog](CHANGELOG.md) for the dashboard and documentation work.

## Status

| Part | State |
| --- | --- |
| Data structures, registry, validation, resources, both timers, trace, export | Implemented and tested |
| Fixed priority, round-robin, rate monotonic, EDF, suspend/resume, seven presets, `b6_sim` | Implemented and tested |
| Metrics: response times, misses, jitter, observed utilization, demand, RM bound applicability | Implemented and tested; exported with every valid run |
| Dashboard import checks, playback, inspector, events, resources | Implemented and tested |
| Priority inheritance | Resource rules done; the scheduler does not yet implement `onPriorityChanged`, so `--inheritance on` stops with an invalid run and the `scheduler` test fails |

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

Dashboard checks need only Node.js (22+ and Chrome or Edge for the last one):

```sh
node dashboard/validate.test.js
node dashboard/view.test.js
node dashboard/browser.check.js
```

## Run the simulator

```sh
./build/b6_sim --list
./build/b6_sim --scenario rr_preemption --policy fixed_priority --timer heap
./build/b6_sim --scenario priority_inversion --inheritance off
./build/b6_sim --scenario rm_edf_compare --policy rate_monotonic --timer heap
./build/b6_sim --scenario rm_edf_compare --policy edf --output runs/edf.json
```

With a multi-configuration generator the program is in `build/Debug/`.

Options: `--policy fixed_priority|rate_monotonic|edf`, `--timer sorted_list|heap`,
`--inheritance off|on`, `--quantum N`, `--output FILE` (default
`runs/<scenario>-<policy>-<timer>.json`). The program prints the CPU timeline
and each job's outcome, then writes the run file. Exit code 0 means a valid run,
1 an invalid run (still written, for inspection), 2 a bad option or scenario.

## View a run

1. Open `dashboard/index.html` in a browser. No server, installation or network is needed.
2. Click **Import run(s)** and choose one or more files from `runs/`.
3. Pick a run, then use Play, Next, Previous or the slider, and choose a job to inspect.

The page replays a recording; it never runs or changes the schedule. To try another
setting, run `b6_sim` again and import the new file.

## Model restrictions

One CPU, integer ticks, zero switching cost, zero-time resource operations, at most one
held mutex per job. Mutexes, semaphores and inheritance need fixed priority; EDF and RM
need independent periodic jobs. Limits: 32 tasks, 512 jobs, 8 mutexes, 8 semaphores,
2,000 ticks, 50,000 events, 100,000 snapshot rows. Exceeding one is an explicit error.

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
- `docs/design.md`: the whole system on one page.
- `dashboard/`: the viewer, its sample fixtures and its checks.
- `scenarios/`: plain-language presets with independent expected results.
- `demo/`: demonstration steps and screenshots.
- `runs/`: real engine runs (`evidenceType: engine_run`) for the demonstration.

The PDF completion plan is local reference material and is ignored by Git.
