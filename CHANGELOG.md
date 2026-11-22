# Changelog

Changes to Bavly's work package (completion plan p.23: timing wheel, dashboard,
import checks, scenario presentation, documentation assembly), newest phase last.
Page numbers refer to the completion plan.

## Baseline before these changes (main at ed29b62)

- Built with CMake, MinGW g++ 15.2, Debug: compiles without errors.
- `ctest`: 9 of 11 pass. **`scheduler` and `scheduler_runs_dashboard_validation`
  already failed.** `test_scheduler` expects `--inheritance on` to be refused, but
  `ResourceManager` now accepts it, and `Scheduler` does not yet override
  `onPriorityChanged`. The second test fails because the first one stops before
  writing its run files. Nothing in this changelog touches those files.
- Timing wheel: the repairs and regression tests from plan p.6 (commit eb6b85e)
  were rechecked. The data-structure suite passes under AddressSanitizer and
  UndefinedBehaviorSanitizer (g++, Ubuntu on WSL). No wheel change was needed.

## Phase 1: Complete import validation

**What changed**
- `validateRun` now checks every part of a run file instead of only tasks, jobs
  and segments.
  - Config: known policy and timer backend, boolean inheritance, quantum of at
    least 1, horizon from 0 to 2,000 (the kernel's ceiling).
  - Unsupported combinations: `edf` or `rate_monotonic` with inheritance, or with
    any mutex or semaphore.
  - Jobs: nonnegative ticks, known final state, and first start, completion and
    miss ticks that lie between the job's release and the horizon.
  - Segments: integer priorities from 0 to 31 on every non-idle interval.
  - Events: known type and references, phase from 1 to 6, increasing `seq`, and
    `(tick, phase, seq)` order.
  - Snapshots: strictly increasing ticks, at most one RUNNING job that matches
    `runningJobId`, known and non-duplicated jobs, waiters and resources, and
    semaphore counts from 0 to `maxCount`.
  - Metrics and validation blocks: required shape, and status `ok` cannot be
    combined with a truncated trace.
- New `parseRun(text)` joins JSON parsing and validation.

**Why it changed**
- Plan p.18 lists these rejections as acceptance checks and p.23 assigns the
  wrong-file tests. Snapshots and events were previously accepted unchecked, so a
  bad file would have reached the time slider.
- A `null` row in an array threw a `TypeError` inside the importer, so the user
  saw no message at all.

**How it works**
- One `need(condition, message)` helper collects messages, so a file reports all
  its problems and each message names the failing field.
- Array rows are checked to be objects first; later checks can then read fields
  safely.
- Snapshots are read before events because snapshots are the only place the file
  lists its resources. A file without snapshots can only have its event resource
  IDs checked for type.
- Runs with `status: "invalid"` are still accepted so they can be inspected, as
  `docs/run-schema-v1.md` requires.

**Files affected**
- `dashboard/app.js`: validator, `parseRun`, import handler uses `parseRun`.
- `dashboard/validate.test.js`: about 100 rejection cases; also loads every
  fixture and every file in `runs/`.
- `dashboard/README.md`: list of import checks.
- `CHANGELOG.md`: created.

**Verification**
- The new test was run before the implementation and failed
  (`parseRun is not a function`).
- `node dashboard/validate.test.js`: passed.
- `node tests/validate_export.js` and `node tests/validate_runs.js` against files
  from a fresh build (18 engine runs): passed, so the stricter checks agree with
  the C++ exporter.

## Phase 2: Selected-tick playback and inspection

**What changed**
- Playback controls: Reset, Previous, Play/Pause, Next and a tick slider, with a
  cursor line on the Gantt chart.
- Inspector for one job: state, base and effective priority, deadline, what it
  waits for and who owns it, held mutex, CPU left in the current RUN, CPU used,
  next script step, and whether the deadline has been missed *by that tick*.
- Tables for all job states and for resources (mutex owner, semaphore permits,
  ordered waiters), plus running job, ready order and sleeping jobs.
- Event list around the selected tick. Selecting an event jumps to it.
- The old "Jobs" table is now titled "Final outcomes" so end-of-run values are
  not mistaken for the selected tick.

**Why it changed**
- Plan p.23 (18 Nov task), the p.19 dashboard areas, and the p.25 acceptance test
  "Snapshot accuracy: slider shows historical state, not final-state leakage",
  which is assigned to Bavly.

**How it works**
- All lookups are pure functions (`snapshotAt`, `inspectJob`, `resourcesAt`,
  `eventsNear`, `describeEvent` and others) with no DOM access, so they are tested
  in Node. The browser part only renders their results with `textContent`.
- `snapshotAt` matches the tick exactly. A missing snapshot is reported on the
  page; state is never taken from a neighbouring tick or from `jobs[].finalState`.
- A job that is READY before its first RUN exports `cpuBurstRemaining: 0`. Showing
  0 would look finished, so the view says "not started" when the next step is RUN.
- Play advances the selected tick with a timer (50 to 600 ms per tick depending on
  the horizon) and stops at the horizon. It runs no scheduling logic.

**Files affected**
- `dashboard/app.js`: view functions, tick rendering, playback handlers.
- `dashboard/index.html`: three-column layout, controls and new tables.
- `dashboard/styles.css`: layout, cursor, controls, event list.
- `dashboard/view.test.js`: created.
- `dashboard/README.md`: usage and interpretation rules.

**Verification**
- `node dashboard/view.test.js` failed first (`inspectJob is not a function`), then
  passed. It also checks every tick of all 18 files in `runs/`: the RUNNING row of
  the snapshot is the job the CPU segment shows.
- `node dashboard/validate.test.js`: passed.
- The page was driven in headless Chrome from `file://` (script added in Phase 4):
  import, slider, buttons, playback and event jumps behaved as expected.

## Phase 3: Inversion, policy comparison, metrics and trust notes

**What changed**
- Gantt chart: boosted intervals are outlined and labelled with the effective
  priority; a missed deadline is marked where it was recorded.
- "Key events" list: every `PRIORITY_CHANGE`, `DEADLINE_MISS` and `ERROR` in the
  run, each jumping to its tick and job.
- Switching between runs with the same `scenarioId` keeps the tick and job.
- Metrics have labels and units, "insufficient samples" for jitter, and "not
  applicable" with the exported reason for the RM bound. A run without metrics
  says so instead of showing empty tables.
- Notes above the run: invalid status and its error, limits reached, truncation
  and exporter warnings.
- `fixtures/inversion_on.design.json`: golden trace B with inheritance ON.

**Why it changed**
- Plan p.23 (19 Nov task): show inversion OFF/ON and RM/EDF runs in one viewer with
  base/effective priority changes and blocking reasons. Plan p.8 asks to inspect
  the event responsible for a missed deadline or a priority change. Plan p.26
  fixes how each metric may be presented.

**How it works**
- `keyEvents`, `overallMetrics`, `taskMetrics` and `runNotes` are pure functions over
  exported fields. No metric is calculated in JavaScript; values are only rounded
  to two decimals, and `demandUtilization`/`rmBound` fractions shown as percentages.
- A boost is `effectivePriority > basePriority` of the exported segment.
- The ON fixture was derived by hand from plan p.15 and
  `scenarios/priority_inversion.md`, because the engine on `main` cannot export an ON
  run yet (see "Baseline"). It is a `design_fixture` and shows the sample banner.
- `validation.limitsHit` is now iterated, so the importer also checks it is an array.

**Files affected**
- `dashboard/app.js`, `dashboard/index.html`, `dashboard/styles.css`: the features above;
  import button text now has enough contrast in dark mode.
- `dashboard/fixtures/inversion_on.design.json`: created.
- `dashboard/view.test.js`, `dashboard/validate.test.js`: new cases.
- `dashboard/README.md`: interpretation rules.

**Verification**
- `node dashboard/view.test.js` failed first, then passed; `node dashboard/validate.test.js`
  passed. Both exporter cross-checks in `tests/` passed on a fresh build.
- The ON fixture's tasks, jobs, segments and 11 snapshots were compared with a run
  from a scratch copy of the engine in which `Scheduler::onPriorityChanged` was
  implemented: identical. That scratch change is not part of this repository.
