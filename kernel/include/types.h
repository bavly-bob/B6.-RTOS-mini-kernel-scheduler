#ifndef B6_KERNEL_TYPES_H
#define B6_KERNEL_TYPES_H

#include "circular_list.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace b6 {

using Tick = std::int64_t;
using TaskId = int;
using JobId = int;
using ResourceId = int;
using TimerId = int;
using Sequence = std::uint64_t;

// Fixed storage ceilings; RunLimits may lower these for a particular run.
constexpr std::size_t kMaxTasks = 32;
constexpr std::size_t kMaxJobs = 512;
constexpr std::size_t kMaxResources = 8; // Per resource kind.
constexpr std::size_t kMaxScriptLength = 64;
constexpr Tick kMaxHorizon = 2000;

enum class Policy { FixedPriority, RateMonotonic, EDF };
enum class TimerBackend { SortedList, Heap };
enum class JobState { Ready, Running, Blocked, Suspended, Completed };
enum class BlockReason { None, Timer, Mutex, Semaphore };
enum class OpCode { Run, Sleep, Lock, Unlock, Wait, Signal, End };
enum class TickPhase { Settle = 1, Inputs, Stabilize, Deadlines, Snapshot, Execute };

struct Instruction {
    OpCode op = OpCode::End;
    Tick durationTicks = 0; // RUN/SLEEP only; positive.
    std::optional<ResourceId> resourceId; // Resource operations only.
};

// Immutable after loading. Each periodic release creates a separate Job.
struct TaskSpec {
    TaskId taskId = 0;
    std::string name;
    int basePriority = 0; // 0..31; larger is more urgent.
    Tick firstRelease = 0;
    Tick periodTicks = 0; // Zero means one-shot.
    Tick relativeDeadline = 1;
    Instruction script[kMaxScriptLength]{};
    std::size_t scriptLength = 0;
};

using TCB = TaskSpec; // One task definition, not a second mutable job record.

struct Job {
    JobId jobId = 0;
    TaskId taskId = 0;
    Tick releaseTick = 0;
    Tick absoluteDeadline = 0;
    std::optional<Tick> firstStartTick;
    std::optional<Tick> completionTick;
    std::size_t scriptIndex = 0;
    Tick cpuBurstRemaining = 0;
    Tick totalCpuUsed = 0;
    JobState state = JobState::Ready;
    BlockReason blockReason = BlockReason::None;
    bool suspended = false; // Independent of a timer/resource wait.
    std::optional<ResourceId> blockedResourceId;
    std::optional<ResourceId> heldMutexId;
    std::optional<Tick> wakeTick;
    std::optional<TimerId> timerId;
    int effectivePriority = 0;
    Sequence releaseSequence = 0;
    Sequence readySequence = 0;
    Sequence waitSequence = 0;
    Tick quantumRemaining = 0;
    CircularList<Job>::Node* readyNode = nullptr;
    CircularList<Job>::Node* waitNode = nullptr;
    bool inEdfQueue = false;
    std::optional<std::size_t> edfIndex;
    bool deadlineMissRecorded = false;
    std::optional<Tick> deadlineMissTick;
};

struct TimerEntry {
    TimerId timerId = 0;
    JobId jobId = 0;
    Tick wakeTick = 0;
    Sequence insertionSequence = 0;
    bool active = true;

    // Both timer backends use the same stable key and cancellation identity.
    bool operator<(const TimerEntry& other) const {
        return wakeTick < other.wakeTick ||
               (wakeTick == other.wakeTick && insertionSequence < other.insertionSequence);
    }
    bool operator==(const TimerEntry& other) const { return timerId == other.timerId; }
};

struct MutexSpec { ResourceId resourceId = 0; };
struct SemaphoreSpec {
    ResourceId resourceId = 0;
    int initialCount = 0;
    int maxCount = 1;
};

// ResourceManager maintains priority/sequence order in these borrowed-job rings.
struct MutexRecord {
    ResourceId resourceId = 0;
    std::optional<JobId> ownerJobId;
    CircularList<Job> waiters;
};
struct SemaphoreRecord {
    ResourceId resourceId = 0;
    int count = 0;
    int maxCount = 1;
    CircularList<Job> waiters;
};

struct RunLimits {
    std::size_t maxTasks = kMaxTasks;
    std::size_t maxJobs = kMaxJobs;
    std::size_t maxMutexes = kMaxResources;
    std::size_t maxSemaphores = kMaxResources;
    std::size_t maxScriptLength = kMaxScriptLength;
    Tick maxHorizonTicks = kMaxHorizon;
    std::size_t maxEvents = 50000;
    std::size_t maxSnapshotJobRows = 100000;
};

struct RunConfig {
    std::string scenarioId;
    Policy policy = Policy::FixedPriority;
    TimerBackend timerBackend = TimerBackend::SortedList;
    bool inheritance = false;
    Tick horizonTicks = 1;
    Tick tickMs = 1;
    Tick quantumTicks = 1;
    RunLimits limits;
};

// Input records own their strings/scripts and never borrow caller storage.
struct Scenario {
    RunConfig config;
    TaskSpec tasks[kMaxTasks]{};
    std::size_t taskCount = 0;
    MutexSpec mutexes[kMaxResources]{};
    std::size_t mutexCount = 0;
    SemaphoreSpec semaphores[kMaxResources]{};
    std::size_t semaphoreCount = 0;
};

} // namespace b6
#endif
