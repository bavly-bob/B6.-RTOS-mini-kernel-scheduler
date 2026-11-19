#ifndef B6_KERNEL_RUN_TRACE_H
#define B6_KERNEL_RUN_TRACE_H

#include "bounded_array.h"
#include "resource_manager.h"

#include <cstdint>
#include <string>

namespace b6 {

// ---------------------------------------------------------------------------
// RunTrace: bounded, append-only record of one run, filled by the engine and
// serialized by the run exporter (docs/run-schema-v1.md).
//
// The engine records three things:
//   events    - ordered explanatory changes, sorted by (tick, phase, seq);
//   segments  - CPU intervals [start, end), including idle (jobId = null);
//   snapshots - the stable state at each boundary, after phase 3 decisions and
//               before phase 6 executes, so the dashboard can show any past tick.
//
// Capacity: every array is allocated once from RunLimits/horizon. When an
// event or snapshot limit is reached, further records are dropped, the trace is
// marked truncated and invalid, and the limit name is listed; nothing is
// silently lost. Ordering mistakes (time going backwards, overlapping CPU
// intervals) are engine bugs and throw std::logic_error.
//
// Strings: event reasons and data keys must be string literals (static
// lifetime); they are stored as pointers. Warning/error texts are copied.
// No C++ pointer value is ever written to the run file.
// ---------------------------------------------------------------------------

enum class EventType {
    Release, Dispatch, Preempt, Block, Wake, Suspend, Resume, Complete,
    Lock, Unlock, Wait, Signal, PriorityChange, DeadlineMiss, Error
};

// One named value inside an event's "data" object.
struct EventDatum {
    enum class Kind { Integer, Boolean, Null };
    const char* key = "";
    Kind kind = Kind::Null;
    std::int64_t value = 0;  // Integer value, or 0/1 for Boolean.
};

constexpr std::size_t kMaxEventData = 4;

struct TraceEvent {
    Tick tick = 0;
    TickPhase phase = TickPhase::Settle;
    Sequence seq = 0;  // Assigned by RunTrace::addEvent, starting at 1.
    EventType type = EventType::Error;
    std::optional<JobId> jobId;
    std::optional<TaskId> taskId;
    std::optional<ResourceId> resourceId;
    const char* reason = "";
    EventDatum data[kMaxEventData]{};
    std::size_t dataCount = 0;

    // Small builder so engine code stays readable, e.g.
    //   trace.addEvent(TraceEvent::at(t, TickPhase::Stabilize, EventType::PriorityChange,
    //                  "inheritance").job(owner).resource(r).with("from", 1).with("to", 3));
    static TraceEvent at(Tick tick, TickPhase phase, EventType type, const char* reason);
    TraceEvent& job(const Job& job);   // Sets jobId and taskId together.
    TraceEvent& job(JobId id);
    TraceEvent& task(TaskId id);
    TraceEvent& resource(ResourceId id);
    TraceEvent& with(const char* key, std::int64_t value);   // Integer datum.
    TraceEvent& flag(const char* key, bool value);           // Boolean datum.
    TraceEvent& withNull(const char* key);                   // Null datum.
};

struct TraceSegment {
    Tick start = 0;
    Tick end = 0;
    std::optional<JobId> jobId;           // nullopt = idle.
    std::optional<int> basePriority;      // nullopt when idle.
    std::optional<int> effectivePriority; // nullopt when idle.
};

// One job's state at a snapshot tick (copied, never a pointer).
struct SnapshotJobRow {
    JobId jobId = 0;
    TaskId taskId = 0;
    JobState state = JobState::Ready;
    bool suspended = false;
    int basePriority = 0;
    int effectivePriority = 0;
    Tick cpuBurstRemaining = 0;
    Tick totalCpuUsed = 0;
    std::size_t scriptIndex = 0;
    BlockReason blockReason = BlockReason::None;
    std::optional<ResourceId> blockedResourceId;
    std::optional<ResourceId> heldMutexId;
    std::optional<Tick> wakeTick;
    bool deadlineMissed = false;
};

struct SnapshotResourceRow {
    ResourceId resourceId = 0;
    bool isMutex = true;
    std::optional<JobId> ownerJobId;  // Mutex only.
    int count = 0;                    // Semaphore only.
    int maxCount = 0;                 // Semaphore only.
    std::size_t firstWaiter = 0;      // Range in the shared ID pool.
    std::size_t waiterCount = 0;
};

struct TraceSnapshot {
    Tick tick = 0;
    std::optional<JobId> runningJobId;
    std::size_t firstReady = 0, readyCount = 0;       // Range in the ID pool.
    std::size_t firstRow = 0, rowCount = 0;           // Range in the job-row pool.
    std::size_t firstResource = 0, resourceCount = 0; // Range in the resource pool.
};

class RunTrace {
public:
    // Capacities come from the scenario's RunLimits and horizon.
    explicit RunTrace(const RunConfig& config);
    RunTrace(const RunTrace&) = delete;
    RunTrace& operator=(const RunTrace&) = delete;

    // --- Events -----------------------------------------------------------
    // Appends with the next sequence number. (tick, phase) must not decrease.
    // Returns false (and truncates) when maxEvents is reached.
    bool addEvent(TraceEvent event);

    // --- CPU segments -----------------------------------------------------
    // Records that `job` (or idle, when null) ran in [tick, tick + 1) with the
    // given priorities. Extends the previous segment when it is contiguous and
    // identical; otherwise starts a new one (so a priority change splits it).
    // Throws std::logic_error for tick < 0, tick >= horizon or out-of-order ticks.
    void recordInterval(Tick tick, const Job* job, int basePriority);
    void recordIdle(Tick tick);

    // --- Snapshots ----------------------------------------------------------
    // Records the boundary state at `tick`: every released job (from the
    // registry), the ready order chosen by the scheduler (job IDs, most urgent
    // first), and every mutex/semaphore with ordered waiters. Ticks must
    // strictly increase. Returns false (and truncates) if the row limit would
    // be exceeded; then no partial snapshot is kept.
    bool captureSnapshot(Tick tick, std::optional<JobId> runningJobId,
                         const JobId* readyOrder, std::size_t readyCount,
                         const Registry& registry, const ResourceManager* resources);

    // --- Run status -------------------------------------------------------
    void addWarning(const std::string& text);    // Kept up to kMaxWarnings.
    void markInvalid(const std::string& error);  // First error is kept.
    bool isValid() const noexcept { return error_.empty(); }
    bool isTruncated() const noexcept { return eventsTruncated_ || snapshotsTruncated_; }
    const std::string& error() const noexcept { return error_; }

    // --- Read access for the exporter --------------------------------------
    const BoundedArray<TraceEvent>& events() const noexcept { return events_; }
    const BoundedArray<TraceSegment>& segments() const noexcept { return segments_; }
    const BoundedArray<TraceSnapshot>& snapshots() const noexcept { return snapshots_; }
    const BoundedArray<SnapshotJobRow>& jobRows() const noexcept { return jobRows_; }
    const BoundedArray<SnapshotResourceRow>& resourceRows() const noexcept { return resourceRows_; }
    const BoundedArray<JobId>& idPool() const noexcept { return idPool_; }
    const BoundedArray<std::string>& warnings() const noexcept { return warnings_; }
    bool eventsTruncated() const noexcept { return eventsTruncated_; }
    bool snapshotsTruncated() const noexcept { return snapshotsTruncated_; }

    static constexpr std::size_t kMaxWarnings = 32;

private:
    Tick horizon_;
    BoundedArray<TraceEvent> events_;
    BoundedArray<TraceSegment> segments_;
    BoundedArray<TraceSnapshot> snapshots_;
    BoundedArray<SnapshotJobRow> jobRows_;
    BoundedArray<SnapshotResourceRow> resourceRows_;
    BoundedArray<JobId> idPool_;       // Ready orders and waiter lists.
    BoundedArray<std::string> warnings_;
    Sequence nextSeq_ = 1;
    bool eventsTruncated_ = false;
    bool snapshotsTruncated_ = false;
    std::string error_;

    void recordSegment(Tick tick, std::optional<JobId> job, std::optional<int> base,
                       std::optional<int> effective);
    static bool pushWaiters(BoundedArray<JobId>& pool, const CircularList<Job>& waiters);
};

const char* toString(EventType type);
const char* toString(JobState state);
const char* toString(BlockReason reason);

} // namespace b6
#endif
