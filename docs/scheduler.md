# Scheduler, clock and simulator program

Owner: Youssef Gohar. Plan pp. 10-11, 14-16, 20. Code:
`kernel/include/scheduler.h`, `kernel/src/scheduler.cpp` (CMake `kernel_scheduler`),
`app/include/simulation.h`, `app/src/simulation.cpp`, `app/src/presets.cpp`
(`b6_app`), `app/src/main.cpp` (`b6_sim`). Tests: `tests/test_scheduler.cpp`,
`tests/validate_runs.js`.

This is the part that makes the other components into one working simulator:
it owns the simulated clock, decides which job has the CPU at every tick, runs
each job's script, and records what happened so the run file can be exported.

## How a run is put together

```text
b6_sim --scenario X  ->  preset builds a Scenario (+ suspend/resume controls)
                     ->  Simulation: Registry, RunTrace, Scheduler, ResourceManager, TimerManager
                     ->  Scheduler::run(): boundaries 0..horizon
                     ->  exportRunToFile(): runs/<runId>.json  ->  dashboard
```

`Simulation` constructs the components in the order the integration guide
requires and destroys them in reverse: nothing outlives the registry, and the
resource manager is destroyed before the scheduler that serves as its hooks.

## One boundary (tick t)

| Phase | What the scheduler does |
| --- | --- |
| 1 Settle | The job that ran `[t-1,t)` has been charged. If its RUN finished, it immediately runs the steps that cannot block it: UNLOCK, SIGNAL, END, or loading its next RUN. If its slice is used up and it still needs CPU, its ring rotates. |
| 2 Inputs | Releases in task-ID order (one new job per release), then due timers (`TimerManager::expireDue`), then suspend/resume controls in input order. |
| 3 Stabilize | Take the most urgent job, make it RUNNING, run its zero-time steps (LOCK, WAIT, SLEEP, ...). If it blocks or completes, take the next one. Stop when a job needs the CPU, or nothing is runnable (idle). |
| 4 Deadlines | Each unfinished job with `deadline <= t` is marked missed once. Completion at the deadline is on time because completions happen in phases 1 and 3. |
| 5 Snapshot | Record the stable state: running job, ready order, every job row, every mutex/semaphore with ordered waiters. |
| 6 Execute | If `t < horizon`, the running job uses `[t,t+1)`: its burst, slice and CPU total change by one. Otherwise idle is recorded. |

Ties are fixed by this order, so the same input always gives the same output
(the tests export every preset twice and compare the files byte for byte).
At the final boundary (`t = horizon`) phases 1, 2, 4 and 5 run; there is no
new release, decision or CPU interval.

## Ready structures

**Fixed priority (and RM):** 32 `CircularList<Job>` rings, one per priority
level, and a `PriorityBitmap`. Bit p is set exactly when ring p is nonempty.
The most urgent job is the front of ring `highestSetBit()`, found by checking at
most 32 bits: a fixed cost, independent of the number of jobs.

- The running job stays in its ring, at the front; the others are READY.
- A new or woken job joins the back of its ring (`pushBack`, O(1)), and its
  node handle is saved in `Job::readyNode`.
- Removal uses the saved handle (`remove`, O(1)); the handle is cleared at once.
- Round-robin is `rotate()`: O(1), A,B,C becomes B,C,A without allocation.

**EDF:** a `MinHeap<EdfEntry>` keyed by `(absoluteDeadline, releaseSequence,
jobId)`, a total order, so ties never depend on pointers or insertion. An index
hook keeps `Job::edfIndex` equal to the heap slot, so removing a finished job
is `removeAt(index)`, O(log n), instead of a linear search. Peek is O(1),
insert O(log n). The running job stays in the heap until it completes.

## Round-robin rules

Equal priorities share the CPU with a slice of `quantumTicks`.

- A slice that reaches zero gives the job a fresh slice and rotates the ring.
  A job alone in its ring simply keeps running with the fresh slice.
- Arrivals join the back, so an equal-priority arrival never cuts a slice short.
- A job preempted by a higher priority keeps the rest of its slice and its
  place at the front of its ring.
- Completing or blocking comes first: rotation only applies to a job that
  still needs CPU.
- Waking, being granted a resource, or resuming gives a fresh slice.

## Script steps

| Step | Effect | Calls |
| --- | --- | --- |
| `RUN n` | Loads a burst of n CPU ticks. | |
| `SLEEP n` | BLOCKED until `t + n`; removed from its ring. | `TimerManager::sleep` |
| `LOCK m` | Owns m, or BLOCKED on it (the hook removes it from its ring). | `ResourceManager::lock` |
| `UNLOCK m` | Releases m; ownership goes straight to the best waiter. | `ResourceManager::unlock` |
| `WAIT s` | Takes a permit, or BLOCKED on s. | `ResourceManager::wait` |
| `SIGNAL s` | Gives the permit straight to the best waiter, or increments. | `ResourceManager::signal` |
| `END` | COMPLETED at t; removed from the ready structure. | |

Every successful step advances `scriptIndex` once, including a LOCK or WAIT
that blocks: the later grant completes that step, so it is never repeated.
Resource steps take zero simulated time. In phase 1 the job that just ran stops
before a step that could block it, so this tick's releases are seen first.

## Suspend and resume

Controls are timed scenario inputs (`ControlAction`), not script steps. They
act on the task's most recent unfinished job.

- **Suspend** removes the job from the ready structure (if READY or RUNNING)
  and sets `suspended`. A wait it had (timer, semaphore, mutex) is kept.
- A timer or semaphore grant that arrives while suspended resolves the wait,
  but the job stays SUSPENDED. A suspended mutex waiter is skipped at handoff.
- **Resume** clears `suspended`. With no unresolved wait the job is READY and
  enqueued; otherwise it returns to BLOCKED. A resumed mutex waiter retries the
  mutex through `ResourceManager::resumeMutexWait`.
- Suspending a job that holds a mutex is rejected (the run stops with an error).

## Collaborator contracts

- `ResourceSchedulerHooks`: the resource manager calls `onBlocked` / `onReady`;
  the scheduler only updates ring/heap membership there and never dispatches.
- `TimerManager`: the scheduler calls `sleep` in phase 3 and `expireDue` in
  phase 2, and enqueues only woken jobs that are READY.
- `changeEffectivePriority(job, p, reason)`: the requeue hook for priority
  inheritance. It moves a runnable job between rings, keeps the bitmap in step,
  and emits `PRIORITY_CHANGE`. Not yet called: donation is Moataz's part.

## Invariants (checked at every boundary)

1. Bitmap bit p is set exactly when ring p is nonempty.
2. Every READY/RUNNING job is in the ready structure exactly once; blocked,
   suspended and completed jobs are not in it; saved handles match.
3. At most one job is RUNNING, and it is the job the scheduler points to.
4. After phase 3, the running job is the most urgent runnable job.

A violation throws `std::logic_error`: the run is marked invalid and exported
with an `ERROR` event, never presented as a successful run.

## Expected results (tests/test_scheduler.cpp)

| Preset | Expected (from the plan, worked out by hand) |
| --- | --- |
| `rr_preemption` | A[0,1) B[1,2) H[2,3) A[3,4) B[4,5) A[5,6) B[6,7); H=3, A=6, B=7 |
| `priority_inversion` (OFF) | L[0,2) M[2,6) L[6,8) H[8,9); H completes 9, missed at 6 |
| `sleep_order` | wakes B@110, D@110, C@120, A@130 on both timer backends |
| `suspend_resume` | X[0,1) idle[1,3) X[3,6) idle[6,7) S[7,9); timer fires at 5 while suspended |
| `semaphore_signal` | P[0,2) C[2,3) P[3,4); permit granted directly |
| `rm_edf_compare` (EDF) | A0[0,2) B0[2,6) A1[6,8); B0 completes 6; B1 released 7, unfinished |
| `rm_edf_compare` (RM) | A gets 31, B 30; A0[0,2) B0[2,5) A1[5,7) B0[7,8); B0 completes 8, missed at 7 |
| `rm_edf_compare` (fixed priority) | Same CPU trace as RM: the preset's priorities follow period order |
| `overload` | 9 jobs, misses recorded, CPU never idle |
| deadline boundary (test only) | completion at the deadline is on time; unfinished at the deadline is flagged once and stays flagged after a late completion; a deadline after the horizon is not missed |

On every preset, policy and timer backend the tests also check CPU
conservation, full horizon coverage, one snapshot per boundary, at most one
RUNNING job, that the RUNNING job is the one that runs, and repeatability.
`tests/validate_runs.js` checks every exported run with the dashboard's own
import validator.

## Costs, honestly

- Choosing the next job: at most 32 bit checks plus O(1) ring access (EDF: O(1) peek).
- Enqueue / dequeue / rotate: O(1) link work; `pushBack` also allocates a node,
  and allocation cost is not included in O(1).
- EDF insert/remove: O(log n). Heap storage is reserved to `maxJobs` up front.
- Each boundary also scans all released jobs for deadlines and snapshots:
  O(jobs) per tick. That is trace cost, not the cost of a scheduling decision.
- This is a simulation with zero switching overhead, not a real-time guarantee.

## Scope decision: no AVL tree

The team's older Milestone 1 plan listed an AVL tree. It is deliberately not
built. The B6 proposal's core requires custom non-linear structures for the
scheduler, and those are already covered: the binary min-heap (EDF ready
queue and heap timer backend), the circular lists with node handles (ready
rings and wait queues), the priority bitmap and the hash map (ID lookup).
No scheduling operation needs ordered search by key with rebalancing:
selection is by priority level (bitmap), earliest deadline or wake time
(heap), or FIFO within a level (ring). An AVL tree would add code to test and
explain without improving any required behaviour. This follows the completion
plan's risk table (p.29, owner Youssef), which asks for this revision to be
recorded rather than silently dropped. It can be revisited after Milestone 2
if a measured need appears (for example, ordered range queries over timers).

## Not done here (other members' parts)

| Missing | Owner | Effect today |
| --- | --- | --- |
| Priority inheritance (donation and restore) | Moataz | `--inheritance on` is refused with a clear message; the hook it needs exists. |
