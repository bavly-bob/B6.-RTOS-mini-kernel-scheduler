# B6 scheduler: design

One page on how the whole system fits together, for Milestone 2. Each section links
to the component guide that has the details.

## 1. What it is

A simulator of a small real-time kernel with one CPU. It runs a named scenario under
a scheduling policy, records why each decision was made, and writes one JSON file.
A local web page replays that file.

```text
preset + options -> C++ engine -> trace + results -> run.json -> dashboard
   app/              kernel/       kernel/, app/      runs/       dashboard/
```

There is one source of truth: the exported run. The dashboard makes no scheduling
decision and computes no metric.

## 2. Layers

| Layer | Contains | Must not |
| --- | --- | --- |
| `ds/` | Circular list, bitmap, hash map, min-heap, sorted list, dynamic array, bounded array, timing wheel | know about jobs, policies or the GUI |
| `kernel/` | Records and registry, validation, resources, timers, scheduler, trace | print GUI output or depend on a browser |
| `app/` | Presets, CLI (`b6_sim`), run exporter | replace the custom structures with library containers |
| `dashboard/` | Import checks and recorded playback | recompute decisions or invent events |

Main types: `Registry` owns tasks and jobs. `Scheduler` owns the clock, ready
structures and dispatch. `ResourceManager` owns mutexes and semaphores. `TimerManager`
owns sleeping jobs. `RunTrace` stores events, CPU segments and snapshots.
`Simulation` builds them in that dependency order and destroys them in reverse.

## 3. Where each data structure is used

| Structure | Used for | Why | Cost |
| --- | --- | --- | --- |
| `CircularList<Job>` | 32 ready rings; mutex and semaphore waiters | Rotation is round-robin; saved node handles unlink in O(1) | push, pop, remove, rotate O(1) link work; ordered waiter insertion O(n) |
| `PriorityBitmap` | Which rings are non-empty | Finds the most urgent level without scanning jobs | at most 32 bit checks |
| `MinHeap<EdfEntry>` | EDF ready queue | Earliest deadline on top; an index hook makes removal cheap | peek O(1); insert, extract, `removeAt` O(log n) |
| `MinHeap<TimerEntry>` | Heap timer backend | Earliest wake on top | insert and expiry O(log n) each; cancel O(n) |
| `sortedList<TimerEntry>` | Sorted-list timer backend | Simplest correct order; head is the next wake | insert O(n); head removal O(1); cancel O(n) |
| `HashMap<T>` | Task and job lookup by ID | Direct access from events and waiters | expected O(1), worst O(n) |
| `DynamicArray` | Reserved batch of woken jobs | One buffer reused for every expiry | access O(1) |
| `BoundedArray` | Trace storage | Allocated once from the run limits, never grows | push O(1) |
| `TimingWheel` | Separate experiment, tested on its own | Single wheel with round counts; not used by the scheduler | add O(1); tick O(timers in the slot); cancel O(W + N) |

These are counts of structure work. Node allocation, snapshots (one row per released
job per tick) and JSON export cost extra and are not part of a scheduling decision.
Waking k jobs costs at least O(k) with any structure.

The older plan's AVL tree is deliberately not built; the reason is recorded in
[scheduler.md](scheduler.md).

## 4. Time and state

Integer ticks, zero switching cost. At each boundary `t` the same six phases run:

1. **Settle** the interval `[t-1,t)`: finished bursts, UNLOCK, SIGNAL, END.
2. **Inputs**: releases in task-ID order, then due timers, then suspend/resume.
3. **Stabilize**: pick the most urgent job and run its zero-time steps until one needs the CPU.
4. **Deadlines**: flag each unfinished job with `deadline <= t`, once.
5. **Snapshot**: record the stable state.
6. **Execute** `[t,t+1)`, or idle.

Completion exactly at a deadline is on time, because completion is settled before
phase 4. A recorded miss stays recorded after a late completion.

Job states are READY, RUNNING, BLOCKED, SUSPENDED and COMPLETED. Suspension is kept
separate from the reason for waiting, so a suspended sleeper's timer can fire without
making it runnable. Details: [shared-contracts.md](shared-contracts.md).

## 5. Policies

| Policy | Chooses | Ties |
| --- | --- | --- |
| Fixed priority | Front of the highest non-empty ring (larger number is more urgent) | Round-robin with `quantumTicks` |
| Rate monotonic | Same, with priorities assigned by period | Equal periods share a ring |
| EDF | Smallest `(absoluteDeadline, releaseSequence, jobId)` | Total order, so never by address |

The running job stays in its ring. A job preempted by a higher priority keeps the rest
of its slice. Supported combinations: mutexes, semaphores and inheritance only under
fixed priority; RM and EDF take independent periodic jobs. Anything else is refused.
Details: [scheduler.md](scheduler.md).

## 6. Synchronization

Mutexes have an owner; semaphores have a count up to a maximum. Waiters are ordered by
effective priority, then arrival. Release hands the resource **directly** to the first
waiter, so no other job can take it in between.

With inheritance on, an owner's effective priority is the maximum of its base priority
and its highest waiter's; it returns to base on unlock. That simple rule is correct
only because a job holds at most one mutex and may not sleep, wait on a semaphore,
be suspended or complete while holding it. Semaphores do not inherit.
Details: [resource-manager.md](resource-manager.md).

## 7. Ownership and lifetime

- The registry owns every job for the whole run; queues and timers only borrow.
- A `CircularList` handle is invalid once its node is removed, and is cleared at once.
- Nothing outlives the registry. No pointer value is ever exported.
- A periodic release always creates a new job, even if the previous one is unfinished.

## 8. Run file and dashboard

[run-schema-v1.md](run-schema-v1.md) defines the only interface between C++ and the
page. The import step rejects anything inconsistent, naming the field. The selected
tick shows the snapshot recorded at that tick and nothing else. Rules and field use:
[dashboard/README.md](../dashboard/README.md).

Design choices on the dashboard side:

- **Snapshots rather than event replay.** The page needs no scheduler logic, so it
  cannot disagree with the engine. The price is file size.
- **No framework, server or network.** It opens from disk on any machine with a browser.
- **Text only.** Names and reasons from a file are never interpreted as HTML.

## 9. Trade-offs

| Choice | Gain | Cost |
| --- | --- | --- |
| Zero switching cost, zero-time resource steps | Traces can be checked by hand | Real overhead is not modelled |
| One held mutex per job | Inheritance is easy to prove correct | No nested locks or chained donation |
| Fixed limits (32 tasks, 512 jobs, 2,000 ticks, 50,000 events, 100,000 snapshot rows) | Bounded memory, explicit errors | Large workloads are refused |
| Two timer backends | Same wakes, different costs to compare | Two adapters to test |
| Snapshot every tick | Simple, exact time inspection | O(jobs) trace work per tick |

## 10. Evidence

| Claim | Where it is checked |
| --- | --- |
| Data structures | `test_ds`, `test_minheap`, `test_sorted_list`, `test_dynamic_array` |
| Wheel advances when empty; future-slot cancel; exact and multiple laps | `tests/test_timing_wheel.cpp` |
| Golden traces, ready invariants, repeatability | `tests/test_scheduler.cpp` |
| Both timer backends wake the same jobs | `tests/test_timer_manager.cpp` |
| Mutex, semaphore and inheritance rules | `tests/test_resource_manager.cpp` |
| Export format and escaping | `tests/test_run_export.cpp`, `tests/validate_export.js`, `tests/validate_runs.js` |
| Import rejection | `dashboard/validate.test.js` |
| The slider shows history, not the final state | `dashboard/view.test.js` |
| Works offline in a real browser | `dashboard/browser.check.js` |

## 11. Not finished

| Item | Component owner (plan pp. 19-22) | Effect today |
| --- | --- | --- |
| `Scheduler::onPriorityChanged` | Youssef / Moataz | `--inheritance on` stops at tick 1 with an explicit invalid run, and the `scheduler` CTest fails. Resource-level inheritance is implemented and tested. |
| RM priority assignment | Marc | `--policy rate_monotonic` is refused. |
| Metrics calculation | Marc | Runs carry no metrics; the page says so. |
| Timer cost experiment | Marc / Sarofem | No cost figures are claimed. |

This is a simulation for study. It makes no hardware timing or safety claim.
