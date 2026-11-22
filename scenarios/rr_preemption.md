# rr_preemption: round-robin and preemption

Golden trace A (plan p.15). Fixed priority, quantum 1, horizon 8.

| Task | Job | Priority | Release | Absolute deadline | Script |
| --- | --- | --- | --- | --- | --- |
| A | 1 | 5 | 0 | 10 | RUN 3; END |
| B | 2 | 5 | 0 | 10 | RUN 3; END |
| H | 3 | 8 | 2 | 4 | RUN 1; END |

**Expected CPU:** A[0,1) B[1,2) H[2,3) A[3,4) B[4,5) A[5,6) B[6,7), then idle.

**Expected results:** completions H=3, A=6, B=7; response times H=1, A=6, B=7. No
deadline is missed. Each job receives exactly its CPU work.

**Look for:** H arrives at 2 and runs at once. A and B keep alternating afterwards,
and neither loses the work it had done.
