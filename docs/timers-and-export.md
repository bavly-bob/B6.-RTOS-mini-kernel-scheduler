# Timers, trace recording and export: integration guide

Sarofem's components (plan pp. 12, 17-18, 22). They are built and tested on
their own; the production scheduler (Youssef Gohar) calls them as described
here. No production scheduler trace is claimed yet: the only exported runs so
far are test files labelled `design_fixture`.

| Component | Files | CMake target |
| --- | --- | --- |
| Timer manager (2 backends) | `kernel/include/timer_manager.h`, `kernel/src/timer_manager.cpp` | `kernel_timers` |
| Bounded trace storage | `ds/include/bounded_array.h`, `kernel/include/run_trace.h`, `kernel/src/run_trace.cpp` | `kernel_trace` |
| JSON run exporter | `app/include/run_exporter.h`, `app/src/run_exporter.cpp` | `run_export` |
| File format | [run-schema-v1.md](run-schema-v1.md) | — |

## Construction order (one run)

```cpp
Registry registry(scenario);            // owns jobs; outlives everything below
Scheduler scheduler(registry, ...);     // implements ResourceSchedulerHooks
ResourceManager resources(registry, scheduler);
TimerManager timers(registry);          // backend from config.timerBackend
RunTrace trace(scenario.config);        // capacities from config.limits/horizon
DynamicArray woken(static_cast<int>(scenario.config.limits.maxJobs)); // reserved once
```

Destroy in reverse order. Nothing here may outlive the registry.

## Per-boundary calls (tick phases from shared-contracts.md)

| Phase | Scheduler does | Calls into these components |
| --- | --- | --- |
| 1 Settle | Account `[t-1, t)`, completions, UNLOCK/SIGNAL | `trace.addEvent(COMPLETE ...)` |
| 2 Inputs | Releases (task-ID order), then timers, then controls | `trace.addEvent(RELEASE ...)`; `timers.expireDue(t, woken)`; for each `Job* j` in `woken`: emit `WAKE` (`flag("suspended", j->suspended)`), and enqueue only if `j->state == JobState::Ready` |
| 3 Stabilize | Run zero-time instructions until a CPU burst or idle | On `SLEEP n`: `timers.sleep(job.jobId, t, n)`, then remove ready membership, advance `scriptIndex` once, emit `BLOCK` (`reason "timer"`, `with("wakeTick", ...)`), reselect. Emit `DISPATCH`, `PREEMPT`, `LOCK`, `PRIORITY_CHANGE`, ... |
| 4 Deadlines | Mark misses once | `trace.addEvent(DEADLINE_MISS ...)` |
| 5 Snapshot | Build the ready order (most urgent first) | `trace.captureSnapshot(t, runningId, readyIds, n, registry, &resources)` |
| 6 Execute | If `t < horizon`, run one tick | `trace.recordInterval(t, running, basePriority)` or `trace.recordIdle(t)` |

After the loop: `exportRunToFile(path, info, registry, trace, &metrics)`.
Set `info.evidence = RunInfo::Evidence::EngineRun` only for real scheduler output.
If the engine throws (registry capacity, resource/timer invariant), catch it at
the application boundary, call `trace.markInvalid(message)` and still export,
so the dashboard shows an explicit invalid run instead of a partial "ok" one.

## TimerManager contract

| Call | Result | Cost |
| --- | --- | --- |
| `sleep(jobId, now, n)` | Job must be RUNNING, unsuspended, no wait, no held mutex, no timer; `n > 0`. Sets BLOCKED + `BlockReason::Timer` + `wakeTick = now + n` + `timerId`. Returns the timer ID. | list O(n), heap O(log n) |
| `nextWake()` | Earliest wake tick or `nullopt`. | O(1) |
| `expireDue(now, woken)` | Clears `woken`, then appends every job whose timer is due, in `(wakeTick, insertion order)`. Each woken job has its wait cleared and a fresh quantum. Unsuspended: READY. Suspended: stays SUSPENDED with the wait resolved. | list O(k), heap O(k log n) |
| `cancel(timerId)` | Removes one pending timer; clears the job's `timerId`/`wakeTick` only. For teardown/abort. | O(n) |
| `reset()` | Drops all timers and clears their jobs' timer fields. | O(n) |

- Both backends give identical wake sequences (tested with 512 jobs, random
  sleeps and cancellations), so the timer experiment can compare costs fairly.
- A timer only wakes the job still waiting on that exact timer ID; a completed
  or cancelled job can never be woken (it throws instead).
- `now` must never decrease between calls.
- RESUME rule (with Moataz's model): if `blockReason == None`, the job becomes
  READY and is enqueued; if it is still `Timer`, it stays BLOCKED and wakes later.

## Costs and honest limits

- The heap backend is reserved to `limits.maxJobs` (one timer per job), so it
  never reallocates mid-run. The sorted list allocates one node per `sleep`.
- `RunTrace` allocates every array once from the limits; `push` never grows.
  Hitting `maxEvents` or `maxSnapshotJobRows` stops recording, marks the run
  invalid and lists the limit; nothing is dropped silently.
- Snapshots copy one row per released job per boundary. With the default limits,
  a 2,000-tick horizon fits roughly 50 jobs present from the start; small demo presets are far below this.
- File size: a run that fills the default snapshot and event limits can reach about
  38.7 MiB, above the dashboard's 32 MiB import limit; the exporter then reports
  an error instead of success. With at most 70,000 snapshot rows the file always
  fits (tested at 72,036 rows with the widest values: 31.8 MiB).
- Both timer backends export byte-identical runs for every preset and for 40
  seeded sleep-heavy scenarios with equal wake ticks, batches and suspended
  sleepers (`tests/test_timer_equivalence.cpp`).
- Trace and export costs are separate from scheduling-operation costs.

## Remaining work (19-21 Nov)

- Connect these calls to the production scheduler loop and export the first
  real `engine_run` (needs Youssef Gohar's scheduler).
- Done: `computeMetrics` (`app/src/metrics.cpp`) fills `RunMetrics`, and
  `Simulation::exportTo` passes it for every valid run.
- Timer-cost experiment with Marc: 10, 100, 500 pending timers on both backends.
