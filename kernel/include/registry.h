#ifndef B6_KERNEL_REGISTRY_H
#define B6_KERNEL_REGISTRY_H

#include "hash_map.h"
#include "types.h"

namespace b6 {

class Registry {
public:
    // Validates and copies the input; invalid input throws std::invalid_argument.
    explicit Registry(const Scenario& scenario);
    Registry(const Registry&) = delete;
    Registry& operator=(const Registry&) = delete;
    Registry(Registry&&) = delete;
    Registry& operator=(Registry&&) = delete;

    const Scenario& scenario() const noexcept { return scenario_; }
    const TaskSpec* findTask(TaskId id) const;
    Job* findJob(JobId id);
    const Job* findJob(JobId id) const;
    std::size_t jobCount() const noexcept { return jobCount_; }

    // Scheduler calls in (release tick, task ID) order, for ticks < horizon.
    // Records remain at stable addresses until this registry is destroyed.
    Job& releaseJob(TaskId taskId, Tick releaseTick);

private:
    Scenario scenario_;
    Job jobs_[kMaxJobs]{};
    std::size_t jobCount_ = 0;
    HashMap<TaskSpec> tasksById_;
    HashMap<Job> jobsById_;
};

} // namespace b6
#endif
