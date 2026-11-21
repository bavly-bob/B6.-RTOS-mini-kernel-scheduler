# Priority inversion OFF/ON acceptance oracle

Independent expected results from the B6 completion plan, pages 15 and 21.
One CPU, fixed priority, quantum 1 and zero-time LOCK/UNLOCK/END. Horizon 10.
The same inputs are used twice, changing only the inheritance flag.

| Task | Task ID | Release | Base priority | Absolute deadline | Script |
| --- | --- | --- | --- | --- | --- |
| L | 0 | 0 | 1 | 20 | LOCK R; RUN 4; UNLOCK R; END |
| H | 1 | 1 | 3 | 6 | LOCK R; RUN 1; UNLOCK R; END |
| M | 2 | 2 | 2 | 22 | RUN 4; END |

R has resource ID 0. H's relativeDeadline is **5**, since it releases at 1 and
must have absolute deadline 6. L/H/M receive job IDs 1/2/3 in release order.

| Mode | Expected intervals | H completion | H miss | L priority events |
| --- | --- | --- | --- | --- |
| OFF | L [0,2), M [2,6), L [6,8), H [8,9), idle [9,10) | 9 | Once at 6; retained after completion | None |
| ON | L [0,4), H [4,5), M [5,9), idle [9,10) | 5 | None | 1 -> 3 at t=1; 3 -> 1 at t=4 |

H blocks on R during [1,8) with inheritance OFF and [1,4) with it ON. CPU totals
must be L=4, H=1 and M=4 in both runs. All job/resource/ready memberships must be
cleared at completion; repeated fresh runs must produce identical decisions.

The executable `test_resource_manager` runs these scripts using a test-only hook
adapter, compares against the constants above and prints the actual coalesced
intervals and priority-change events. This is component integration evidence, not
an exporter-generated production run. No JavaScript is used to invent the trace.
