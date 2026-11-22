#ifndef B6_APP_METRICS_H
#define B6_APP_METRICS_H

#include "run_exporter.h"

namespace b6 {

// Run metrics (Marc), computed only from the finished registry and trace;
// the dashboard displays them and never recomputes anything.
//
// Overall:
// - busyTicks: CPU ticks used by jobs (non-idle segments); observedTicks is
//   the horizon; busyPercent = 100 * busy / observed (observed utilization).
// - completedJobs / unfinishedJobs: released jobs with / without a completion
//   by the horizon. deadlineMisses: jobs flagged missed (each counted once).
// - demandUtilization: sum(Ci/Ti), Ci = the RUN ticks of one job. Only for
//   a set where every task is periodic; otherwise nullopt.
// - rmBound: n(2^(1/n) - 1) (Liu and Layland), set only when it applies:
//   a rate_monotonic run of independent periodic tasks (no mutexes,
//   semaphores or SLEEP) whose deadlines equal their periods. rmBoundNote
//   gives the first reason it does not apply, or what the demand means.
// Per task (input order): response = completion - release for completed jobs
// only, late ones included; jitter = max - min, nullopt below two samples;
// misses = this task's jobs flagged missed.
RunMetrics computeMetrics(const Registry& registry, const RunTrace& trace);

} // namespace b6
#endif
