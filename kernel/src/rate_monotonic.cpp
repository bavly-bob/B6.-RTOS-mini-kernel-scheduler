#include "rate_monotonic.h"

namespace b6 {

void assignRateMonotonicPriorities(Scenario& scenario) {
    const std::size_t count = scenario.taskCount < kMaxTasks ? scenario.taskCount : kMaxTasks;

    // Distinct periods in ascending order (insertion sort; at most 32 tasks).
    Tick periods[kMaxTasks]{};
    std::size_t distinct = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const Tick period = scenario.tasks[i].periodTicks;
        std::size_t at = 0;
        while (at < distinct && periods[at] < period) ++at;
        if (at < distinct && periods[at] == period) continue;
        for (std::size_t j = distinct; j > at; --j) periods[j] = periods[j - 1];
        periods[at] = period;
        ++distinct;
    }

    // Rank 0 (shortest period) -> 31. At most 32 ranks, so never below 0.
    for (std::size_t i = 0; i < count; ++i) {
        std::size_t rank = 0;
        while (periods[rank] != scenario.tasks[i].periodTicks) ++rank;
        scenario.tasks[i].basePriority = 31 - static_cast<int>(rank);
    }
}

} // namespace b6
