#ifndef B6_KERNEL_SCHEDULER_H
#define B6_KERNEL_SCHEDULER_H

#include "MinHeap.h"
#include "bitmap.h"
#include "circular_list.h"
#include "resource_manager.h"
#include "run_trace.h"
#include "timer_manager.h"

namespace b6 {

// ---------------------------------------------------------------------------
// Scheduler: the simulated clock, the ready structures and dispatch.
// Owner: Youssef Gohar (plan pp. 10-11, 20).
//
// One simulated CPU, integer ticks, zero context-switch cost. Every boundary t
// runs the six phases from docs/shared-contracts.md in the same order, so the
// same scenario always produces the same trace:
//
//   1 Settle     account [t-1, t); the job that just ran finishes its burst
//                and runs its non-blocking zero-time steps (UNLOCK, SIGNAL,
//                END, loading the next RUN); an expired slice rotates.
//   2 Inputs     releases (task-ID order), then due timers, then suspend/resume.
//   3 Stabilize  pick the most urgent job; run its zero-time steps (LOCK, WAIT,
//                SLEEP, ...) until it needs the CPU or nothing can run.
//   4 Deadlines  flag each unfinished job with deadline <= t, exactly once.
//   5 Snapshot   record the stable state for the dashboard.
//   6 Execute    if t < horizon, the chosen job runs [t, t+1); otherwise idle.
//
// Ready structures
// - Fixed priority / RM: 32 CircularList rings (one per priority level) plus a
//   PriorityBitmap. Bit p is set iff ring p is nonempty, so the most urgent
//   level is found by checking at most 32 bits. The running job STAYS in its
//   ring (at the front); the other members are READY. Equal priorities share
//   the CPU round-robin with a slice of config.quantumTicks.
// - EDF: a MinHeap<EdfEntry> ordered by (absoluteDeadline, releaseSequence,
//   jobId). The running job stays in the heap until it completes.
//
// Round-robin rules (plan p.11)
// - A slice that reaches zero rotates the ring and gives the job a fresh slice.
// - Arrivals join the back of their ring, so they never cut a slice short.
// - A job preempted by a higher priority keeps the rest of its slice.
// - Completing or blocking comes before rotation.
// - Waking, being granted a resource, or resuming gives a fresh slice.
//
// Collaborators (the scheduler calls them; they never edit ready structures):
// - ResourceManager calls onBlocked/onReady (ResourceSchedulerHooks).
// - TimerManager handles SLEEP wake-ups; the scheduler enqueues READY wakers.
// - RunTrace receives events, CPU intervals and snapshots.
//
// Lifetime: Registry and RunTrace must outlive the scheduler. attach() must be
// called once, before run(), because the ResourceManager needs the scheduler
// (as its hooks) at construction time.
//
// Errors: invalid scenario input throws std::invalid_argument; a broken
// invariant throws std::logic_error. After any exception the run is invalid.
// ---------------------------------------------------------------------------

// A timed administrative command from the scenario (not part of a task script).
// It targets the most recently released, unfinished job of `taskId`.
struct ControlAction {
    enum class Kind { Suspend, Resume };
    Tick tick = 0;
    TaskId taskId = 0;
    Kind kind = Kind::Suspend;
};

constexpr std::size_t kMaxControls = 32;

class Scheduler : public ResourceSchedulerHooks {
public:
    // `controls` must be sorted by tick (equal ticks keep input order).
    Scheduler(Registry& registry, RunTrace& trace,
              const ControlAction* controls = nullptr, std::size_t controlCount = 0);
    ~Scheduler() override;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    // Connects the collaborators. `resources` may be null when the scenario
    // has no mutexes/semaphores (RM/EDF runs, which the validator keeps
    // resource-free). Must be called exactly once before run().
    void attach(ResourceManager* resources, TimerManager& timers);

    // Simulates boundaries 0..horizon. Call once.
    void run();

    // ResourceSchedulerHooks: membership updates only, never a dispatch.
    void onBlocked(Job& job) override;
    void onReady(Job& job) override;
    // Priority inheritance (Moataz's ResourceManager): effectivePriority is
    // already change.to. Moves ready membership from change.from to change.to
    // and records PRIORITY_CHANGE; the event is written after the LOCK/BLOCK/
    // UNLOCK/RESUME event that caused it, so the trace reads in cause order.
    void onPriorityChanged(Job& job, const ResourcePriorityChange& change) override;

    // Direct requeue (used by tests): moves a runnable job to the ring of
    // `newPriority` (fixed priority only), keeps the bitmap in step and emits
    // PRIORITY_CHANGE at once. A blocked job just records the new value.
    // Reordering resource wait queues stays with the ResourceManager.
    void changeEffectivePriority(Job& job, int newPriority, const char* reason);

    // Throws std::logic_error if any ready-structure invariant is broken:
    // bitmap bit p set iff ring p nonempty; every READY/RUNNING job is in the
    // ready structure exactly once and nothing else is; at most one RUNNING
    // job and it is `running()`. Called at every boundary (with the extra rule
    // that the running job is the most urgent one); cheap for demo sizes.
    void checkInvariants() const;

    const Job* running() const noexcept { return running_; }
    Tick now() const noexcept { return now_; }

private:
    // EDF heap position tracking: keeps Job::edfIndex equal to the heap slot,
    // so removing a job from the heap is O(log n) instead of a linear search.
    struct EdfIndexHook {
        Registry* registry = nullptr;
        void operator()(const EdfEntry& entry, int index) const;
    };
    using EdfHeap = MinHeap<EdfEntry, std::less<EdfEntry>, EdfIndexHook>;

    Registry& registry_;
    RunTrace& trace_;
    ResourceManager* resources_ = nullptr;
    TimerManager* timers_ = nullptr;
    bool attached_ = false;
    bool ran_ = false;
    const bool edf_;

    CircularList<Job> rings_[PriorityBitmap::kPriorityLevels];
    PriorityBitmap bitmap_;
    EdfHeap edf_heap_;

    Job* running_ = nullptr;       // The one RUNNING job, or null (idle).
    const Job* rotated_ = nullptr; // Job whose slice just expired (PREEMPT reason).
    Tick now_ = 0;
    TickPhase phase_ = TickPhase::Settle;
    Sequence nextReadySequence_ = 1;

    ControlAction controls_[kMaxControls]{};
    std::size_t controlCount_ = 0;
    std::size_t nextControl_ = 0;

    std::size_t taskOrder_[kMaxTasks]{}; // Scenario task indices, by task ID.
    DynamicArray woken_;                 // Reserved to maxJobs: no mid-run growth.
    JobId readyBuffer_[kMaxJobs]{};      // Snapshot ready order (scratch).
    EdfEntry edfBuffer_[kMaxJobs]{};     // EDF snapshot sorting (scratch).

    // PRIORITY_CHANGE events raised inside a resource call, written once the
    // caller has written its own event. One lock donates once; one unlock
    // restores the owner and may donate to the new owner, so 4 is ample.
    static constexpr std::size_t kMaxPendingPriorityEvents = 4;
    TraceEvent pendingPriority_[kMaxPendingPriorityEvents]{};
    std::size_t pendingPriorityCount_ = 0;

    // Boundary phases.
    void boundary(Tick t);
    void settle();
    void applyInputs();
    void releaseDueJobs();
    void wakeTimers();
    void applyControls();
    void stabilize();
    void recordDeadlines();
    void recordSnapshot();
    void execute();

    // Ready structure.
    void enqueue(Job& job);
    void dequeue(Job& job);
    bool isQueued(const Job& job) const;
    Job* mostUrgent() const;
    bool rotateIfSliceUsed(Job& job);

    // Script interpreter.
    enum class StepMode { SettleOnly, Full };
    bool runZeroTimeSteps(Job& job, StepMode mode);
    void complete(Job& job);
    void dispatch(Job& job);
    void noteGranted(std::optional<JobId> granted, ResourceId id, const char* reason);

    // Controls.
    Job* latestUnfinishedJob(TaskId taskId);
    void suspend(Job& job);
    void resume(Job& job);

    void emit(TraceEvent event);
    void flushPriorityEvents();
    void moveRing(Job& job, int from, int to);
    void checkPriority(int priority) const;
    TraceEvent event(EventType type, const char* reason) const;
};

} // namespace b6
#endif
