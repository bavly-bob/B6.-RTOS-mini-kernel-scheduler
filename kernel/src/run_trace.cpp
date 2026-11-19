#include "run_trace.h"

#include <stdexcept>

namespace b6 {

// ---- TraceEvent builder ----------------------------------------------------

TraceEvent TraceEvent::at(Tick tick, TickPhase phase, EventType type, const char* reason) {
    TraceEvent event;
    event.tick = tick;
    event.phase = phase;
    event.type = type;
    event.reason = reason ? reason : "";
    return event;
}

TraceEvent& TraceEvent::job(const Job& j) {
    jobId = j.jobId;
    taskId = j.taskId;
    return *this;
}
TraceEvent& TraceEvent::job(JobId id) { jobId = id; return *this; }
TraceEvent& TraceEvent::task(TaskId id) { taskId = id; return *this; }
TraceEvent& TraceEvent::resource(ResourceId id) { resourceId = id; return *this; }

TraceEvent& TraceEvent::with(const char* key, std::int64_t value) {
    if (dataCount == kMaxEventData) throw std::length_error("too many event data fields");
    data[dataCount++] = {key, EventDatum::Kind::Integer, value};
    return *this;
}
TraceEvent& TraceEvent::flag(const char* key, bool value) {
    if (dataCount == kMaxEventData) throw std::length_error("too many event data fields");
    data[dataCount++] = {key, EventDatum::Kind::Boolean, value ? 1 : 0};
    return *this;
}
TraceEvent& TraceEvent::withNull(const char* key) {
    if (dataCount == kMaxEventData) throw std::length_error("too many event data fields");
    data[dataCount++] = {key, EventDatum::Kind::Null, 0};
    return *this;
}

// ---- RunTrace ----------------------------------------------------------------

// Segment bound: each segment covers at least one tick, so `horizon` suffices.
// Snapshot bound: at most one per boundary 0..horizon. Resource rows: every
// mutex and semaphore at every snapshot. The ID pool holds ready orders and
// waiter lists; both are subsets of the snapshot's job rows (ready jobs and
// resource waiters are disjoint), so the row limit also bounds the pool.
RunTrace::RunTrace(const RunConfig& config)
    : horizon_(config.horizonTicks),
      events_(config.limits.maxEvents),
      segments_(static_cast<std::size_t>(config.horizonTicks)),
      snapshots_(static_cast<std::size_t>(config.horizonTicks) + 1),
      jobRows_(config.limits.maxSnapshotJobRows),
      resourceRows_((static_cast<std::size_t>(config.horizonTicks) + 1) *
                    (config.limits.maxMutexes + config.limits.maxSemaphores)),
      idPool_(config.limits.maxSnapshotJobRows),
      warnings_(kMaxWarnings) {}

bool RunTrace::addEvent(TraceEvent event) {
    if (eventsTruncated_) return false;
    if (event.tick < 0) throw std::logic_error("event tick must be nonnegative");
    if (!events_.isEmpty()) {
        const TraceEvent& last = events_.back();
        if (event.tick < last.tick || (event.tick == last.tick && event.phase < last.phase))
            throw std::logic_error("events must be added in (tick, phase) order");
    }
    if (events_.isFull()) {
        eventsTruncated_ = true;
        markInvalid("event limit (maxEvents) reached; trace truncated");
        return false;
    }
    event.seq = nextSeq_++;
    events_.push(event);
    return true;
}

void RunTrace::recordSegment(Tick tick, std::optional<JobId> job, std::optional<int> base,
                             std::optional<int> effective) {
    if (tick < 0 || tick >= horizon_) throw std::logic_error("CPU interval outside [0, horizon)");
    if (!segments_.isEmpty()) {
        TraceSegment& last = segments_.back();
        if (tick < last.end) throw std::logic_error("CPU intervals must not overlap or go back");
        if (tick == last.end && last.jobId == job && last.basePriority == base &&
            last.effectivePriority == effective) {
            last.end = tick + 1;  // Same job, same priorities: extend.
            return;
        }
    }
    TraceSegment segment;
    segment.start = tick;
    segment.end = tick + 1;
    segment.jobId = job;
    segment.basePriority = base;
    segment.effectivePriority = effective;
    // Cannot fail: at most `horizon` disjoint one-tick-or-longer segments exist.
    if (!segments_.push(segment)) throw std::logic_error("segment storage exhausted");
}

void RunTrace::recordInterval(Tick tick, const Job* job, int basePriority) {
    if (!job) {
        recordIdle(tick);
        return;
    }
    recordSegment(tick, job->jobId, basePriority, job->effectivePriority);
}

void RunTrace::recordIdle(Tick tick) {
    recordSegment(tick, std::nullopt, std::nullopt, std::nullopt);
}

// Waiter queues are CircularLists in priority/FIFO order. The list exposes its
// head value through front(); that job's saved waitNode is the head node, so
// following getNext() size() times visits the queue in order.
bool RunTrace::pushWaiters(BoundedArray<JobId>& pool, const CircularList<Job>& waiters) {
    const Job* head = waiters.front();
    if (!head) return true;
    const CircularList<Job>::Node* node = head->waitNode;
    if (!node) throw std::logic_error("waiter without a wait node");
    for (std::size_t i = 0; i < waiters.size(); ++i) {
        if (!pool.push(node->getValue()->jobId)) return false;
        node = node->getNext();
    }
    return true;
}

bool RunTrace::captureSnapshot(Tick tick, std::optional<JobId> runningJobId,
                               const JobId* readyOrder, std::size_t readyCount,
                               const Registry& registry, const ResourceManager* resources) {
    if (snapshotsTruncated_) return false;
    if (tick < 0 || tick > horizon_) throw std::logic_error("snapshot outside [0, horizon]");
    if (!snapshots_.isEmpty() && tick <= snapshots_.back().tick)
        throw std::logic_error("snapshot ticks must strictly increase");
    if (readyCount && !readyOrder) throw std::invalid_argument("missing ready order");

    // If a pool fills part-way, the half-built snapshot is never pushed, so
    // its partial rows are unreachable: the exporter reads only the ranges of
    // published snapshots. Recording stops and the run is marked truncated.
    auto truncate = [&]() {
        snapshotsTruncated_ = true;
        markInvalid("snapshot row limit (maxSnapshotJobRows) reached; trace truncated");
        return false;
    };

    TraceSnapshot snap;
    snap.tick = tick;
    snap.runningJobId = runningJobId;

    snap.firstReady = idPool_.size();
    for (std::size_t i = 0; i < readyCount; ++i)
        if (!idPool_.push(readyOrder[i])) return truncate();
    snap.readyCount = readyCount;

    snap.firstRow = jobRows_.size();
    const std::size_t jobs = registry.jobCount();
    for (JobId id = 1; id <= static_cast<JobId>(jobs); ++id) {
        const Job* job = registry.findJob(id);
        if (!job) throw std::logic_error("registry job IDs are not contiguous");
        const TaskSpec* task = registry.findTask(job->taskId);
        SnapshotJobRow row;
        row.jobId = job->jobId;
        row.taskId = job->taskId;
        row.state = job->state;
        row.suspended = job->suspended;
        row.basePriority = task ? task->basePriority : job->effectivePriority;
        row.effectivePriority = job->effectivePriority;
        row.cpuBurstRemaining = job->cpuBurstRemaining;
        row.totalCpuUsed = job->totalCpuUsed;
        row.scriptIndex = job->scriptIndex;
        row.blockReason = job->blockReason;
        row.blockedResourceId = job->blockedResourceId;
        row.heldMutexId = job->heldMutexId;
        row.wakeTick = job->wakeTick;
        row.deadlineMissed = job->deadlineMissRecorded;
        if (!jobRows_.push(row)) return truncate();
    }
    snap.rowCount = jobs;

    snap.firstResource = resourceRows_.size();
    if (resources) {
        const Scenario& s = registry.scenario();
        for (std::size_t i = 0; i < s.mutexCount; ++i) {
            const MutexRecord* m = resources->findMutex(s.mutexes[i].resourceId);
            if (!m) continue;
            SnapshotResourceRow row;
            row.resourceId = m->resourceId;
            row.isMutex = true;
            row.ownerJobId = m->ownerJobId;
            row.firstWaiter = idPool_.size();
            if (!pushWaiters(idPool_, m->waiters)) return truncate();
            row.waiterCount = m->waiters.size();
            if (!resourceRows_.push(row)) return truncate();
            ++snap.resourceCount;
        }
        for (std::size_t i = 0; i < s.semaphoreCount; ++i) {
            const SemaphoreRecord* sem = resources->findSemaphore(s.semaphores[i].resourceId);
            if (!sem) continue;
            SnapshotResourceRow row;
            row.resourceId = sem->resourceId;
            row.isMutex = false;
            row.count = sem->count;
            row.maxCount = sem->maxCount;
            row.firstWaiter = idPool_.size();
            if (!pushWaiters(idPool_, sem->waiters)) return truncate();
            row.waiterCount = sem->waiters.size();
            if (!resourceRows_.push(row)) return truncate();
            ++snap.resourceCount;
        }
    }
    if (!snapshots_.push(snap)) return truncate();
    return true;
}

void RunTrace::addWarning(const std::string& text) {
    warnings_.push(text);  // Extra warnings beyond kMaxWarnings are dropped.
}

void RunTrace::markInvalid(const std::string& error) {
    if (error_.empty()) error_ = error.empty() ? "invalid run" : error;
}

// ---- Schema spellings (docs/run-schema-v1.md) ------------------------------

const char* toString(EventType type) {
    switch (type) {
    case EventType::Release: return "RELEASE";
    case EventType::Dispatch: return "DISPATCH";
    case EventType::Preempt: return "PREEMPT";
    case EventType::Block: return "BLOCK";
    case EventType::Wake: return "WAKE";
    case EventType::Suspend: return "SUSPEND";
    case EventType::Resume: return "RESUME";
    case EventType::Complete: return "COMPLETE";
    case EventType::Lock: return "LOCK";
    case EventType::Unlock: return "UNLOCK";
    case EventType::Wait: return "WAIT";
    case EventType::Signal: return "SIGNAL";
    case EventType::PriorityChange: return "PRIORITY_CHANGE";
    case EventType::DeadlineMiss: return "DEADLINE_MISS";
    case EventType::Error: return "ERROR";
    }
    return "ERROR";
}

const char* toString(JobState state) {
    switch (state) {
    case JobState::Ready: return "READY";
    case JobState::Running: return "RUNNING";
    case JobState::Blocked: return "BLOCKED";
    case JobState::Suspended: return "SUSPENDED";
    case JobState::Completed: return "COMPLETED";
    }
    return "READY";
}

const char* toString(BlockReason reason) {
    switch (reason) {
    case BlockReason::None: return "none";
    case BlockReason::Timer: return "timer";
    case BlockReason::Mutex: return "mutex";
    case BlockReason::Semaphore: return "semaphore";
    }
    return "none";
}

} // namespace b6
