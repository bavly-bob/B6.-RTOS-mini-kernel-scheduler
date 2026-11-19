#ifndef B6_KERNEL_TIMER_MANAGER_H
#define B6_KERNEL_TIMER_MANAGER_H

#include "DynamicArray.h"
#include "MinHeap.h"
#include "registry.h"
#include "sortedList.h"

namespace b6 {

// ---------------------------------------------------------------------------
// TimerQueue: the one ordering contract both required timer backends satisfy.
//
// Entries are TimerEntry values ordered by (wakeTick, insertionSequence), so
// the head is always the next timer to expire and equal wake ticks leave in
// insertion order. Identity is the timer ID (TimerEntry::operator==).
// The queue knows nothing about jobs, states or the scheduler.
// ---------------------------------------------------------------------------
class TimerQueue {
public:
    virtual ~TimerQueue() = default;
    virtual void insert(const TimerEntry& entry) = 0;   // Keeps the order above.
    virtual bool peek(TimerEntry& out) const = 0;       // False when empty.
    virtual bool popHead() = 0;                         // False when empty.
    virtual bool remove(TimerId id) = 0;                // False when absent.
    virtual std::size_t size() const = 0;
    virtual void clear() = 0;
    // Pre-run reservation. Only the heap can reserve; list nodes are allocated
    // one per insert, which is an honest, documented allocator cost.
    virtual void reserve(std::size_t capacity) = 0;
    virtual const char* name() const = 0;               // Schema spelling.
};

// One thin adapter for both structures. sortedList and MinHeap deliberately
// share insertTask/getValueAtHead/removeHead/deleteTask, so no per-backend
// branching is needed. Costs come from the underlying structure:
//                 insert      peek   popHead    remove(id)
//   sortedList    O(n)        O(1)   O(1)       O(n) search + O(1) unlink
//   MinHeap       O(log n)    O(1)   O(log n)   O(n) search + O(log n) repair
template <class Storage>
class TimerQueueAdapter final : public TimerQueue {
public:
    explicit TimerQueueAdapter(const char* backendName) : name_(backendName) {}
    void insert(const TimerEntry& entry) override { storage_.insertTask(entry); }
    bool peek(TimerEntry& out) const override { return storage_.getValueAtHead(out); }
    bool popHead() override { return storage_.removeHead(); }
    bool remove(TimerId id) override {
        TimerEntry probe;     // operator== compares only timerId, so the
        probe.timerId = id;   // wake tick is not needed to find the entry.
        return storage_.deleteTask(probe);
    }
    std::size_t size() const override { return static_cast<std::size_t>(storage_.size()); }
    void clear() override { storage_.clear(); }
    void reserve(std::size_t capacity) override;
    const char* name() const override { return name_; }

private:
    Storage storage_;
    const char* name_;
};

// ---------------------------------------------------------------------------
// TimerManager: sleeping jobs and their wake-ups, for one run.
//
// The same calls with the same ticks produce the same wake order under either
// backend. That equivalence is what the timer experiment compares.
//
// Division of work with the scheduler (mirrors ResourceManager):
// - sleep(): the scheduler's interpreter calls it while the job is RUNNING and
//   executing SLEEP n. The manager validates the job, sets its wait metadata
//   (BLOCKED, BlockReason::Timer, wakeTick, timerId) and queues one timer.
//   The scheduler then removes the job's ready membership (clear readyNode,
//   update the bitmap), advances scriptIndex once, and reselects.
// - expireDue(): called once per boundary in phase 2, after releases. Every
//   active timer with wakeTick <= now is removed in (wakeTick, sequence)
//   order. Its job's wait is resolved: block reason cleared, wakeTick/timerId
//   reset, a fresh quantum. An unsuspended job becomes READY; a suspended job
//   stays SUSPENDED with its wait resolved (resume later makes it READY).
//   Every woken job is appended to `woken`; the scheduler enqueues only the
//   READY ones and emits one WAKE event per entry.
// The manager never edits ready rings, the bitmap, or scriptIndex.
//
// Lifetime: the Registry must outlive the manager. Job pointers handed out in
// `woken` are borrowed from the registry (stable for the whole run).
// Capacity: one timer per job at most, so the capacity is limits.maxJobs and
// can never overflow; the heap is reserved to it at construction.
// Errors: invalid caller input throws std::invalid_argument; broken
// invariants (wrong state, time moving backwards) throw std::logic_error.
// After any exception, treat the run as invalid and stop.
// ---------------------------------------------------------------------------
class TimerManager {
public:
    // Uses the backend chosen in the registry's RunConfig.
    explicit TimerManager(Registry& registry);
    // Explicit backend, for experiments that compare both on one scenario.
    TimerManager(Registry& registry, TimerBackend backend);
    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    TimerBackend backend() const noexcept { return backend_; }
    const char* backendName() const noexcept { return active_->name(); }

    // SLEEP `duration` ticks (> 0) starting at `now`: wakes at now + duration.
    // Precondition: job RUNNING, not suspended, no unresolved wait, no held
    // mutex and no active timer. Returns the new timer ID. Cost: one queue insert.
    TimerId sleep(JobId jobId, Tick now, Tick duration);

    // Earliest pending wake tick, or nullopt when nothing is sleeping. O(1).
    std::optional<Tick> nextWake() const;

    // Wakes every timer due at `now` (see above). Clears `woken`, then appends
    // each woken Job* in wake order. Reserve `woken` to limits.maxJobs before
    // the run so this never allocates. Returns the number woken.
    // Cost: O(k) list pops or O(k log n) heap pops for k due timers.
    std::size_t expireDue(Tick now, DynamicArray& woken);

    // Removes a pending timer and clears its job's timerId/wakeTick. The job's
    // state is left to the caller (only used for teardown/abort paths; a
    // normal wake always goes through expireDue). False if not pending. O(n).
    bool cancel(TimerId timerId);

    std::size_t pendingCount() const noexcept { return active_->size(); }

    // Ends every pending timer for a fresh run: clears each sleeping job's
    // timerId/wakeTick and empties the queue. Sequences restart.
    void reset();

private:
    Registry& registry_;
    TimerBackend backend_;
    TimerQueueAdapter<sortedList<TimerEntry>> sortedBackend_{"sorted_list"};
    TimerQueueAdapter<MinHeap<TimerEntry>> heapBackend_{"heap"};
    TimerQueue* active_ = nullptr;   // Points at exactly one of the two above.
    TimerId nextTimerId_ = 1;        // Unique per run; 0 is never issued.
    Sequence nextSequence_ = 1;      // Tie-break among equal wake ticks.
    Tick lastNow_ = 0;               // Simulated time must not move backwards.
    bool started_ = false;           // lastNow_ is meaningful only after a call.

    void observeTime(Tick now);
    Job& jobFor(const TimerEntry& entry);
};

template <class Storage>
void TimerQueueAdapter<Storage>::reserve(std::size_t) {}  // sortedList: no-op.

template <>
inline void TimerQueueAdapter<MinHeap<TimerEntry>>::reserve(std::size_t capacity) {
    storage_.reserve(static_cast<int>(capacity));
}

} // namespace b6
#endif
