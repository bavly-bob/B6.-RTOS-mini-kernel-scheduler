# suspend_resume: suspension keeps the reason for waiting

Second half of golden trace C (plan p.16), plus suspension of a ready job. Fixed
priority, horizon 10.

| Task | Job | Priority | Script | Controls |
| --- | --- | --- | --- | --- |
| S | 1 | 5 | SLEEP 5; RUN 2; END | suspend at 2, resume at 7 |
| X | 2 | 3 | RUN 4; END | suspend at 1, resume at 3 |

**Expected CPU:** X[0,1) idle[1,3) X[3,6) idle[6,7) S[7,9) idle[9,10). X completes at
6, S at 9.

| Tick | S must be | Why |
| --- | --- | --- |
| 0-1 | BLOCKED, timer until 5 | It is sleeping. |
| 2-4 | SUSPENDED, still waiting for the timer | Suspension does not cancel the sleep. |
| 5-6 | SUSPENDED, waiting for nothing | The timer fired, but S may not run yet. |
| 7 | RUNNING | Resumed with its wait resolved. |

**Look for:** the idle tick [6,7). S has finished sleeping and the CPU is free, but
only RESUME makes it runnable.
