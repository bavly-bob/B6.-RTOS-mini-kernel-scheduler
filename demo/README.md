# Milestone 2 demonstration

Everything needed for the demonstration, with no network. The real runs are in
[`runs/`](../runs); this folder adds the steps and the screenshots.

## Before you start

1. Build and test from a clean checkout (commands in the [main README](../README.md)).
2. Open `dashboard/index.html` in a browser by double-clicking it.
3. Click **Import run(s)** and select the files for the part you are showing. Several
   files can be selected together.

Screenshots in [`screenshots/`](screenshots) are named `<runId>-t<tick>` and are a
fallback if a live import is not possible. All were taken from `engine_run` files
exported at engine revision `b2b0837`.

## Suggested eight minutes (plan p.25)

| Time | Show | Speaker | Files to import |
| --- | --- | --- | --- |
| 0:00-1:00 | Problem, data structures, CLI to JSON to dashboard | Youssef | none |
| 1:00-2:00 | Round-robin, preemption, state inspector | Youssef | `rr_preemption-fixed_priority-heap` |
| 2:00-3:00 | Same wake order on two timer backends | Sarofem | `sleep_order-fixed_priority-sorted_list`, `-heap` |
| 3:00-5:00 | Inversion OFF/ON, owner, waiter, priority; semaphore | Moataz | `priority_inversion-fixed_priority-heap`, `-heap-inheritance`, `semaphore_signal-fixed_priority-heap` |
| 5:00-6:30 | RM against EDF, deadline result | Marc | `rm_edf_compare-rate_monotonic-heap`, `rm_edf_compare-edf-heap` |
| 6:30-8:00 | Inspection, metrics, design, limits, next phase | Bavly | `overload-fixed_priority-heap` |

## What to click

**Round-robin.** Next from tick 0 to 3. At 2, H preempts and *Ready* lists A then B.

**Timers.** Import both files, go to 110, switch between the two runs: the tick is kept
and both show B then D waking.

**Inversion OFF.** Choose H in the inspector and go to 4: BLOCKED on mutex 0, owned by L,
while M runs. In *Key events* select the miss at 6: the chart shows ✕. H completes at 9.

**Inversion ON.** Import `priority_inversion-fixed_priority-heap-inheritance` (a real
engine run). Go to 1: H blocks on mutex 0 and L is boosted from 1 to 3, so M cannot run
when it arrives at 2. At 4 L unlocks and returns to 1; H gets the mutex and completes at
5, before its deadline 6. Switch to the OFF run at the same tick to compare.

**Semaphore.** Tick 1: C waits on semaphore 0. Tick 2: P signals and C runs; the count
stays 0 because the permit goes straight to C.

**RM and EDF.** Go to 5 in the first run: RM gave A priority 31 and B 30 from their
periods, so A1 preempts B0. Go to 7, then switch: RM misses B0 at 7, EDF has finished it
at 6. B1 is listed as "not completed" in both.

**Overload.** Key events lists four misses; *Final outcomes* keeps all nine jobs.

**Wrong file.** Import this README: the message names the file and the reason.

## Closing narration (Bavly)

"A task missed its deadline. The scheduler knows why, but normally nobody can see it.
Here is the recorded run: at tick 6 the high-priority job is blocked, and the page names
the mutex and the low-priority job that holds it. Nothing on this page is calculated in
the browser. Every state and event comes from the C++ engine, and a file that does not
add up is refused. The model is deliberately small: one CPU, integer ticks, no switching
cost, one mutex per job. Inside those limits, every run can be repeated and explained."

## Known limits to state openly

- None open. (`--inheritance on` is connected end to end since commit `60efef5`.)

When one of these lands, export the new run, replace the fixture step, run
`node dashboard/browser.check.js demo/screenshots`, and update this list.
