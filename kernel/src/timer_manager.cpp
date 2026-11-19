#include "timer_manager.h"

#include <limits>
#include <stdexcept>

namespace b6 {

TimerManager::TimerManager(Registry& registry)
    : TimerManager(registry, registry.scenario().config.timerBackend) {}

TimerManager::TimerManager(Registry& registry, TimerBackend backend)
    : registry_(registry), backend_(backend) {
    active_ = backend == TimerBackend::Heap
                  ? static_cast<TimerQueue*>(&heapBackend_)
                  : static_cast<TimerQueue*>(&sortedBackend_);
    // At most one pending timer per job, so maxJobs bounds the queue.
    active_->reserve(registry.scenario().config.limits.maxJobs);
}

void TimerManager::observeTime(Tick now) {
    if (now < 0) throw std::invalid_argument("timer tick must be nonnegative");
    if (started_ && now < lastNow_) throw std::logic_error("simulated time moved backwards");
    lastNow_ = now;
    started_ = true;
}

TimerId TimerManager::sleep(JobId jobId, Tick now, Tick duration) {
    Job* job = registry_.findJob(jobId);
    if (!job) throw std::invalid_argument("SLEEP for an unknown job");
    if (duration <= 0) throw std::invalid_argument("SLEEP duration must be positive");
    if (now > std::numeric_limits<Tick>::max() - duration)
        throw std::invalid_argument("SLEEP wake tick would overflow");
    if (job->state != JobState::Running || job->suspended)
        throw std::logic_error("only the RUNNING, unsuspended job may SLEEP");
    if (job->blockReason != BlockReason::None || job->waitNode)
        throw std::logic_error("job already has an unresolved wait");
    if (job->heldMutexId) throw std::logic_error("SLEEP while holding a mutex is not supported");
    if (job->timerId) throw std::logic_error("job already has an active timer");
    observeTime(now);

    TimerEntry entry;
    entry.timerId = nextTimerId_;
    entry.jobId = jobId;
    entry.wakeTick = now + duration;
    entry.insertionSequence = nextSequence_;
    entry.active = true;
    active_->insert(entry);  // If this throws, nothing below has changed.
    ++nextTimerId_;
    ++nextSequence_;

    // Wait metadata only. Ready membership is the scheduler's responsibility.
    job->state = JobState::Blocked;
    job->blockReason = BlockReason::Timer;
    job->blockedResourceId.reset();
    job->wakeTick = entry.wakeTick;
    job->timerId = entry.timerId;
    return entry.timerId;
}

std::optional<Tick> TimerManager::nextWake() const {
    TimerEntry head;
    if (!active_->peek(head)) return std::nullopt;
    return head.wakeTick;
}

Job& TimerManager::jobFor(const TimerEntry& entry) {
    Job* job = registry_.findJob(entry.jobId);
    if (!job) throw std::logic_error("timer refers to an unknown job");
    // A timer may only wake the job that is still waiting on that exact timer.
    // This is what guarantees a completed or cancelled job is never woken.
    if (!job->timerId || *job->timerId != entry.timerId || job->blockReason != BlockReason::Timer)
        throw std::logic_error("stale timer: job is not waiting on it");
    if (job->state != JobState::Blocked && job->state != JobState::Suspended)
        throw std::logic_error("sleeping job is neither BLOCKED nor SUSPENDED");
    return *job;
}

std::size_t TimerManager::expireDue(Tick now, DynamicArray& woken) {
    observeTime(now);
    woken.clear();
    std::size_t count = 0;
    TimerEntry head;
    // The queue is ordered, so stop at the first timer that is not yet due.
    while (active_->peek(head) && head.wakeTick <= now) {
        active_->popHead();
        if (!head.active) continue;  // Lazily cancelled entry (not used today).
        Job& job = jobFor(head);

        // Resolve the wait. Same reset as ResourceManager::resolveWait.
        job.blockReason = BlockReason::None;
        job.wakeTick.reset();
        job.timerId.reset();
        job.quantumRemaining = registry_.scenario().config.quantumTicks;
        job.state = job.suspended ? JobState::Suspended : JobState::Ready;

        woken.pushBack(&job);  // No allocation if reserved to maxJobs.
        ++count;
    }
    return count;
}

bool TimerManager::cancel(TimerId timerId) {
    if (!active_->remove(timerId)) return false;
    // Find the job that held this timer. At most maxJobs jobs exist; this scan
    // is part of the documented O(n) cancellation cost.
    for (JobId id = 1; id <= static_cast<JobId>(registry_.jobCount()); ++id) {
        Job* job = registry_.findJob(id);
        if (job && job->timerId && *job->timerId == timerId) {
            job->timerId.reset();
            job->wakeTick.reset();
            break;
        }
    }
    return true;
}

void TimerManager::reset() {
    TimerEntry head;
    while (active_->peek(head)) {
        active_->popHead();
        Job* job = registry_.findJob(head.jobId);
        if (job && job->timerId && *job->timerId == head.timerId) {
            job->timerId.reset();
            job->wakeTick.reset();
        }
    }
    nextTimerId_ = 1;
    nextSequence_ = 1;
    lastNow_ = 0;
    started_ = false;
}

} // namespace b6
