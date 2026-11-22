# B6 Scheduler Dashboard

Open `dashboard/index.html` directly in a browser (no server, no network, no install).
Click **Import run(s)** and pick one or more exported run files. Pick a run from the drop-down.
`fixtures/rr_preemption.design.json` is hand-written golden trace A, labelled `design_fixture`,
for working before the engine exports real runs.

The page only displays what the file contains. It does not run or re-schedule anything.

Self-checks: `node dashboard/validate.test.js` (import) and `node dashboard/view.test.js` (selected tick).

## Using it

- **Reset, Previous, Play/Pause, Next** and the slider only change the selected tick of the recording.
- Choose a job in the **Inspector** or select a row in **Job states**.
- Select an event to jump to its tick and job.
- The view at tick `t` is the snapshot recorded at `t`: decisions at `t` have been made, `[t, t+1)` has not run.
  If a file has no snapshot at that tick the page says so; it never borrows another tick's state.
- **Final outcomes** is the only table that shows the end of the run instead of the selected tick.
- "CPU left in this RUN" is the rest of the current RUN step. "not started" means the step has not been loaded yet.

## Fields the dashboard currently reads (schemaVersion 1)

This is the consumer side of the contract for Sarofem's exporter. Names follow `kernel/include/types.h`.
Unknown times are JSON `null`, never 0 or the horizon.

| Field | Used for |
| --- | --- |
| `schemaVersion` = 1 | any other value is rejected |
| `run.{runId, scenarioId, label, evidenceType, engineRevision}` | metadata; `evidenceType` must be `engine_run` or `design_fixture` (sample banner) |
| `config.{policy, timerBackend, inheritance, quantumTicks, tickMs, horizonTicks, contextSwitchCostTicks}` | metadata; `horizonTicks` sets the Gantt width |
| `tasks[].{taskId, name, basePriority, firstRelease, periodTicks, relativeDeadline, script}` | task table, lane names; `script` is a display string |
| `jobs[].{jobId, taskId, releaseTick, absoluteDeadline, firstStartTick, completionTick, totalCpuUsed, finalState, deadlineMissed, deadlineMissTick}` | one Gantt lane per job, release/deadline markers, job table |
| `segments[].{start, end, jobId, basePriority, effectivePriority}` | CPU bars; `jobId: null` is idle; sorted, non-overlapping, `0 <= start < end <= horizon` |
| `metrics.overall` (any key/value pairs) | shown as-is |
| `metrics.perTask[].{taskId, completedSamples, responseMin, responseMean, responseMax, jitter, misses}` | per-task table; `jitter: null` with < 2 samples shows "insufficient samples" |
| `validation.{status, traceTruncated}` | metadata |
| `snapshots[]` | slider, inspector, job states, resources, ready order and sleepers; looked up by exact `tick` |
| `events[]` | event list; `type`, `reason`, `resourceId` and `data` are shown as text |

## Import checks

A file is rejected, with the failing field named, for:

- text that is not JSON, files over 32 MiB, or a `schemaVersion` other than 1;
- missing objects or arrays, or rows that are not objects;
- unknown policy, timer backend, state, block reason or event type;
- `edf` or `rate_monotonic` combined with inheritance, mutexes or semaphores;
- duplicate task, job or resource IDs, or references to unknown ones;
- intervals that are negative, empty, overlapping, out of order or past the horizon;
- events out of `(tick, phase, seq)` order, or snapshot ticks that do not increase;
- more than one RUNNING job, or a `runningJobId` that disagrees with the rows;
- semaphore counts outside `0..maxCount`, or a horizon above 2,000 ticks.

A run whose own `validation.status` is `invalid` is still accepted for inspection.
