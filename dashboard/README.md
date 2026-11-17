# B6 Scheduler Dashboard

Open `dashboard/index.html` directly in a browser (no server, no network, no install).
Click **Import run(s)** and pick one or more exported run files. Pick a run from the drop-down.
`fixtures/rr_preemption.design.json` is hand-written golden trace A, labelled `design_fixture`,
for working before the engine exports real runs.

The page only displays what the file contains. It does not run or re-schedule anything.

Self-check: `node dashboard/validate.test.js`

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
| `events`, `snapshots` | must be arrays; displayed from 18 Nov (slider, inspector) |

Rejected on import: bad JSON, files > 32 MiB, wrong schema version, missing objects/arrays,
duplicate task/job IDs, jobs referring to unknown tasks, segments referring to unknown jobs,
malformed/overlapping/out-of-horizon segments, non-integer ticks.
