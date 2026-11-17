# Producer/consumer acceptance oracle

Independent expected result from the completion plan, before GUI integration.
Fixed priority, inheritance off, quantum 1, one CPU and zero-time resource actions.
Both tasks release at tick 0. Semaphore S has initial count 0 and maximum count 1.

| Task | ID | Priority | Script |
| --- | --- | --- | --- |
| Consumer C | 0 | 3 | WAIT S; RUN 1; END |
| Producer P | 1 | 1 | RUN 2; SIGNAL S; RUN 1; END |

| Boundary | Expected decision |
| --- | --- |
| 0 | C waits and blocks. P starts. |
| 1 | P continues its first CPU burst. |
| 2 | P's burst ends; SIGNAL directly grants C's WAIT. C preempts P. |
| 3 | C completes. P resumes. |
| 4 | P completes. |

Expected CPU intervals: **P [0,2), C [2,3), P [3,4)**.
Per-tick job owners: **P, P, C, P**.
C consumes 1 CPU tick, P consumes 3. C completes at 3, P at 4.
The semaphore count remains zero throughout: its one signal is transferred directly
to C, not stored as a permit that another waiter could steal. There is exactly one
block notification and one ready notification for this wait.

`test_resource_manager` executes these scripts using a test-only scheduler adapter
and compares against these constants. This verifies the resource integration seam;
it is not an exported production scheduler run or dashboard evidence.
