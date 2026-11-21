# Moataz -> everyone: kernel foundation

Implemented prerequisite from the B6 Project Completion Plan (16 November 2026),
pages 9-14 and 20-21. This is the shared records, registry and input-validation
handoff. It does not implement dispatch, resource operations, timers or export.
The plan's proposals are made concrete below; team review is still needed before
other modules adopt or change these interfaces.

The next component is now implemented separately in `kernel_resources`; see
[resource-manager.md](resource-manager.md) for operations and scheduler hooks.
Statements below about the foundation's scope do not include that component.

## What each member uses

| Consumer | Interface | Responsibility remaining with the consumer |
| --- | --- | --- |
| Youssef | `Registry::releaseJob`, `Job`, `TickPhase` | Visit every due release, ready membership, state transitions, clock and dispatch |
| Marc | Task periods, job deadlines, release sequence and EDF metadata | Assign RM priorities before registry construction; implement EDF ordering and metrics |
| Sarofem | `TimerEntry`, job IDs, optional timestamps and run limits | Timer adapters, trace storage, serialization and schema v1 |
| Bavly | Documented states, IDs, priorities and missing-time meaning | Consume Sarofem's exported schema; never serialize C++ pointers |
| Moataz (next task) | `MutexRecord`, `SemaphoreRecord`, job wait metadata | Ordered wait queues, ownership transfer, permit accounting and scheduler hooks |

All types are in namespace `b6`. Include `types.h`, `registry.h` or `validation.h`
through `kernel/include`. Link CMake target `kernel_foundation`, which propagates
the public include paths. No existing data structure implementation was changed.

## IDs and records

- Task/resource IDs are nonnegative `int`; zero is valid. Task IDs are unique;
  resource IDs are unique across mutexes and semaphores. Job IDs start at 1 and
  increase per release. Timer IDs/sequences must be assigned uniquely by the timer
  manager. Different ID kinds are named aliases, not distinct C++ wrapper types.
- `Tick` is signed 64-bit simulated time; `Sequence` is unsigned 64-bit order.
- `TaskSpec` contains immutable input fields and a finite script. `TCB` is an
  alias for it, not a second object containing mutable execution state.
- `Job` represents one release. Identity, release tick, deadline and release
  sequence must not be changed after publication. Scheduler-owned fields track
  execution, waiting and queue membership independently for each job.
- `firstStartTick`, `completionTick`, `deadlineMissTick`, resource IDs and timer
  metadata use `std::optional`; absence becomes JSON `null`, never zero or the
  horizon. `totalCpuUsed` starts at zero. The interpreter initializes
  `cpuBurstRemaining` when entering a RUN instruction; the registry does not
  execute even the first instruction.
- `TimerEntry` compares `(wakeTick, insertionSequence)` and tests equality by
  `timerId`. Never mutate a queued ordering key. `active=false` represents
  cancellation; physical removal/expiry is the timer manager's responsibility.
- Resource records contain borrowed `Job*` waiter rings. They are storage only:
  their constructors do not implement locking or automatically sort waiters.
  The future manager must initialize them from validated resource specs.

## Ownership and registry use

The registry copies the scenario, owns fixed arrays of task and job records, and
indexes borrowed pointers with the existing `HashMap<T>`. It cannot be copied or
moved. Task access is const. Caller changes to the original scenario do not change
the registry. Job addresses remain stable, including completed jobs, until registry
destruction. There is intentionally no mid-run deletion or reset API.

Construct the registry before ready queues, timers and resource managers; destroy
those borrowers first. When a list node is removed, clear the corresponding
`readyNode`/`waitNode` immediately. Never pass a stale handle to `remove()`.
The scheduler owns ready-list/bitmap changes, including priority donation moves.

`validateScenario(s)` returns an empty string on success or the first actionable
error. `Registry(s)` also validates and throws `std::invalid_argument` on invalid
input. Missing `findTask`/`findJob` returns `nullptr`; lookup does not create records.

Call `releaseJob(taskId, tick)` for every due task in `(tick, taskId)` order.
It rejects unknown IDs, wrong release times, out-of-order/duplicate calls and
releases outside `[0, horizonTicks)`. It returns a READY record with no queue
membership: the scheduler must enqueue it before recording a stable boundary.
The registry does not generate releases or detect a due release the caller skipped.
Job IDs and release sequences are deterministic for the same ordered calls.
Invalid calls leave the published count unchanged. Capacity failures throw
`std::length_error`; allocation failures propagate `std::bad_alloc`.

Example: a task with period 2 released at ticks 0 and 2 has two distinct jobs,
even if the first is still blocked. The second release never resets the first job.

## State and scheduling contract

The four live states are `Ready`, `Running`, `Blocked`, `Suspended`; `Completed`
is an additional terminal state retained for metrics. `BlockReason` is `None`,
`Timer`, `Mutex` or `Semaphore`. At a stable boundary:

- READY/RUNNING jobs have `suspended=false` and no unresolved block reason.
- SUSPENDED jobs have `suspended=true`; an unresolved wait remains in their block
  metadata. Resolving a wait clears it without clearing suspension.
- Resume yields BLOCKED if the wait is unresolved, otherwise READY.
- COMPLETED jobs have no queue/timer membership or held mutex. Completion time
  is present. A previously recorded deadline miss remains recorded.

These are integration invariants, not state transitions implemented by this
foundation. Future scheduler/resource APIs must enforce them together.

Priority 31 is highest and 0 is lowest, matching the committed bitmap. Under
fixed priority/RM the running job stays in its priority ring; exactly one job can
be RUNNING. Bitmap bit p is set iff ring p is nonempty. RR retains a partial slice
across high-priority preemption, resets it after blocking/new turns, and settles
completion/blocking before rotation. EDF orders by `(absoluteDeadline,
releaseSequence, jobId)` using value entries, never raw pointer comparison.

Tick phases have explicit values 1-6:

1. Settle the previous CPU interval, completion and immediate UNLOCK/SIGNAL.
2. Process releases (task-ID order), then due timers (wake/sequence order), then
   administrative controls (input order). These subphase choices fix ties.
3. Select/stabilize zero-time instructions until a CPU burst or idle is reached.
4. Record each unfinished job with deadline <= now as missed once.
5. Record ordered events and the stable boundary snapshot.
6. Execute `[t,t+1)` only when `t < horizonTicks`.

Completion exactly at a deadline is on time. The final boundary settles existing
jobs but creates no new releases or CPU interval. Events use `(tick, phase, seq)`;
event IDs/sequences are assigned by the future event sink. There are no backward
script jumps. A boundary can consume at most `maxJobs * maxScriptLength`
instructions; the future interpreter must enforce that work bound.

## Accepted inputs and supported modes

RUN/SLEEP have positive durations and no resource ID. LOCK/UNLOCK address an
existing mutex; WAIT/SIGNAL address an existing semaphore; each has duration 0.
END has no resource and duration 0 and must be the last instruction. Scripts may
contain only END (zero CPU work); zero-time loops are unrepresentable.

The validator checks IDs, names, priorities, release/period/deadline ranges,
instruction fields, semaphore counts and duplicate/missing resources. It rejects
nested/recursive locks, unlock without a matching lock, and SLEEP, WAIT or END
while holding a mutex. Arithmetic checks prevent deadline/wake/CPU-sum overflow.

Fixed priority supports resource scripts and the inheritance configuration flag.
Actual synchronization and inheritance are not implemented here. Initial RM/EDF
inputs must be periodic, resource-free RUN/END scripts with inheritance disabled.
RM plus resources is deferred until explicitly implemented/tested, as the plan
allows. Relative deadlines need not equal periods; utilization-bound applicability
must be checked separately by Marc's metrics code. A first release at or beyond
the horizon is valid and contributes zero jobs. An empty scenario is a valid idle
workload.

Runtime checks remain necessary for non-owner unlock, duplicate memberships,
semaphore overflow, suspension while holding a lock, resource handoff and state
transitions. Static script checks do not prove those future implementations correct.
Administrative controls and JSON parsing are outside this prerequisite.

## Limits and costs

Storage ceilings: 32 tasks, 512 jobs, 8 mutexes, 8 semaphores, 64 instructions per
task, horizon 2,000 ticks, 50,000 events and 100,000 snapshot job rows. The script
ceiling of 64 is a concrete implementation choice added to the plan. Run limits
may lower these ceilings. Zero resource capacities are allowed; task/job/script,
horizon/event/snapshot limits must be positive.

All scheduled releases inside the horizon are counted during validation, including
overlapping unfinished jobs. Exceeding the job limit rejects the scenario instead
of dropping releases. Event and snapshot limits are configuration contracts only;
the future trace writer must count and enforce them at runtime.

Lookup and release indexing are expected O(1), with collision-chain worst cases.
Input validation scans bounded task/resource arrays and scripts (including pairwise
duplicate checks); it runs before simulation. Registry storage is fixed capacity;
hash bucket arrays are reserved at construction, but hash nodes still allocate on
insertion and strings may allocate. This is not allocation-free/hard-real-time code.
Only strings, optional values and standard scalar utilities are used from the
standard library; core collections remain custom maps/lists and built-in arrays.

## Verification and next work

`test_kernel_foundation` checks validation failures, stable storage, overlapping
jobs, valid ID zero, null timestamps, release ordering, full 512-job capacity,
resource script restrictions and timer ordering. Its checks stay active with NDEBUG.

Resource ownership/permit behavior, ordered waiters and a test-only producer/consumer
oracle are now available in `kernel_resources`, together with priority inheritance,
restoration, runtime transition guards and test-adapter inversion OFF/ON traces.
Next is production scheduler/timer integration. No production scheduler trace or
dashboard evidence is claimed by this prerequisite. The timing-wheel failure noted
in the original plan was repaired upstream before the 21 November update.
