# Run file contract: schemaVersion 1

Owner: Sarofem (writer, `app/src/run_exporter.cpp`). Consumer: Bavly
(`dashboard/app.js`). Source: B6 completion plan pp. 17-18. Any change to a
field name or meaning needs a team decision and a new `schemaVersion`.

A run file is one self-contained UTF-8 JSON object. The C++ engine makes every
scheduling decision and computes every metric; the dashboard only displays it.

## General rules

- IDs and ticks are integers. Ticks are simulated time; `tickMs` only scales labels.
- Unknown or not-yet-happened values are `null`, never `0` or the horizon.
  An unfinished job has `completionTick: null`.
- No C++ pointer value, address or memory size is ever written.
- All strings are escaped (RFC 8259). The writer also escapes `<`, `>` and `&`
  as `<`, `>`, `&`; the dashboard shows names and reasons as text only.
- Order is meaningful: `jobs` by job ID, `segments` by time, `events` by
  `(tick, phase, seq)`, `snapshots` by tick, waiter lists by queue order.
- File size limit: 32 MiB (`exportRunToFile` refuses to report success above it).

## Top level

| Field | Type | Meaning |
| --- | --- | --- |
| `schemaVersion` | `1` | The dashboard rejects any other value. |
| `run` | object | Identity of this run. |
| `config` | object | Settings the engine ran with (immutable in the dashboard). |
| `tasks` | array | Task definitions from the scenario. |
| `jobs` | array | Every released job and its final outcome. |
| `segments` | array | CPU intervals, including idle. |
| `events` | array | Ordered explanatory changes. |
| `snapshots` | array | Stable state at each boundary (for the time slider). |
| `metrics` | object | C++-computed results. |
| `validation` | object | Whether the run is complete and trustworthy. |

The dashboard rejects a file missing any of these objects/arrays.

## `run`

| Field | Type | Rule |
| --- | --- | --- |
| `runId` | string | Unique name, e.g. `inversion-on`. |
| `scenarioId` | string | Preset ID, e.g. `priority_inversion`. |
| `label` | string | Human-readable title. |
| `evidenceType` | `"engine_run"` \| `"design_fixture"` | `engine_run` only for output of the real scheduler. Hand-made data is always `design_fixture` (the dashboard shows a "sample data" banner). |
| `engineRevision` | string \| null | Git commit of the build (filled automatically by CMake). |
| `createdAt` | string \| null | ISO-8601 time; null keeps files byte-identical across reruns. |

## `config`

`policy` (`"fixed_priority"`, `"rate_monotonic"`, `"edf"`), `timerBackend`
(`"sorted_list"`, `"heap"`), `inheritance` (bool), `quantumTicks`, `tickMs`,
`horizonTicks`, `contextSwitchCostTicks` (always 0), `limits` (`maxTasks`,
`maxJobs`, `maxMutexes`, `maxSemaphores`, `maxScriptLength`, `maxHorizonTicks`,
`maxEvents`, `maxSnapshotJobRows`) and `restrictions` (list of model limits as text).

## `tasks[]`

`taskId`, `name`, `basePriority` (0-31, larger is more urgent), `firstRelease`,
`periodTicks` (0 = one-shot), `relativeDeadline`, `script` (text such as
`"LOCK 1; RUN 4; UNLOCK 1; END"`; resource operations show the resource ID).

## `jobs[]`

| Field | Type | Rule |
| --- | --- | --- |
| `jobId`, `taskId` | int | `taskId` must exist in `tasks`. |
| `releaseTick`, `absoluteDeadline` | int | Fixed at release. |
| `firstStartTick` | int \| null | First tick it ran. |
| `completionTick` | int \| null | Null if unfinished at the horizon. |
| `totalCpuUsed` | int | Equals the sum of its segment lengths. |
| `finalState` | `READY` \| `RUNNING` \| `BLOCKED` \| `SUSPENDED` \| `COMPLETED` | State at the horizon. |
| `deadlineMissed` | bool | Recorded once; a late completion keeps it true. |
| `deadlineMissTick` | int \| null | Tick the miss was recorded. |

Response time is `completionTick - releaseTick`, and only when completed.

## `segments[]`

`{start, end, jobId, basePriority, effectivePriority}` covering `[start, end)`.
Idle has `jobId`, `basePriority` and `effectivePriority` all `null`.
Rules (checked by the dashboard): `0 <= start < end <= horizonTicks`, sorted,
no overlap, `jobId` known. The engine records one tick at a time; contiguous
ticks of the same job at the same priorities merge, and a priority change
starts a new segment, so inheritance is visible in the Gantt chart.

## `events[]`

| Field | Type | Rule |
| --- | --- | --- |
| `tick` | int | Boundary where it happened. |
| `phase` | 1-6 | 1 settle, 2 inputs, 3 stabilize, 4 deadlines, 5 snapshot, 6 execute. |
| `seq` | int | 1, 2, 3... over the whole run. |
| `type` | string | One of the types below. |
| `jobId`, `taskId`, `resourceId` | int \| null | Whatever applies. |
| `reason` | string | Short snake_case cause, e.g. `mutex_owned`, `inheritance`, `timer`. |
| `data` | object | Type-specific integers/booleans/nulls (at most 4 fields). |

Types: `RELEASE`, `DISPATCH`, `PREEMPT`, `BLOCK`, `WAKE`, `SUSPEND`, `RESUME`,
`COMPLETE`, `LOCK`, `UNLOCK`, `WAIT`, `SIGNAL`, `PRIORITY_CHANGE`,
`DEADLINE_MISS`, `ERROR`.

Recommended `data` fields (plan p.18): `PRIORITY_CHANGE {from, to}`,
`BLOCK {ownerJobId}` for a mutex, `WAKE {suspended}` for a timer wake,
`RELEASE {absoluteDeadline}`, `DISPATCH {priority}`, `SIGNAL {grantedJobId}`.

```json
{"tick":1,"phase":3,"seq":12,"type":"BLOCK","jobId":2,"taskId":2,"resourceId":1,
 "reason":"mutex_owned","data":{"ownerJobId":1}}
{"tick":1,"phase":3,"seq":13,"type":"PRIORITY_CHANGE","jobId":1,"taskId":1,"resourceId":1,
 "reason":"inheritance","data":{"from":1,"to":3}}
```

## `snapshots[]`

One per boundary, taken after phase-3 decisions and before phase 6 executes, so
a selected tick shows the state at that tick, not the job's final state.
Ticks strictly increase (normally `0..horizonTicks`).

| Field | Type | Meaning |
| --- | --- | --- |
| `tick` | int | Boundary tick. |
| `runningJobId` | int \| null | At most one RUNNING job. |
| `readyOrder` | int[] | Runnable jobs, most urgent first, as the scheduler sees them (ring order per priority, or EDF heap order). |
| `jobs` | object[] | One row per job released so far (completed ones too). |
| `mutexes` | object[] | `{resourceId, ownerJobId (or null), waiters: int[]}`. |
| `semaphores` | object[] | `{resourceId, count, maxCount, waiters: int[]}`. |

Job row: `jobId`, `taskId`, `state`, `suspended`, `basePriority`,
`effectivePriority`, `cpuBurstRemaining`, `totalCpuUsed`, `scriptIndex`,
`blockReason` (`none`, `timer`, `mutex`, `semaphore`), `blockedResourceId`,
`heldMutexId`, `wakeTick` (null unless sleeping), `deadlineMissed`.

A suspended sleeper whose timer expired has `state: SUSPENDED`,
`suspended: true`, `blockReason: none`; while still waiting it has
`blockReason: timer` and a `wakeTick`.

Waiter lists use the resource manager's queue order: effective priority
descending, then wait order (FIFO among equals).

## `metrics`

`overall`: `busyTicks`, `observedTicks`, `busyPercent`, `completedJobs`,
`unfinishedJobs`, `deadlineMisses`, `demandUtilization` (null unless periodic),
`rmBound` (null when not applicable), `rmBoundApplicable`, `rmBoundNote`.
`perTask[]`: `taskId`, `completedSamples`, `responseMin`, `responseMean`,
`responseMax`, `jitter` (max - min; null with fewer than two samples), `misses`.
Definitions: plan p.26. If the engine did not compute metrics, `overall` is
`{}`, `perTask` is `[]` and a warning says so.

## `validation`

| Field | Meaning |
| --- | --- |
| `status` | `"ok"` or `"invalid"`. |
| `error` | First error text, or null. |
| `warnings` | Text list (at most 32). |
| `limitsHit` | Names of limits reached: `maxEvents`, `maxSnapshotJobRows`. |
| `traceTruncated` | `true` if any events/snapshots were dropped. A complete accepted run has `false`. |

A truncated or invalid run is still written, so the problem can be inspected,
but it must not be used as demonstration evidence.

## Checks

- `ctest -R run_export` writes two files and `run_export_dashboard_validation`
  runs `validateRun` from `dashboard/app.js` on them (needs Node.js).
- `dashboard/fixtures/rr_preemption.design.json` remains the hand-written sample.
