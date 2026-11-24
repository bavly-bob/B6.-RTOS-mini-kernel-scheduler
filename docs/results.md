# Results: golden traces, run metrics and timer costs

Marc's results package (plan pp. 15-16, 22, 26), prepared on 22 Nov 2026 from the
commit that adds this file (run files from `d21268b`). Table 1 and the inheritance-on
metrics row were rechecked at `6d6583a`, when the scheduler started handling
inheritance. The timer experiment is shared with Sarofem (timer manager owner);
**Sarofem has not reviewed these tables yet.**

Every table below is printed by a program in this repository, so it can be checked
and rerun; nothing was typed in by hand except the golden values, which come from
[scheduler.md](scheduler.md) and [scenarios](../scenarios/README.md).

```sh
cmake -S . -B build-rel -DCMAKE_BUILD_TYPE=Release && cmake --build build-rel
./build-rel/b6_golden        # Table 1 (exit 1 while any golden trace is unmatched)
./build-rel/b6_timer_cost    # Tables 3 and 4
```

Environment for the numbers here: g++ 13.3 (Ubuntu 24.04 container on Docker
Desktop/WSL 2), Release build, 16 logical CPUs. Only Table 4 depends on it.

## 1. Measured outcomes against the golden traces

Each preset runs through the shared engine on **both** timer backends. "Match" needs
both backends to agree with each other and with the golden trace. Outcome format:
`A:6` completed at 6; `B0:8!7` completed at 8 and missed at 7; `B1:open` unfinished
at the horizon.

| Scenario | Policy | Inheritance | Golden | Measured | Golden outcome | Measured outcome | Backends agree | Match |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `rr_preemption` | fixed_priority | off | A[0,1) B[1,2) H[2,3) A[3,4) B[4,5) A[5,6) B[6,7) idle[7,8) | same | A:6 B:7 H:3 | A:6 B:7 H:3 | yes | yes |
| `sleep_order` | fixed_priority | off | wakes B@110 D@110 C@120 A@130 | same | not stated | A:131 B:111 C:121 D:112 | yes | yes |
| `suspend_resume` | fixed_priority | off | X[0,1) idle[1,3) X[3,6) idle[6,7) S[7,9) idle[9,10) | same | S:9 X:6 | S:9 X:6 | yes | yes |
| `semaphore_signal` | fixed_priority | off | P[0,2) C[2,3) P[3,4) idle[4,5) | same | C:3 P:4 | C:3 P:4 | yes | yes |
| `priority_inversion` | fixed_priority | off | L[0,2) M[2,6) L[6,8) H[8,9) idle[9,10) | same | L:8 H:9!6 M:6 | L:8 H:9!6 M:6 | yes | yes |
| `priority_inversion` | fixed_priority | **on** | L[0,4) H[4,5) M[5,9) idle[9,10) | same | L:4 H:5 M:9 | L:4 H:5 M:9 | yes | yes |
| `rm_edf_compare` | rate_monotonic | off | A0[0,2) B0[2,5) A1[5,7) B0[7,8) | same | A0:2 B0:8!7 A1:7 B1:open | same | yes | yes |
| `rm_edf_compare` | edf | off | A0[0,2) B0[2,6) A1[6,8) | same | A0:2 B0:6 A1:8 B1:open | same | yes | yes |
| `rm_edf_compare` | fixed_priority | off | A0[0,2) B0[2,5) A1[5,7) B0[7,8) | same | A0:2 B0:8!7 A1:7 B1:open | same | yes | yes |
| `overload` | fixed_priority | off | 9 jobs, misses > 0, idle 0 | 9 jobs, misses 4, idle 0 | not stated | A0:3 B0:16!5 A1:7 B1:open!10 A2:11 B2:open!15 A3:15 B3:open!20 A4:19 | yes | yes |
| `overload` | rate_monotonic | off | 9 jobs, misses > 0, idle 0 | 9 jobs, misses 4, idle 0 | not stated | same as fixed_priority | yes | yes |
| `overload` | edf | off | 9 jobs, misses > 0, idle 0 | 9 jobs, misses 8, idle 0 | not stated | A0:3 B0:6!5 A1:9!8 B1:12!10 A2:15!12 B2:18!15 A3:open!16 B3:open!20 A4:open!20 | yes | yes |

**12 of 12 match on both backends.** With inheritance ON the run file shows L as
two segments, L[0,1) and L[1,4), because the schema starts a new segment when the
priority changes (L is boosted from 1 to 3 at tick 1). `b6_golden` joins adjacent
segments of the same job before comparing, since golden traces state who owns the
CPU, not at which priority.

`sleep_order` and `overload` golden traces state a wake order and properties, not
completions; the measured completions are listed for the record.

## 2. Run metrics (from the exported run files)

Values from `runs/*-heap.json` (the sorted_list files are identical). Definitions:
[run-schema-v1.md](run-schema-v1.md#metrics). Response = completion − release, for
completed jobs only (late ones included); jitter = max − min, "–" with one sample.

| Scenario | Policy | CPU busy | Completed / unfinished / missed | Demand | RM bound | Note |
| --- | --- | --- | --- | --- | --- | --- |
| `rr_preemption` | fixed_priority | 7/8 (87.5%) | 3 / 0 / 0 | – | – | one-shot tasks |
| `sleep_order` | fixed_priority | 4/140 (2.86%) | 4 / 0 / 0 | – | – | one-shot tasks |
| `suspend_resume` | fixed_priority | 6/10 (60%) | 2 / 0 / 0 | – | – | one-shot tasks |
| `semaphore_signal` | fixed_priority | 4/5 (80%) | 2 / 0 / 0 | – | – | one-shot tasks |
| `priority_inversion` | fixed_priority | 9/10 (90%) | 3 / 0 / 1 | – | – | one-shot tasks |
| `priority_inversion` (inheritance on) | fixed_priority | 9/10 (90%) | 3 / 0 / 0 | – | – | one-shot tasks; H response 4 instead of 8 |
| `rm_edf_compare` | rate_monotonic | 8/8 (100%) | 3 / 1 / 1 | 97.14% | 82.84% | demand above the bound is inconclusive |
| `rm_edf_compare` | edf | 8/8 (100%) | 3 / 1 / 0 | 97.14% | – | policy is edf |
| `rm_edf_compare` | fixed_priority | 8/8 (100%) | 3 / 1 / 1 | 97.14% | – | policy is fixed_priority |
| `overload` | rate_monotonic | 20/20 (100%) | 6 / 3 / 4 | 135% | 82.84% | demand above 100%: overload |
| `overload` | edf | 20/20 (100%) | 6 / 3 / 8 | 135% | – | policy is edf |
| `overload` | fixed_priority | 20/20 (100%) | 6 / 3 / 4 | 135% | – | policy is fixed_priority |

Per-task response times for the periodic comparisons:

| Scenario | Policy | Task | Samples | Response min / mean / max | Jitter | Misses |
| --- | --- | --- | --- | --- | --- | --- |
| `rm_edf_compare` | rate_monotonic | A (C2, T5) | 2 | 2 / 2 / 2 | 0 | 0 |
| `rm_edf_compare` | rate_monotonic | B (C4, T7) | 1 | 8 / 8 / 8 | – | 1 |
| `rm_edf_compare` | edf | A | 2 | 2 / 2.5 / 3 | 1 | 0 |
| `rm_edf_compare` | edf | B | 1 | 6 / 6 / 6 | – | 0 |
| `overload` | rate_monotonic | A (C3, T4) | 5 | 3 / 3 / 3 | 0 | 0 |
| `overload` | rate_monotonic | B (C3, T5) | 1 | 16 / 16 / 16 | – | 4 |
| `overload` | edf | A | 3 | 3 / 5 / 7 | 4 | 4 |
| `overload` | edf | B | 3 | 6 / 7 / 8 | 2 | 4 |

**Reading the comparison.**

- At 97% demand (above the two-task RM bound of 82.84%, below 100%) EDF meets every
  deadline in the horizon and RM misses B0 at 7. The bound alone could not decide
  this ("inconclusive"); the trace does.
- RM buys A a zero-jitter response of 2 by delaying B; EDF spreads the delay over
  both tasks (A's jitter rises to 1).
- Under overload (135%) the outcome flips: RM keeps the short-period task A perfect
  and sacrifices B (4 misses, all on B), while EDF misses 8 deadlines spread over
  both tasks. This is EDF's known domino effect when demand exceeds 100%, shown here
  in a 20-tick run, not a general measurement.
- RM and fixed_priority agree on both periodic presets because their hand-set
  priorities already follow period order.

## 3. Timer operations: key comparisons (exact)

Both backends hold the same pending timers and perform the same operations, in the
order the timer manager uses them: *insert* (`sleep`), *cancel by ID* (`cancel`, n/10
distinct IDs), then *expire* (pop every remaining timer in wake order, as
`expireDue`). Counts are `operator<` plus `operator==` calls per operation, from the
same `sortedList` and `MinHeap` templates with a counting entry type. *Random*: wake
ticks drawn from a seeded generator in [1, 4n]. *Ascending*: each new timer wakes last
(the common case when jobs sleep for similar times).

| Pending timers | Wake ticks | Backend | Insert | Cancel by ID | Expire (pop) | Same expiry order |
| --- | --- | --- | --- | --- | --- | --- |
| 10 | random | sorted_list | 3.1 | 5.0 | 0.0 | yes |
| 10 | random | heap | 1.3 | 6.0 | 2.2 | yes |
| 10 | ascending | sorted_list | 1.8 | 7.0 | 0.0 | yes |
| 10 | ascending | heap | 0.9 | 8.0 | 2.4 | yes |
| 100 | random | sorted_list | 26.7 | 37.6 | 0.0 | yes |
| 100 | random | heap | 1.9 | 43.2 | 8.3 | yes |
| 100 | ascending | sorted_list | 2.0 | 61.0 | 0.0 | yes |
| 100 | ascending | heap | 1.0 | 65.5 | 8.3 | yes |
| 500 | random | sorted_list | 122.6 | 218.2 | 0.0 | yes |
| 500 | random | heap | 2.2 | 221.7 | 12.7 | yes |
| 500 | ascending | sorted_list | 2.0 | 246.3 | 0.0 | yes |
| 500 | ascending | heap | 1.0 | 256.8 | 12.7 | yes |

## 4. Timer operations: time per operation (this machine only)

The real `TimerQueueAdapter` classes, the heap reserved to `maxJobs` as in a run.
Median nanoseconds per operation over 2001 repetitions of the whole sequence.

| Pending timers | Wake ticks | Backend | Insert | Cancel by ID | Expire (pop) |
| --- | --- | --- | --- | --- | --- |
| 10 | random | sorted_list | 19.2 | 58.0 | 20.2 |
| 10 | random | heap | 28.0 | 78.0 | 31.1 |
| 10 | ascending | sorted_list | 23.5 | 69.0 | 20.3 |
| 10 | ascending | heap | 16.6 | 78.0 | 42.3 |
| 100 | random | sorted_list | 61.4 | 92.7 | 13.2 |
| 100 | random | heap | 34.9 | 72.9 | 96.0 |
| 100 | ascending | sorted_list | 25.4 | 164.5 | 13.2 |
| 100 | ascending | heap | 11.8 | 74.5 | 96.8 |
| 500 | random | sorted_list | 401.8 | 701.5 | 12.5 |
| 500 | random | heap | 40.2 | 182.3 | 143.5 |
| 500 | ascending | sorted_list | 24.9 | 712.4 | 11.4 |
| 500 | ascending | heap | 11.1 | 196.3 | 144.2 |

**Reading the timer results.**

- **Same behaviour:** in all 6 workloads both backends expire the remaining timers in
  exactly the same order, which is the precondition for comparing their costs.
- **Insert:** the sorted list's cost grows with n for random wake ticks (122.6
  comparisons and ~400 ns at 500 timers) but stays at ~2 comparisons for ascending
  ones, because of its O(1) tail shortcut. The heap stays at 1-2 comparisons either
  way (O(log n) worst case; random keys rarely sift far).
- **Expire:** the sorted list pops with no comparisons (O(1) unlink); the heap pays
  its O(log n) sift-down, 12.7 comparisons and ~144 ns per pop at 500.
- **Cancel by ID:** both scan linearly by ID, so the comparison counts are close
  (the heap adds a few for its repair). In time the heap's array scan is 3.6-3.8x
  faster than the list's pointer chase at 500 timers.
- **For this simulator:** at 10 timers every operation is tens of nanoseconds on
  either backend; the choice only matters at hundreds of pending timers. There the
  heap is better when sleeps are inserted out of order or cancelled, and the sorted
  list is better when sleeps arrive in wake order and are mostly expired. Each
  boundary also scans every job for deadlines and snapshots (O(jobs)), which costs
  more than any timer operation here.

**Limits.** Wall-clock figures come from one container on one machine; the sorted
list also allocates one node per insert, which these times include, while the heap
was reserved in advance. Only costs inside the timer queue are measured, not a whole
`expireDue` call with its job updates. The `TimingWheel` is a separate structure, not a
timer backend, and is not measured.
