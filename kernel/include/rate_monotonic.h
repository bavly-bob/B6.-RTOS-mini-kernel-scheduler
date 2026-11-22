#ifndef B6_KERNEL_RATE_MONOTONIC_H
#define B6_KERNEL_RATE_MONOTONIC_H

#include "types.h"

namespace b6 {

// Rate-monotonic priority assignment (Marc). A shorter period means a higher
// priority: the shortest period gets 31, each longer distinct period one less.
// Tasks with equal periods share a level, so they share a ready ring and take
// round-robin turns. The result depends only on the periods, never on task
// order, and adding a longer-period task does not change existing levels.
//
// Overwrites basePriority on every task. It never indexes past kMaxTasks and
// does not validate: run validateScenario afterwards (a non-periodic task is
// still ranked here, then rejected there for RM).
void assignRateMonotonicPriorities(Scenario& scenario);

} // namespace b6
#endif
