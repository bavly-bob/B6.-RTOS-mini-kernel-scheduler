# rm_edf_compare: rate monotonic against EDF

Golden trace E (plan p.16). Independent periodic tasks, horizon 8, no resources.

| Task | CPU per job | Period = relative deadline | Base priority |
| --- | --- | --- | --- |
| A | 2 | 5 | 2 |
| B | 4 | 7 | 1 |

Releases are the same under both policies: A0 and B0 at 0, A1 at 5, B1 at 7.

| Policy | Expected CPU | B0 (deadline 7) |
| --- | --- | --- |
| RM (shorter period first) | A0[0,2) B0[2,5) A1[5,7) B0[7,8) | completes 8, missed at 7 |
| EDF | A0[0,2) B0[2,6) A1[6,8) | completes 6, met |

B1 is released but unfinished at the horizon in both runs. It stays visible with no
completion or response time.

Demand is 2/5 + 4/7 = 34/35, about 97.14%. The two-task RM sufficient bound is about
82.84%. Being above the bound does not prove a miss; the trace is the evidence here.

**Look for:** at 5, RM preempts B0 for A1 because A has the shorter period. EDF keeps
B0 because its deadline (7) is earlier than A1's (10).

**Status:** `--policy rate_monotonic` and `--policy edf` both run. RM ignores the
preset's base priorities and assigns A 31 and B 30 from the periods
(`kernel/src/rate_monotonic.cpp`). `--policy fixed_priority` gives the same CPU trace,
because the preset's own priorities (A 2, B 1) already follow period order.
