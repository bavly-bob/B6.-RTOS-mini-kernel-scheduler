#ifndef B6_KERNEL_RESOURCE_MANAGER_H
#define B6_KERNEL_RESOURCE_MANAGER_H

#include "registry.h"
#include <stdexcept>

namespace b6 {

enum class PriorityChangeReason { Inheritance, MutexRelease };

struct ResourcePriorityChange {
    JobId jobId;
    ResourceId mutexId;
    int from;
    int to;
    PriorityChangeReason reason;
};

// Notifications update scheduler memberships, never dispatch recursively.
class ResourceSchedulerHooks {
public:
    virtual ~ResourceSchedulerHooks() = default;
    virtual void onBlocked(Job& job) = 0;
    virtual void onReady(Job& job) = 0;
    // effectivePriority is already updated. Move existing ready membership from
    // change.from to change.to, preserving state/quantum; no node means no move.
    virtual void onPriorityChanged(Job&, const ResourcePriorityChange&) {
        // Older PI-off adapters remain compatible; never ignore a required requeue.
        throw std::logic_error("scheduler adapter must implement onPriorityChanged for inheritance");
    }
};

enum class AcquireResult { Acquired, Blocked };

class ResourceManager {
public:
    // Registry and hooks must outlive this manager. Fixed priority, PI off or on.
    ResourceManager(Registry& registry, ResourceSchedulerHooks& hooks);
    ~ResourceManager();
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) = delete;
    ResourceManager& operator=(ResourceManager&&) = delete;

    AcquireResult lock(JobId caller, ResourceId mutexId);
    std::optional<JobId> unlock(JobId caller, ResourceId mutexId);
    AcquireResult wait(JobId caller, ResourceId semaphoreId);
    std::optional<JobId> signal(JobId caller, ResourceId semaphoreId);

    // Interpreter/control guards only: the scheduler performs the transition.
    void validateMaySleep(JobId jobId) const;
    void validateMaySuspend(JobId jobId) const;
    void validateMayComplete(JobId jobId) const;

    // After the scheduler resumes a mutex waiter, retry an unowned mutex.
    std::optional<JobId> resumeMutexWait(JobId jobId);
    const MutexRecord* findMutex(ResourceId id) const;
    const SemaphoreRecord* findSemaphore(ResourceId id) const;

private:
    Registry& registry_;
    ResourceSchedulerHooks& hooks_;
    MutexRecord mutexes_[kMaxResources];
    SemaphoreRecord semaphores_[kMaxResources];
    Sequence nextWaitSequence_ = 1;
    bool notifying_ = false;
    bool failed_ = false;

    void requireUsable() const;
    void notify(Job& job, bool ready);
    void changePriority(Job& job, int priority, ResourceId mutexId, PriorityChangeReason reason);
    void recomputeOwner(MutexRecord& record);
    const Job& liveJob(JobId id) const;
    Job& runningJob(JobId id);
    MutexRecord& mutex(ResourceId id);
    SemaphoreRecord& semaphore(ResourceId id);
    void enqueue(CircularList<Job>& queue, Job& job);
    void block(Job& job, BlockReason reason, ResourceId resource);
    void resolveWait(CircularList<Job>& queue, Job& job);
    std::optional<JobId> grantMutex(MutexRecord& record);
};

} // namespace b6
#endif
