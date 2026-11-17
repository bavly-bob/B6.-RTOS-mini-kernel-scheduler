#include "resource_manager.h"
#include <limits>
#include <stdexcept>

namespace b6 {

ResourceManager::ResourceManager(Registry& registry, ResourceSchedulerHooks& hooks)
    : registry_(registry), hooks_(hooks) {
    const Scenario& s = registry_.scenario();
    if (s.config.policy != Policy::FixedPriority || s.config.inheritance)
        throw std::invalid_argument("resource manager currently requires fixed priority with inheritance off");
    for (std::size_t i = 0; i < s.mutexCount; ++i)
        mutexes_[i].resourceId = s.mutexes[i].resourceId;
    for (std::size_t i = 0; i < s.semaphoreCount; ++i) {
        semaphores_[i].resourceId = s.semaphores[i].resourceId;
        semaphores_[i].count = s.semaphores[i].initialCount;
        semaphores_[i].maxCount = s.semaphores[i].maxCount;
    }
}

ResourceManager::~ResourceManager() {
    // Run teardown only: clear borrowed handles before list nodes disappear.
    for (std::size_t i = 0; i < registry_.scenario().mutexCount; ++i) {
        auto& record = mutexes_[i];
        while (Job* job = record.waiters.popFront()) job->waitNode = nullptr;
        if (record.ownerJobId) {
            if (Job* owner = registry_.findJob(*record.ownerJobId)) owner->heldMutexId.reset();
        }
    }
    for (std::size_t i = 0; i < registry_.scenario().semaphoreCount; ++i)
        while (Job* job = semaphores_[i].waiters.popFront()) job->waitNode = nullptr;
}

const MutexRecord* ResourceManager::findMutex(ResourceId id) const {
    for (std::size_t i = 0; i < registry_.scenario().mutexCount; ++i)
        if (mutexes_[i].resourceId == id) return &mutexes_[i];
    return nullptr;
}

const SemaphoreRecord* ResourceManager::findSemaphore(ResourceId id) const {
    for (std::size_t i = 0; i < registry_.scenario().semaphoreCount; ++i)
        if (semaphores_[i].resourceId == id) return &semaphores_[i];
    return nullptr;
}

MutexRecord& ResourceManager::mutex(ResourceId id) {
    for (std::size_t i = 0; i < registry_.scenario().mutexCount; ++i)
        if (mutexes_[i].resourceId == id) return mutexes_[i];
    throw std::invalid_argument("unknown mutex ID");
}

SemaphoreRecord& ResourceManager::semaphore(ResourceId id) {
    for (std::size_t i = 0; i < registry_.scenario().semaphoreCount; ++i)
        if (semaphores_[i].resourceId == id) return semaphores_[i];
    throw std::invalid_argument("unknown semaphore ID");
}

Job& ResourceManager::runningJob(JobId id) {
    requireUsable();
    Job* job = registry_.findJob(id);
    if (!job) throw std::invalid_argument("unknown caller job ID");
    if (job->state != JobState::Running || job->suspended ||
        job->blockReason != BlockReason::None || job->blockedResourceId || job->waitNode)
        throw std::logic_error("resource operation requires a running job without wait membership");
    if (job->effectivePriority < 0 || job->effectivePriority > 31)
        throw std::logic_error("effective priority must be in 0..31");
    return *job;
}

void ResourceManager::requireUsable() const {
    if (failed_) throw std::logic_error("scheduler hook failed; abort this run");
    if (notifying_) throw std::logic_error("scheduler hooks must not reenter resource operations");
}

void ResourceManager::notify(Job& job, bool ready) {
    notifying_ = true;
    try {
        if (ready) hooks_.onReady(job);
        else hooks_.onBlocked(job);
        notifying_ = false;
    } catch (...) {
        // Resource state is committed; a failed scheduler update invalidates the run.
        notifying_ = false;
        failed_ = true;
        throw;
    }
}

void ResourceManager::enqueue(CircularList<Job>& queue, Job& job) {
    if (nextWaitSequence_ == std::numeric_limits<Sequence>::max())
        throw std::length_error("resource wait sequence exhausted");
    Job* originalHead = queue.front();
    const std::size_t count = queue.size();
    // Rotate to the insertion point; pushBack then inserts before that point.
    // Equal priorities are passed over, keeping FIFO order and existing handles.
    for (std::size_t i = 0; i < count; ++i) {
        if (queue.front()->effectivePriority < job.effectivePriority) break;
        queue.rotate();
    }
    try {
        job.waitNode = queue.pushBack(&job);
    } catch (...) {
        while (queue.front() != originalHead) queue.rotate();
        throw;
    }
    Job* head = !originalHead || job.effectivePriority > originalHead->effectivePriority
        ? &job : originalHead;
    while (queue.front() != head) queue.rotate();
    job.waitSequence = nextWaitSequence_++;
}

void ResourceManager::block(Job& job, BlockReason reason, ResourceId resource) {
    job.blockReason = reason;
    job.blockedResourceId = resource;
    job.state = JobState::Blocked;
    notify(job, false);
}

void ResourceManager::resolveWait(CircularList<Job>& queue, Job& job) {
    queue.remove(job.waitNode);
    job.waitNode = nullptr;
    job.blockReason = BlockReason::None;
    job.blockedResourceId.reset();
    job.quantumRemaining = registry_.scenario().config.quantumTicks;
    job.state = job.suspended ? JobState::Suspended : JobState::Ready;
    if (!job.suspended) notify(job, true);
}

AcquireResult ResourceManager::lock(JobId caller, ResourceId mutexId) {
    Job& job = runningJob(caller);
    MutexRecord& record = mutex(mutexId);
    if (job.heldMutexId) throw std::logic_error("recursive and nested mutex locking are unsupported");
    if (!record.ownerJobId) {
        record.ownerJobId = job.jobId;
        job.heldMutexId = mutexId;
        return AcquireResult::Acquired;
    }
    enqueue(record.waiters, job);
    block(job, BlockReason::Mutex, mutexId);
    return AcquireResult::Blocked;
}

std::optional<JobId> ResourceManager::grantMutex(MutexRecord& record) {
    // Suspended jobs cannot hold a mutex. Keep their waits until resume.
    Job* first = record.waiters.front();
    if (!first) return std::nullopt;
    auto* node = first->waitNode;
    for (std::size_t i = 0; i < record.waiters.size(); ++i, node = node->getNext()) {
        Job& next = *node->getValue();
        if (next.suspended) continue;
        record.ownerJobId = next.jobId;
        next.heldMutexId = record.resourceId;
        resolveWait(record.waiters, next);
        return next.jobId;
    }
    return std::nullopt;
}

std::optional<JobId> ResourceManager::unlock(JobId caller, ResourceId mutexId) {
    Job& job = runningJob(caller);
    MutexRecord& record = mutex(mutexId);
    if (record.ownerJobId != caller || job.heldMutexId != mutexId)
        throw std::logic_error("only the mutex owner may unlock");
    record.ownerJobId.reset();
    job.heldMutexId.reset();
    return grantMutex(record);
}

AcquireResult ResourceManager::wait(JobId caller, ResourceId semaphoreId) {
    Job& job = runningJob(caller);
    SemaphoreRecord& record = semaphore(semaphoreId);
    if (job.heldMutexId) throw std::logic_error("cannot wait on a semaphore while holding a mutex");
    if (record.count > 0) {
        --record.count;
        return AcquireResult::Acquired;
    }
    enqueue(record.waiters, job);
    block(job, BlockReason::Semaphore, semaphoreId);
    return AcquireResult::Blocked;
}

std::optional<JobId> ResourceManager::signal(JobId caller, ResourceId semaphoreId) {
    runningJob(caller);
    SemaphoreRecord& record = semaphore(semaphoreId);
    if (Job* next = record.waiters.front()) {
        const JobId granted = next->jobId;
        // The permit completes this WAIT directly; no other job can consume it.
        resolveWait(record.waiters, *next);
        return granted;
    }
    if (record.count == record.maxCount) throw std::overflow_error("semaphore count would exceed maximum");
    ++record.count;
    return std::nullopt;
}

std::optional<JobId> ResourceManager::resumeMutexWait(JobId jobId) {
    requireUsable();
    Job* job = registry_.findJob(jobId);
    if (!job) throw std::invalid_argument("unknown resumed job ID");
    if (job->suspended || job->state != JobState::Blocked || job->blockReason != BlockReason::Mutex ||
        !job->blockedResourceId || !job->waitNode || job->heldMutexId)
        throw std::logic_error("resumeMutexWait requires a resumed job still waiting for a mutex");
    MutexRecord& record = mutex(*job->blockedResourceId);
    return record.ownerJobId ? std::nullopt : grantMutex(record);
}

} // namespace b6
