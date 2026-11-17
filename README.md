# B6 RTOS mini-kernel scheduler

A C++17 educational scheduler simulator under construction. Existing custom data
structures provide storage; the new kernel foundation supplies shared task/job
records, a stable registry and scenario validation.

Start with [the shared contracts](docs/shared-contracts.md) for Moataz's first
handoff, field meanings, ownership rules, supported inputs and team integration.
Mutex ownership, semaphore permits and ordered waiters are now implemented in
`kernel_resources`. See [the resource integration guide](docs/resource-manager.md)
and [producer/consumer oracle](scenarios/semaphore_signal.md). The production
scheduler loop, priority inheritance, timer adapters, exporter and dashboard are
not implemented yet.

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

Current baseline limitation: the existing `data_structures` suite fails in
`tests/test_timing_wheel.cpp:11`; the completion plan assigns the wheel repair to
Bavly. The heap, sorted-list and DynamicArray suites are not all registered in
CMake yet; their integration remains a separate team task. A passing foundation
suite does not imply the entire project passes.

## Layout

- `ds/include`, `ds/src`: committed custom data structures.
- `kernel/include`, `kernel/src`: shared records, registry, validation and resources.
- `tests/test_kernel_foundation.cpp`: prerequisite behavior and invalid inputs.
- `tests/test_resource_manager.cpp`: resources, scheduler hooks and semaphore oracle.
- `docs/shared-contracts.md`: field, lifetime, timing and ownership handoff.
- `docs/resource-manager.md`: mutex/semaphore API and scheduler integration.

The PDF completion plan is local reference material and is ignored by Git.
