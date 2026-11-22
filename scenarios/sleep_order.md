# sleep_order: timer order

Golden trace C (plan p.16) plus one sleeper with an equal wake time. Fixed priority,
all tasks priority 4, released at 100, absolute deadline 140, horizon 140.

| Task | Job | Script | Wakes at |
| --- | --- | --- | --- |
| A | 1 | SLEEP 30; RUN 1; END | 130 |
| B | 2 | SLEEP 10; RUN 1; END | 110 |
| C | 3 | SLEEP 20; RUN 1; END | 120 |
| D | 4 | SLEEP 10; RUN 1; END | 110 |

**Expected wake order:** B@110, D@110, C@120, A@130. B is before D because B's timer
was inserted first.

**Expected CPU:** idle[0,110) B[110,111) D[111,112) idle[112,120) C[120,121)
idle[121,130) A[130,131) idle[131,140).

**Look for:** the `sorted_list` and `heap` runs show the same wake events. Sleeping
jobs use no CPU; the inspector shows each wake tick.
