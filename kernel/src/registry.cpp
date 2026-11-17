#include "registry.h"
#include "validation.h"
#include <stdexcept>

namespace b6 {

Registry::Registry(const Scenario& scenario) : scenario_(scenario) {
    const std::string error = validateScenario(scenario_);
    if (!error.empty()) throw std::invalid_argument(error);
    // Reserve bucket arrays before releases; map nodes still allocate on put().
    tasksById_.resize(kMaxTasks * 2);
    jobsById_.resize(kMaxJobs * 2);
    for (std::size_t i = 0; i < scenario_.taskCount; ++i)
        tasksById_.put(scenario_.tasks[i].taskId, &scenario_.tasks[i]);
}

const TaskSpec* Registry::findTask(TaskId id) const { return tasksById_.get(id); }
Job* Registry::findJob(JobId id) { return jobsById_.get(id); }
const Job* Registry::findJob(JobId id) const { return jobsById_.get(id); }

Job& Registry::releaseJob(TaskId taskId, Tick releaseTick) {
    const TaskSpec* task = findTask(taskId);
    if (task == nullptr) throw std::invalid_argument("release references an unknown taskId");
    if (releaseTick < task->firstRelease || releaseTick >= scenario_.config.horizonTicks)
        throw std::invalid_argument("release tick is before firstRelease or outside the horizon");
    if ((task->periodTicks == 0 && releaseTick != task->firstRelease) ||
        (task->periodTicks > 0 && (releaseTick - task->firstRelease) % task->periodTicks != 0))
        throw std::invalid_argument("release tick does not match the task's release definition");
    if (jobCount_ > 0) {
        const Job& previous = jobs_[jobCount_ - 1];
        if (releaseTick < previous.releaseTick ||
            (releaseTick == previous.releaseTick && taskId <= previous.taskId))
            throw std::invalid_argument("release order must increase by (tick, taskId), without duplicates");
    }
    if (jobCount_ >= scenario_.config.limits.maxJobs)
        throw std::length_error("released jobs exceed config.limits.maxJobs");

    Job& job = jobs_[jobCount_];
    job = Job{};
    job.jobId = static_cast<JobId>(jobCount_ + 1);
    job.taskId = taskId;
    job.releaseTick = releaseTick;
    job.absoluteDeadline = releaseTick + task->relativeDeadline;
    job.effectivePriority = task->basePriority;
    job.releaseSequence = jobCount_ + 1;
    job.quantumRemaining = scenario_.config.quantumTicks;
    // Publish only after map insertion succeeds; failed allocation consumes no ID.
    jobsById_.put(job.jobId, &job);
    ++jobCount_;
    return job;
}

} // namespace b6
