# Scenarios

Each preset is built in `app/src/presets.cpp`. The files here say in plain language
what a preset shows and what must happen. Expected values come from the completion
plan (pp. 14-16) or are worked out by hand from the stated rules; they are the
acceptance oracle, not a copy of program output.

| Preset | Shows | Details |
| --- | --- | --- |
| `rr_preemption` | Equal priorities take turns; an urgent job preempts | [rr_preemption.md](rr_preemption.md) |
| `sleep_order` | Sleepers wake in timer order on both backends | [sleep_order.md](sleep_order.md) |
| `suspend_resume` | Suspension keeps an unresolved wait | [suspend_resume.md](suspend_resume.md) |
| `semaphore_signal` | A consumer waits for a producer | [semaphore_signal.md](semaphore_signal.md) |
| `priority_inversion` | A medium job delays a high one through a mutex | [priority_inversion.md](priority_inversion.md) |
| `rm_edf_compare` | Same releases, different decisions | [rm_edf_compare.md](rm_edf_compare.md) |
| `overload` | Demand above 100%: misses and backlog stay visible | [overload.md](overload.md) |
| validation cases | Bad input gives a clear error | [validation_cases.md](validation_cases.md) |

Run one with `b6_sim --scenario <preset>`, then import the run file in
`dashboard/index.html`. One CPU, integer ticks, zero switching cost; an interval
`[a,b)` includes tick `a` and not tick `b`.
