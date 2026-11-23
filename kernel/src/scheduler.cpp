#include "scheduler.h"

#include <stdexcept>
#include <string>

namespace b6 {

// ---- EDF heap position tracking ------------------------------------------

void Scheduler::EdfIndexHook::operator()(const EdfEntry& entry, int index) const {
    Job* job = registry->findJob(entry.jobId());
    if (!job) throw std::logic_error("EDF entry refers to an unknown job");
    if (index < 0) {
        job->edfIndex.reset();
        job->inEdfQueue = false;
    } else {
        job->edfIndex = static_cast<std::size_t>(index);
        job->inEdfQueue = true;
    }
}

// ---- Construction ----------------------------------------------------------

Scheduler::Scheduler(Registry& registry, RunTrace& trace,
                     const ControlAction* controls, std::size_t controlCount)
    : registry_(registry),
      trace_(trace),
      edf_(registry.scenario().config.policy == Policy::EDF),
      edf_heap_(std::less<EdfEntry>(), EdfIndexHook{&registry}),
      woken_(static_cast<int>(registry.scenario().config.limits.maxJobs)) {
    const Scenario& s = registry_.scenario();
    // Reserve once so a release never reallocates the heap mid-run.
    edf_heap_.reserve(static_cast<int>(s.config.limits.maxJobs));

    // Controls are scenario input: validate them like the rest of the input.
    if (controlCount > kMaxControls) throw std::invalid_argument("too many suspend/resume controls");
    if (controlCount && !controls) throw std::invalid_argument("missing control list");
    for (std::size_t i = 0; i < controlCount; ++i) {
        const ControlAction& c = controls[i];
        const std::string at = "controls[" + std::to_string(i) + "]";
        if (c.tick < 0 || c.tick > s.config.horizonTicks)
            throw std::invalid_argument(at + ".tick must be within [0, horizonTicks]");
        if (i > 0 && c.tick < controls[i - 1].tick)
            throw std::invalid_argument(at + " is out of order; controls must be sorted by tick");
        if (!registry_.findTask(c.taskId))
            throw std::invalid_argument(at + ".taskId references an unknown task");
        controls_[i] = c;
    }
    controlCount_ = controlCount;

    // Releases must reach the registry in task-ID order (insertion sort; <= 32).
    for (std::size_t i = 0; i < s.taskCount; ++i) {
        std::size_t j = i;
        while (j > 0 && s.tasks[taskOrder_[j - 1]].taskId > s.tasks[i].taskId) {
            taskOrder_[j] = taskOrder_[j - 1];
            --j;
        }
        taskOrder_[j] = i;
    }
}

Scheduler::~Scheduler() {
    // Clear saved handles before the ring nodes are freed with the rings.
    for (auto& ring : rings_)
        while (Job* job = ring.popFront()) job->readyNode = nullptr;
    edf_heap_.clear();  // The hook clears every Job::edfIndex.
}

void Scheduler::attach(ResourceManager* resources, TimerManager& timers) {
    if (attached_) throw std::logic_error("Scheduler::attach called twice");
    resources_ = resources;
    timers_ = &timers;
    attached_ = true;
}

// ---- The clock ---------------------------------------------------------------

void Scheduler::run() {
    if (!attached_) throw std::logic_error("Scheduler::attach must be called before run");
    if (ran_) throw std::logic_error("a Scheduler runs once; build a new one for a new run");
    ran_ = true;
    try {
        for (Tick t = 0; t <= registry_.scenario().config.horizonTicks; ++t) boundary(t);
    } catch (const std::exception& error) {
        // Record where the run stopped, then let the application report it.
        try {
            emit(event(EventType::Error, "engine_error"));
        } catch (...) {
        }
        trace_.markInvalid(error.what());
        throw;
    }
}

void Scheduler::boundary(Tick t) {
    now_ = t;
    const bool atHorizon = t == registry_.scenario().config.horizonTicks;
    settle();
    applyInputs();
    // The final boundary settles and records only; no new CPU decision.
    if (!atHorizon) {
        stabilize();
        checkInvariants();
        if (running_ != mostUrgent())
            throw std::logic_error("invariant: the running job is not the most urgent");
    }
    recordDeadlines();
    recordSnapshot();
    execute();
}

// Phase 1. The job that ran [t-1, t) has already been charged in execute().
// If its burst ended, it continues straight into the steps that cannot block
// it (UNLOCK, SIGNAL, END, loading the next RUN), so a release or completion
// takes effect at this boundary. Blocking steps wait for phase 3, after this
// tick's releases are visible.
void Scheduler::settle() {
    phase_ = TickPhase::Settle;
    Job* job = running_;
    if (!job) return;
    if (job->cpuBurstRemaining == 0) runZeroTimeSteps(*job, StepMode::SettleOnly);
    // Completion/blocking came first; rotate only a job that still needs CPU.
    if (job->state == JobState::Running && job->cpuBurstRemaining > 0) rotateIfSliceUsed(*job);
}

// Phase 2: releases, then timers, then administrative controls.
void Scheduler::applyInputs() {
    phase_ = TickPhase::Inputs;
    releaseDueJobs();
    wakeTimers();
    applyControls();
}

void Scheduler::releaseDueJobs() {
    const Scenario& s = registry_.scenario();
    if (now_ >= s.config.horizonTicks) return;  // No release at the horizon.
    for (std::size_t i = 0; i < s.taskCount; ++i) {
        const TaskSpec& task = s.tasks[taskOrder_[i]];
        if (now_ < task.firstRelease) continue;
        const bool due = task.periodTicks == 0
                             ? now_ == task.firstRelease
                             : (now_ - task.firstRelease) % task.periodTicks == 0;
        if (!due) continue;
        // A new job per release, even if the previous one is unfinished.
        Job& job = registry_.releaseJob(task.taskId, now_);
        enqueue(job);
        emit(event(EventType::Release, "scheduled").job(job)
                 .with("absoluteDeadline", job.absoluteDeadline));
    }
}

void Scheduler::wakeTimers() {
    timers_->expireDue(now_, woken_);
    for (int i = 0; i < woken_.size(); ++i) {
        Job& job = *static_cast<Job*>(woken_.at(i));
        emit(event(EventType::Wake, "timer").job(job).flag("suspended", job.suspended));
        // A suspended sleeper's wait is resolved, but it stays off the CPU.
        if (job.state == JobState::Ready) enqueue(job);
    }
}

void Scheduler::applyControls() {
    while (nextControl_ < controlCount_ && controls_[nextControl_].tick == now_) {
        const ControlAction& c = controls_[nextControl_++];
        Job* job = latestUnfinishedJob(c.taskId);
        if (!job)
            throw std::invalid_argument("control at tick " + std::to_string(now_) +
                                        ": task has no released, unfinished job");
        if (c.kind == ControlAction::Kind::Suspend) suspend(*job);
        else resume(*job);
    }
}

// Phase 3. Pick the most urgent job and let it run its zero-time steps. If
// they block or finish it, pick again. Stop at the first job that needs the
// CPU, or when nothing is runnable (idle).
void Scheduler::stabilize() {
    phase_ = TickPhase::Stabilize;
    const Scenario& s = registry_.scenario();
    // Each pass either consumes a script step, removes a job, or rotates a
    // ring once (after which the slice is fresh), so this bound is never hit
    // by a correct engine.
    const std::size_t bound = 4 * (s.config.limits.maxJobs * s.config.limits.maxScriptLength + 1);
    for (std::size_t pass = 0;; ++pass) {
        if (pass > bound) throw std::logic_error("boundary did not stabilize");
        Job* candidate = mostUrgent();
        if (!candidate) break;  // Idle.
        dispatch(*candidate);
        if (!runZeroTimeSteps(*candidate, StepMode::Full)) continue;  // Blocked/finished.
        if (rotateIfSliceUsed(*candidate)) continue;                  // Peer's turn.
        break;
    }
    rotated_ = nullptr;
}

// Phase 4. Completion exactly at the deadline is on time, because completions
// happen in phases 1 and 3, before this check.
void Scheduler::recordDeadlines() {
    phase_ = TickPhase::Deadlines;
    for (JobId id = 1; id <= static_cast<JobId>(registry_.jobCount()); ++id) {
        Job& job = *registry_.findJob(id);
        if (job.state == JobState::Completed || job.deadlineMissRecorded) continue;
        if (job.absoluteDeadline > now_) continue;
        job.deadlineMissRecorded = true;  // Once; a later completion keeps it.
        job.deadlineMissTick = now_;
        emit(event(EventType::DeadlineMiss, "unfinished_at_deadline").job(job)
                 .with("absoluteDeadline", job.absoluteDeadline));
    }
}

// Phase 5. The ready order lists READY jobs (not the running one), most
// urgent first: ring order from priority 31 down, or EDF key order.
void Scheduler::recordSnapshot() {
    phase_ = TickPhase::Snapshot;
    std::size_t count = 0;
    if (edf_) {
        const int n = edf_heap_.size();
        for (int i = 0; i < n; ++i) {  // Insertion sort of a copy; the heap is untouched.
            const EdfEntry entry = edf_heap_.at(i);
            int j = i;
            while (j > 0 && entry < edfBuffer_[j - 1]) {
                edfBuffer_[j] = edfBuffer_[j - 1];
                --j;
            }
            edfBuffer_[j] = entry;
        }
        for (int i = 0; i < n; ++i)
            if (!running_ || edfBuffer_[i].jobId() != running_->jobId)
                readyBuffer_[count++] = edfBuffer_[i].jobId();
    } else {
        for (int p = PriorityBitmap::kPriorityLevels - 1; p >= 0; --p) {
            const CircularList<Job>& ring = rings_[p];
            if (ring.isEmpty()) continue;
            const CircularList<Job>::Node* node = ring.front()->readyNode;
            for (std::size_t i = 0; i < ring.size(); ++i, node = node->getNext())
                if (node->getValue() != running_) readyBuffer_[count++] = node->getValue()->jobId;
        }
    }
    const std::optional<JobId> runningId =
        running_ ? std::optional<JobId>(running_->jobId) : std::nullopt;
    trace_.captureSnapshot(now_, runningId, readyBuffer_, count, registry_, resources_);
}

// Phase 6. One tick of CPU for the chosen job, or an idle tick.
void Scheduler::execute() {
    phase_ = TickPhase::Execute;
    if (now_ >= registry_.scenario().config.horizonTicks) return;
    Job* job = running_;
    if (!job) {
        trace_.recordIdle(now_);
        return;
    }
    if (job->cpuBurstRemaining <= 0) throw std::logic_error("running job has no CPU work");
    if (!job->firstStartTick) job->firstStartTick = now_;
    --job->cpuBurstRemaining;
    ++job->totalCpuUsed;
    if (!edf_) --job->quantumRemaining;
    trace_.recordInterval(now_, job, registry_.findTask(job->taskId)->basePriority);
}

// ---- Ready structure ---------------------------------------------------------

void Scheduler::enqueue(Job& job) {
    if (isQueued(job)) throw std::logic_error("job is already in the ready structure");
    if (job.state != JobState::Ready || job.suspended || job.blockReason != BlockReason::None)
        throw std::logic_error("only an unsuspended READY job without a wait can be enqueued");
    job.readySequence = nextReadySequence_++;
    if (edf_) {
        edf_heap_.insert(EdfEntry(job.absoluteDeadline, job.releaseSequence, job.jobId));
        return;
    }
    const int p = job.effectivePriority;
    if (p < 0 || p >= PriorityBitmap::kPriorityLevels)
        throw std::logic_error("effective priority must be in 0..31");
    job.readyNode = rings_[p].pushBack(&job);  // Back of the ring: FIFO among equals.
    bitmap_.set(p);
}

void Scheduler::dequeue(Job& job) {
    if (edf_) {
        if (!job.edfIndex) throw std::logic_error("job is not in the EDF heap");
        edf_heap_.removeAt(static_cast<int>(*job.edfIndex));  // Hook clears the index.
    } else {
        const int p = job.effectivePriority;
        if (!job.readyNode || !rings_[p].remove(job.readyNode))
            throw std::logic_error("job is not in its priority ring");
        job.readyNode = nullptr;  // The handle is invalid after removal.
        if (rings_[p].isEmpty()) bitmap_.clear(p);
    }
    if (running_ == &job) running_ = nullptr;
}

bool Scheduler::isQueued(const Job& job) const {
    return edf_ ? job.inEdfQueue : job.readyNode != nullptr;
}

Job* Scheduler::mostUrgent() const {
    if (edf_) return edf_heap_.isEmpty() ? nullptr : registry_.findJob(edf_heap_.peek().jobId());
    if (bitmap_.isEmpty()) return nullptr;
    return rings_[bitmap_.highestSetBit()].front();  // <= 32 bit checks, then O(1).
}

// Round-robin. `job` is at the front of its ring (it was selected there). If
// its slice is used up, it gets a fresh one and the ring rotates, so the next
// equal-priority job is at the front. Returns true if someone else is now
// first. A job alone in its ring just gets the fresh slice.
bool Scheduler::rotateIfSliceUsed(Job& job) {
    if (edf_ || job.quantumRemaining > 0) return false;
    CircularList<Job>& ring = rings_[job.effectivePriority];
    if (ring.front() != &job) throw std::logic_error("rotating job is not at the front of its ring");
    job.quantumRemaining = registry_.scenario().config.quantumTicks;
    ring.rotate();  // O(1): A,B,C becomes B,C,A; handles stay valid.
    rotated_ = &job;
    return ring.front() != &job;
}

// ---- Script interpreter --------------------------------------------------------

// Executes zero-time steps of the RUNNING `job` until it needs the CPU.
// Returns true if it is still RUNNING with CPU work left. In SettleOnly mode
// it stops before a step that could block (LOCK, WAIT, SLEEP).
// Each successful step advances scriptIndex exactly once, including a LOCK or
// WAIT that blocks: the later grant completes that step.
bool Scheduler::runZeroTimeSteps(Job& job, StepMode mode) {
    const TaskSpec& task = *registry_.findTask(job.taskId);
    while (job.state == JobState::Running && job.cpuBurstRemaining == 0) {
        if (job.scriptIndex >= task.scriptLength) throw std::logic_error("script ran past END");
        const Instruction& step = task.script[job.scriptIndex];
        const bool canBlock = step.op == OpCode::Lock || step.op == OpCode::Wait ||
                              step.op == OpCode::Sleep;
        if (mode == StepMode::SettleOnly && canBlock) return false;
        const bool usesResource = step.op == OpCode::Lock || step.op == OpCode::Unlock ||
                                  step.op == OpCode::Wait || step.op == OpCode::Signal;
        if (usesResource && !resources_)
            throw std::logic_error("resource step without a resource manager");

        switch (step.op) {
        case OpCode::Run:
            job.cpuBurstRemaining = step.durationTicks;
            ++job.scriptIndex;
            break;
        case OpCode::End:
            complete(job);
            return false;
        case OpCode::Sleep: {
            const TimerId timer = timers_->sleep(job.jobId, now_, step.durationTicks);
            ++job.scriptIndex;
            dequeue(job);  // The timer manager set BLOCKED; membership is ours.
            emit(event(EventType::Block, "timer").job(job)
                     .with("wakeTick", *job.wakeTick).with("timerId", timer));
            return false;
        }
        case OpCode::Lock: {
            const ResourceId id = *step.resourceId;
            const AcquireResult result = resources_->lock(job.jobId, id);
            ++job.scriptIndex;
            if (result == AcquireResult::Acquired) {
                emit(event(EventType::Lock, "acquired").job(job).resource(id));
                flushPriorityEvents();  // A new owner may inherit from waiters.
                break;
            }
            // onBlocked has already removed it from the ready ring.
            TraceEvent blocked = event(EventType::Block, "mutex_owned").job(job).resource(id);
            const MutexRecord* m = resources_->findMutex(id);
            if (m && m->ownerJobId) blocked.with("ownerJobId", *m->ownerJobId);
            emit(blocked);
            flushPriorityEvents();  // The owner inherits from this waiter.
            return false;
        }
        case OpCode::Unlock: {
            const ResourceId id = *step.resourceId;
            const std::optional<JobId> granted = resources_->unlock(job.jobId, id);
            ++job.scriptIndex;
            emit(event(EventType::Unlock, "released").job(job).resource(id));
            flushPriorityEvents();  // Owner restored; the new owner may inherit.
            noteGranted(granted, id, "mutex_granted");
            break;
        }
        case OpCode::Wait: {
            const ResourceId id = *step.resourceId;
            const AcquireResult result = resources_->wait(job.jobId, id);
            ++job.scriptIndex;
            if (result == AcquireResult::Acquired) {
                emit(event(EventType::Wait, "permit_taken").job(job).resource(id));
                break;
            }
            emit(event(EventType::Block, "semaphore_empty").job(job).resource(id));
            return false;
        }
        case OpCode::Signal: {
            const ResourceId id = *step.resourceId;
            const std::optional<JobId> granted = resources_->signal(job.jobId, id);
            ++job.scriptIndex;
            TraceEvent signalled = event(EventType::Signal, "signalled").job(job).resource(id);
            if (granted) signalled.with("grantedJobId", *granted);
            else signalled.withNull("grantedJobId");
            emit(signalled);
            noteGranted(granted, id, "semaphore_granted");
            break;
        }
        }
    }
    return job.state == JobState::Running && job.cpuBurstRemaining > 0;
}

void Scheduler::complete(Job& job) {
    dequeue(job);
    job.state = JobState::Completed;
    job.completionTick = now_;
    job.cpuBurstRemaining = 0;
    emit(event(EventType::Complete, "end").job(job).flag("late", job.deadlineMissRecorded));
}

// Gives the CPU decision to `job`. The previous RUNNING job, if it is still
// runnable, goes back to READY and keeps its place (and slice) in its ring.
void Scheduler::dispatch(Job& job) {
    if (running_ == &job) return;
    if (running_) {
        Job& previous = *running_;
        previous.state = JobState::Ready;
        const char* reason = "higher_priority";
        if (edf_) reason = "earlier_deadline";
        else if (rotated_ == &previous && job.effectivePriority == previous.effectivePriority)
            reason = "quantum_expired";
        emit(event(EventType::Preempt, reason).job(previous).with("byJobId", job.jobId));
    }
    job.state = JobState::Running;
    running_ = &job;
    emit(event(EventType::Dispatch, edf_ ? "earliest_deadline" : "highest_priority").job(job)
             .with("priority", job.effectivePriority));
}

void Scheduler::noteGranted(std::optional<JobId> granted, ResourceId id, const char* reason) {
    if (!granted) return;
    const Job& job = *registry_.findJob(*granted);
    emit(event(EventType::Wake, reason).job(job).resource(id).flag("suspended", job.suspended));
}

// ---- Suspend / resume ------------------------------------------------------------

Job* Scheduler::latestUnfinishedJob(TaskId taskId) {
    for (JobId id = static_cast<JobId>(registry_.jobCount()); id >= 1; --id) {
        Job* job = registry_.findJob(id);
        if (job->taskId == taskId && job->state != JobState::Completed) return job;
    }
    return nullptr;
}

// Suspension removes the right to run but keeps any wait (timer, semaphore,
// mutex) so the reason for waiting is not lost.
void Scheduler::suspend(Job& job) {
    if (job.suspended) throw std::invalid_argument("SUSPEND of a job that is already suspended");
    if (job.heldMutexId) throw std::invalid_argument("SUSPEND while holding a mutex is not supported");
    if (isQueued(job)) dequeue(job);  // READY or RUNNING.
    job.suspended = true;
    job.state = JobState::Suspended;
    emit(event(EventType::Suspend, "control").job(job)
             .flag("waiting", job.blockReason != BlockReason::None));
}

// Resume makes the job READY only if its wait is already resolved; otherwise
// it goes back to BLOCKED and wakes later as usual.
void Scheduler::resume(Job& job) {
    if (!job.suspended) throw std::invalid_argument("RESUME of a job that is not suspended");
    job.suspended = false;
    job.quantumRemaining = registry_.scenario().config.quantumTicks;  // Fresh slice.
    const bool waiting = job.blockReason != BlockReason::None;
    job.state = waiting ? JobState::Blocked : JobState::Ready;
    emit(event(EventType::Resume, "control").job(job).flag("stillWaiting", waiting));
    if (!waiting) {
        enqueue(job);
    } else if (job.blockReason == BlockReason::Mutex) {
        // The mutex may have been released while this waiter was suspended.
        const ResourceId id = *job.blockedResourceId;
        noteGranted(resources_->resumeMutexWait(job.jobId), id, "mutex_granted");
        flushPriorityEvents();  // A granted waiter may inherit from the rest.
    }
}

// ---- Priority moves (requeue hook for inheritance) ----------------------------

void Scheduler::checkPriority(int priority) const {
    if (priority < 0 || priority >= PriorityBitmap::kPriorityLevels)
        throw std::invalid_argument("effective priority must be in 0..31");
    if (edf_) throw std::logic_error("priority changes are not defined under EDF");
}

// Move ring membership and bitmap together, so they never disagree. A READY
// job joins the back of its new ring. The RUNNING job goes to the front: it
// keeps the CPU and its partial slice (no rotation, no quantum reset); phase
// 3 reselects if another job is now more urgent. No node means no move.
void Scheduler::moveRing(Job& job, int from, int to) {
    if (!job.readyNode) return;
    if (!rings_[from].remove(job.readyNode)) throw std::logic_error("job is not in its ring");
    job.readyNode = nullptr;
    if (rings_[from].isEmpty()) bitmap_.clear(from);
    job.readyNode = &job == running_ ? rings_[to].pushFront(&job) : rings_[to].pushBack(&job);
    bitmap_.set(to);
}

void Scheduler::changeEffectivePriority(Job& job, int newPriority, const char* reason) {
    checkPriority(newPriority);
    const int old = job.effectivePriority;
    if (old == newPriority) return;
    job.effectivePriority = newPriority;
    moveRing(job, old, newPriority);
    emit(event(EventType::PriorityChange, reason).job(job).with("from", old).with("to", newPriority));
}

// ---- ResourceSchedulerHooks ------------------------------------------------------

void Scheduler::onBlocked(Job& job) {
    if (job.state != JobState::Blocked) throw std::logic_error("onBlocked for a job that is not BLOCKED");
    dequeue(job);
}

void Scheduler::onReady(Job& job) {
    enqueue(job);  // Checks READY, unsuspended and not already queued.
}

void Scheduler::onPriorityChanged(Job& job, const ResourcePriorityChange& change) {
    checkPriority(change.to);
    if (job.effectivePriority != change.to || change.from == change.to)
        throw std::logic_error("onPriorityChanged: inconsistent priority change");
    moveRing(job, change.from, change.to);
    if (pendingPriorityCount_ == kMaxPendingPriorityEvents)
        throw std::logic_error("too many priority changes in one resource call");
    const char* reason =
        change.reason == PriorityChangeReason::Inheritance ? "inheritance" : "mutex_release";
    pendingPriority_[pendingPriorityCount_++] = event(EventType::PriorityChange, reason)
        .job(job).resource(change.mutexId).with("from", change.from).with("to", change.to);
}

// ---- Invariants ----------------------------------------------------------------

void Scheduler::checkInvariants() const {
    if (pendingPriorityCount_ != 0)
        throw std::logic_error("invariant: a PRIORITY_CHANGE event was not written");
    std::size_t queued = 0;
    if (edf_) {
        for (int i = 0; i < edf_heap_.size(); ++i) {
            const Job* job = registry_.findJob(edf_heap_.at(i).jobId());
            if (!job || !job->inEdfQueue || !job->edfIndex || *job->edfIndex != static_cast<std::size_t>(i))
                throw std::logic_error("invariant: EDF heap index out of step");
            ++queued;
        }
    } else {
        for (int p = 0; p < PriorityBitmap::kPriorityLevels; ++p) {
            const CircularList<Job>& ring = rings_[p];
            if (bitmap_.test(p) == ring.isEmpty())
                throw std::logic_error("invariant: bitmap bit and ring disagree at priority " + std::to_string(p));
            if (ring.isEmpty()) continue;
            const CircularList<Job>::Node* node = ring.front()->readyNode;
            for (std::size_t i = 0; i < ring.size(); ++i, node = node->getNext()) {
                const Job* job = node->getValue();
                if (job->readyNode != node || job->effectivePriority != p)
                    throw std::logic_error("invariant: ring member has a stale handle or priority");
                ++queued;
            }
        }
    }
    std::size_t runnable = 0, runningCount = 0;
    for (JobId id = 1; id <= static_cast<JobId>(registry_.jobCount()); ++id) {
        const Job& job = *registry_.findJob(id);
        const bool shouldBeQueued = job.state == JobState::Ready || job.state == JobState::Running;
        if (shouldBeQueued != isQueued(job))
            throw std::logic_error("invariant: job " + std::to_string(id) + " ready membership is wrong");
        if (shouldBeQueued && (job.suspended || job.blockReason != BlockReason::None))
            throw std::logic_error("invariant: a runnable job is suspended or waiting");
        if (shouldBeQueued) ++runnable;
        if (job.state == JobState::Running) ++runningCount;
    }
    if (runnable != queued) throw std::logic_error("invariant: duplicate or foreign ready membership");
    if (runningCount > 1) throw std::logic_error("invariant: more than one RUNNING job");
    if ((runningCount == 1) != (running_ != nullptr) || (running_ && running_->state != JobState::Running))
        throw std::logic_error("invariant: running pointer and RUNNING state disagree");
}

// ---- Events ----------------------------------------------------------------------

TraceEvent Scheduler::event(EventType type, const char* reason) const {
    return TraceEvent::at(now_, phase_, type, reason);
}

void Scheduler::emit(TraceEvent e) {
    trace_.addEvent(e);  // On the event limit the trace marks itself truncated.
}

void Scheduler::flushPriorityEvents() {
    for (std::size_t i = 0; i < pendingPriorityCount_; ++i) emit(pendingPriority_[i]);
    pendingPriorityCount_ = 0;
}

} // namespace b6
