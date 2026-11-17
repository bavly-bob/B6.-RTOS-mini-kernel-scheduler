# Mutex and semaphore integration

Moataz's 17 November component: mutex ownership, semaphore permits and ordered
waiters. `ResourceManager` works with the shared registry and existing circular
list. It does not implement a production scheduler, priority inheritance, timer
operations or a JSON event writer. See the independent
[producer/consumer oracle](../scenarios/semaphore_signal.md).

## Construct and connect

Include `resource_manager.h` and link CMake target `kernel_resources`.
Construct one `Registry`, one scheduler hook adapter and one `ResourceManager`
per run, in that order. The registry and adapter must outlive the manager.
The manager copies validated resource IDs/counts into its owned resource records;
job pointers in wait queues remain borrowed from the registry.

The current manager rejects every policy other than fixed priority and rejects
`inheritance=true`. The foundation validator still accepts inheritance as a future
configuration; construction fails explicitly until donation is implemented.

Youssef implements `ResourceSchedulerHooks`:

| Callback | State already set by ResourceManager | Scheduler responsibility |
| --- | --- | --- |
| `onBlocked(Job&)` | BLOCKED, block reason/resource, wait node and wait sequence | Remove ready/EDF membership, clear the saved ready handle, update bitmap/running selection |
| `onReady(Job&)` | READY, cleared wait metadata, fresh quantum | Enqueue once, assign ready sequence, set the ready handle and bitmap |

Callbacks finish their queue updates synchronously. They must not dispatch or call
resource operations recursively. The scheduler reselects after the operation has
returned, including after SIGNAL/UNLOCK. Read-only resource lookup inside a callback
is allowed. The resource manager never edits ready rings or the priority bitmap.

Callbacks may throw (for example, ready-node allocation can fail). The resource
change has already committed, so this invalidates the run: the original exception
propagates, and the manager rejects every subsequent mutating operation. Catch it
at the application boundary and report an invalid run; do not continue scheduling.
Read-only inspection and destruction remain available. This avoids silently losing
a permit or trying to roll back a partially updated scheduler.

## Operation contract

All regular operations take a caller job ID and a resource ID. The caller must be
registered, RUNNING, unsuspended and free of unresolved wait membership. Only the
scheduler may decide which job becomes RUNNING. This is a single-threaded simulated
kernel, not a host-thread synchronization primitive.

| Operation | Result |
| --- | --- |
| `lock(job, mutex)` | Returns Acquired and assigns ownership if free; otherwise returns Blocked after ordered enqueue and the block hook |
| `unlock(job, mutex)` | Only the owner may unlock; returns the directly granted job ID, or null if none eligible |
| `wait(job, semaphore)` | Returns Acquired after consuming one count, or Blocked after enqueue when count is zero |
| `signal(job, semaphore)` | Grants exactly one queued WAIT directly, or increments count if there are no waiters; returns the granted job ID or null |

Advance the script index **once** after a successful operation, including a
Blocked LOCK/WAIT result. The future wake already completes that acquisition;
re-executing LOCK/WAIT would lock recursively or consume another permit. If an
operation throws, abort or report the error; do not advance as though it succeeded.
The manager itself never changes `scriptIndex` or consumes simulated CPU time.

The caller can use operation results plus job/resource records to emit LOCK,
UNLOCK, WAIT, SIGNAL, BLOCK and WAKE events. Ticks, event sequencing and export
remain the scheduler/exporter's responsibility; no production trace is claimed.

## Ownership and ordering

Each job may hold at most one mutex. Recursive/nested locking, non-owner unlock
and semaphore WAIT while holding a mutex throw `std::logic_error`. Unlock clears
the previous owner's held-mutex field before handing ownership to one waiter.
The recipient owns the mutex before its ready hook runs, so another runnable job
cannot steal it. Remaining waiters keep their memberships and stable handles.

Wait queues are physically ordered by effective priority descending, then wait
sequence ascending. Equal-priority waiters are FIFO. A single increasing sequence
is used across resources. Current fixed-priority runs must not change effective
priorities while jobs wait; a future inheritance implementation must explicitly
reorder affected queues. No donation or base-priority restoration occurs here.

The existing circular list has no insert-before method. The manager rotates to
the insertion point, appends there, then restores the appropriate head. This
preserves existing node handles and needs only one new node. If allocation fails,
the original head is restored and no block hook runs. Insertion is O(n) link
visits, not O(1); no data-structure source files were changed.

Semaphore signal transfers a permit straight to the first waiter, leaving count
unchanged. Otherwise it increments count unless already at max; overflow throws
`std::overflow_error` without changing count. Semaphores have no owner and do not
receive inheritance. Binary semaphores use maxCount=1; counting semaphores use a
larger positive maximum.

## Suspension and teardown boundaries

The manager does not implement suspend/resume commands. It supports the following
integration rules so later suspension work does not lose a wait:

- A suspended semaphore waiter may receive a direct permit. Its wait is resolved,
  but it stays SUSPENDED and no ready hook runs. Resume later enqueues it without
  repeating WAIT.
- A suspended mutex waiter is skipped during handoff. It keeps its wait node;
  ownership is never assigned while it is suspended. If all waiters are suspended,
  the mutex becomes unowned.
- After resuming a mutex waiter, the scheduler clears `suspended`, sets BLOCKED
  while preserving its wait metadata, and calls `resumeMutexWait(jobId)`. If the
  mutex is free, the highest eligible waiter gets it; if owned, the job keeps
  waiting. Do not enqueue it separately unless its wait has resolved.
- The scheduler must reject SLEEP, suspension and completion while holding a
  mutex. The static script validator already rejects SLEEP/END with a held lock;
  the future interpreter must enforce runtime state changes too.

Destroy the manager only after stopping the run. Destruction frees waiter nodes,
clears their saved handles and clears owners' held-mutex metadata. It does not wake
jobs, emit events or repair scheduler state for continued execution. A fresh run
uses a fresh registry/manager. Do not copy or externally edit resource records,
job identity, resource membership or queue keys.

Resource lookup scans at most eight records. A normal semaphore handoff is O(1)
after lookup; mutex handoff is O(n) when suspended waiters must be skipped.
Storage uses existing allocated circular-list nodes and is bounded by registered
jobs. Allocation and scheduler callback costs are separate from link work.

## Evidence and remaining work

`test_resource_manager` uses real registry jobs, ready rings and a bitmap in a
test-only hook adapter. It checks priority/FIFO order, middle insertion, stable
wait handles, owner-only unlock, nested-lock rejection, counting/binary semaphore
rules, direct grants, overflow, suspended waits, teardown and hook failures.
The scripted producer/consumer test checks P [0,2), C [2,3), P [3,4), completion
times 3/4 and exact CPU totals, against the oracle written independently.

Next: integrate Youssef's production hooks; then inversion-OFF, priority donation
through his requeue hook, inheritance-ON and restoration. The existing timing-wheel
test failure is still outside this component.
