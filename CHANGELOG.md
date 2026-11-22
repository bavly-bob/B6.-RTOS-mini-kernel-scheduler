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
