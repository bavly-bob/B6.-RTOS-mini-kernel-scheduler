# Validation cases: bad input gives a clear error

Plan pp. 14 and 25. Every case must stop with a message, without a crash, a hang or
a plausible-looking partial result.

## Simulator (`b6_sim`)

All of these exit with code 2 and write no run file.

| Command options | Message contains |
| --- | --- |
| `--scenario nope` | unknown scenario: nope |
| `--scenario rr_preemption --policy lottery` | unknown policy: lottery |
| `--scenario rr_preemption --timer wheel` | unknown timer backend: wheel |
| `--scenario rr_preemption --inheritance maybe` | --inheritance takes on or off |
| `--scenario rr_preemption --quantum 0` | quantumTicks must be positive |
| `--scenario rr_preemption --speed 3` | unrecognized or incomplete option |
| `--scenario priority_inversion --policy edf` | require independent jobs without resources |
| `--scenario rr_preemption --policy edf` | must be periodic |
| `--scenario rm_edf_compare --policy edf --inheritance on` | without resources or inheritance |

Scenario, script, resource and capacity errors inside the kernel are covered by
`tests/test_kernel_foundation.cpp`, `tests/test_resource_manager.cpp` and
`tests/test_scheduler.cpp`.

## Dashboard import

`dashboard/validate.test.js` holds one case per rule listed in
`dashboard/README.md`; `dashboard/browser.check.js` shows six wrong files being refused
in a real browser (see `demo/screenshots/rejected-files.png`).
