#include "rate_monotonic.h"
#include "registry.h"
#include "validation.h"
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace b6;

// Checks remain active in Release builds too.
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    std::exit(1); } } while (false)

static Scenario basicScenario() {
    Scenario s;
    s.config.scenarioId = "foundation_test";
    s.config.horizonTicks = 10;
    s.taskCount = 1;
    TaskSpec& t = s.tasks[0];
    t.taskId = 0; // Zero is a valid ID, never a missing-value sentinel.
    t.name = "worker";
    t.basePriority = 5;
    t.relativeDeadline = 3;
    t.scriptLength = 2;
    t.script[0] = {OpCode::Run, 2, std::nullopt};
    t.script[1] = {OpCode::End, 0, std::nullopt};
    return s;
}

template <typename Change>
static void rejects(Change change, const char* messagePart) {
    Scenario s = basicScenario();
    change(s);
    const std::string error = validateScenario(s);
    CHECK(!error.empty());
    CHECK(error.find(messagePart) != std::string::npos);
    bool caught = false;
    try { Registry registry(s); }
    catch (const std::invalid_argument&) { caught = true; }
    CHECK(caught);
}

template <typename Operation>
static void rejectsRelease(Operation operation) {
    bool caught = false;
    try { operation(); }
    catch (const std::invalid_argument&) { caught = true; }
    CHECK(caught);
}

static void testValidation() {
    CHECK(validateScenario(basicScenario()).empty());
    rejects([](Scenario& s) { s.config.scenarioId.clear(); }, "scenarioId");
    rejects([](Scenario& s) { s.config.policy = static_cast<Policy>(99); }, "policy");
    rejects([](Scenario& s) { s.config.timerBackend = static_cast<TimerBackend>(99); }, "timerBackend");
    rejects([](Scenario& s) { s.config.quantumTicks = 0; }, "quantumTicks");
    rejects([](Scenario& s) { s.config.tickMs = -1; }, "tickMs");
    rejects([](Scenario& s) { s.config.horizonTicks = 2001; }, "horizonTicks");
    rejects([](Scenario& s) { s.config.limits.maxJobs = 513; }, "limits");
    rejects([](Scenario& s) { s.taskCount = 33; }, "maxTasks");
    rejects([](Scenario& s) { s.mutexCount = 9; }, "maxMutexes");
    rejects([](Scenario& s) { s.semaphoreCount = 9; }, "maxSemaphores");
    rejects([](Scenario& s) { s.taskCount = 2; s.tasks[1] = s.tasks[0]; }, "duplicated");
    rejects([](Scenario& s) { s.tasks[0].taskId = -1; }, "taskId");
    rejects([](Scenario& s) { s.tasks[0].name.clear(); }, "name");
    rejects([](Scenario& s) { s.tasks[0].basePriority = 32; }, "basePriority");
    rejects([](Scenario& s) { s.tasks[0].basePriority = -1; }, "basePriority");
    rejects([](Scenario& s) { s.tasks[0].firstRelease = -1; }, "releases");
    rejects([](Scenario& s) { s.tasks[0].periodTicks = -1; }, "periods");
    rejects([](Scenario& s) { s.tasks[0].relativeDeadline = 0; }, "deadline");
    rejects([](Scenario& s) { s.tasks[0].relativeDeadline = std::numeric_limits<Tick>::max(); }, "overflow");
    rejects([](Scenario& s) { s.tasks[0].scriptLength = 0; }, "scriptLength");
    rejects([](Scenario& s) { s.tasks[0].scriptLength = 65; }, "scriptLength");
    rejects([](Scenario& s) { s.tasks[0].script[0].durationTicks = 0; }, "duration");
    rejects([](Scenario& s) { s.tasks[0].script[0].resourceId = 0; }, "resource ID");
    rejects([](Scenario& s) { s.tasks[0].script[1].durationTicks = 1; }, "zero-time");
    rejects([](Scenario& s) { s.tasks[0].script[0] = {}; }, "last instruction");
    rejects([](Scenario& s) { s.tasks[0].scriptLength = 1; }, "finish with END");
    rejects([](Scenario& s) { s.tasks[0].script[0].op = static_cast<OpCode>(99);
                              s.tasks[0].script[0].durationTicks = 0; }, "unsupported opcode");
    rejects([](Scenario& s) { s.tasks[0].script[0] = {OpCode::Sleep, 0, {}}; }, "duration");
    rejects([](Scenario& s) { s.tasks[0].script[0] = {OpCode::Sleep, std::numeric_limits<Tick>::max(), {}}; }, "overflow");
    rejects([](Scenario& s) { s.tasks[0].periodTicks = 1; s.config.limits.maxJobs = 9; }, "scheduled releases");

    // Empty scenarios allow the future scheduler to produce an idle trace.
    Scenario idle = basicScenario();
    idle.taskCount = 0;
    CHECK(validateScenario(idle).empty());
    for (Policy policy : {Policy::RateMonotonic, Policy::EDF}) {
        Scenario periodic = basicScenario();
        periodic.config.policy = policy;
        periodic.tasks[0].periodTicks = 5;
        CHECK(validateScenario(periodic).empty());
        periodic.config.inheritance = true;
        CHECK(!validateScenario(periodic).empty());
        periodic.config.inheritance = false;
        periodic.tasks[0].script[0].op = OpCode::Sleep;
        CHECK(!validateScenario(periodic).empty());
    }
}

static void testResourceDefinitions() {
    Scenario s = basicScenario();
    s.mutexCount = 1;
    s.mutexes[0].resourceId = 0;
    s.semaphoreCount = 1;
    s.semaphores[0] = {1, 0, 1};
    TaskSpec& t = s.tasks[0];
    t.scriptLength = 6;
    t.script[0] = {OpCode::Lock, 0, 0};
    t.script[1] = {OpCode::Run, 1, {}};
    t.script[2] = {OpCode::Unlock, 0, 0};
    t.script[3] = {OpCode::Wait, 0, 1};
    t.script[4] = {OpCode::Signal, 0, 1};
    t.script[5] = {};
    CHECK(validateScenario(s).empty());
    t.script[1] = {OpCode::Lock, 0, 0};
    CHECK(validateScenario(s).find("recursive or nested") != std::string::npos);
    t.script[1] = {OpCode::Sleep, 1, {}};
    CHECK(validateScenario(s).find("holding a mutex") != std::string::npos);
    t.script[1] = {OpCode::Wait, 0, 1};
    CHECK(validateScenario(s).find("holding a mutex") != std::string::npos);
    t.script[1] = {OpCode::Run, 1, {}};
    t.script[2] = {OpCode::Run, 1, {}};
    t.script[3] = {OpCode::Run, 1, {}};
    CHECK(validateScenario(s).find("complete while holding") != std::string::npos);

    rejects([](Scenario& x) { x.tasks[0].script[0] = {OpCode::Lock, 0, {}}; }, "missing");
    rejects([](Scenario& x) { x.tasks[0].script[0] = {OpCode::Lock, 0, 99}; }, "unknown mutex");
    rejects([](Scenario& x) { x.tasks[0].script[0] = {OpCode::Unlock, 0, 0}; }, "not held");
    rejects([](Scenario& x) { x.tasks[0].script[0] = {OpCode::Wait, 0, 99}; }, "unknown semaphore");
    rejects([](Scenario& x) { x.semaphoreCount = 1; x.semaphores[0] = {1, 2, 1}; }, "counts");
    rejects([](Scenario& x) { x.mutexCount = 2; }, "duplicate mutex");
    rejects([](Scenario& x) { x.semaphoreCount = 2; }, "duplicate semaphore");
    rejects([](Scenario& x) { x.mutexCount = 1; x.semaphoreCount = 1; }, "unique across resources");

    MutexRecord mutex;
    SemaphoreRecord semaphore;
    CHECK(!mutex.ownerJobId && mutex.waiters.isEmpty());
    CHECK(semaphore.count == 0 && semaphore.maxCount == 1 && semaphore.waiters.isEmpty());
}

static void testStableRegistryAndOverlappingJobs() {
    static_assert(!std::is_copy_constructible<Registry>::value);
    static_assert(!std::is_move_constructible<Registry>::value);
    Scenario s = basicScenario();
    s.tasks[0].periodTicks = 2;
    Registry registry(s);
    const TaskSpec* task = registry.findTask(0);
    s.tasks[0].name = "caller changed";
    s.tasks[0].script[0].durationTicks = 99;
    CHECK(task && task->name == "worker" && task->script[0].durationTicks == 2);
    CHECK(registry.findTask(999) == nullptr && registry.findJob(0) == nullptr);
    Job& first = registry.releaseJob(0, 0);
    first.cpuBurstRemaining = 1;
    first.state = JobState::Suspended;
    first.blockReason = BlockReason::Timer;
    first.suspended = true;
    first.wakeTick = 5;
    Job& second = registry.releaseJob(0, 2);
    CHECK(&first != &second && first.jobId == 1 && second.jobId == 2);
    CHECK(first.cpuBurstRemaining == 1 && first.suspended && first.wakeTick == 5);
    CHECK(first.state == JobState::Suspended && first.blockReason == BlockReason::Timer);
    CHECK(second.releaseTick == 2 && second.absoluteDeadline == 5);
    CHECK(second.state == JobState::Ready && second.effectivePriority == 5);
    CHECK(!second.firstStartTick && !second.completionTick && !second.deadlineMissTick);
    CHECK(second.readyNode == nullptr && second.waitNode == nullptr && !second.inEdfQueue);
    CHECK(registry.findJob(1) == &first && registry.findTask(0) == task);
    const Registry& readOnly = registry;
    CHECK(readOnly.findJob(2) == &second);

    rejectsRelease([&] { registry.releaseJob(0, 2); });
    rejectsRelease([&] { registry.releaseJob(0, 0); });
    rejectsRelease([&] { registry.releaseJob(0, 3); });
    rejectsRelease([&] { registry.releaseJob(0, 10); });
    rejectsRelease([&] { registry.releaseJob(999, 4); });
    CHECK(registry.jobCount() == 2);
    CHECK(registry.releaseJob(0, 4).jobId == 3);
}

static void testReleaseBoundariesAndCapacity() {
    Scenario s = basicScenario();
    s.taskCount = 2;
    s.tasks[1] = s.tasks[0];
    s.tasks[1].taskId = 31;
    s.tasks[1].basePriority = 31;
    Registry registry(s);
    registry.releaseJob(0, 0);
    Job& high = registry.releaseJob(31, 0);
    CHECK(high.effectivePriority == 31 && high.releaseSequence == 2);
    rejectsRelease([&] { registry.releaseJob(0, 1); });
    rejectsRelease([&] { registry.releaseJob(0, -1); });

    s = basicScenario();
    s.config.horizonTicks = 512;
    s.tasks[0].periodTicks = 1;
    Registry full(s);
    Job* first = nullptr;
    for (Tick t = 0; t < 512; ++t) {
        Job& job = full.releaseJob(0, t);
        if (t == 0) first = &job;
        CHECK(job.jobId == t + 1);
    }
    CHECK(full.jobCount() == 512 && full.findJob(1) == first);
    CHECK(first->completionTick == std::nullopt); // Unfinished jobs stay visible.
    s.config.horizonTicks = 513;
    CHECK(validateScenario(s).find("scheduled releases") != std::string::npos);

    s = basicScenario();
    s.tasks[0].firstRelease = s.config.horizonTicks;
    CHECK(validateScenario(s).empty());
    Registry future(s);
    rejectsRelease([&] { future.releaseJob(0, 10); });
    CHECK(future.jobCount() == 0);
}

static void testTimerOrdering() {
    TimerEntry early{1, 1, 5, 2, true};
    TimerEntry firstTie{2, 2, 10, 3, true};
    TimerEntry secondTie{3, 3, 10, 4, true};
    CHECK(early < firstTie && firstTie < secondTie && !(secondTie < firstTie));
    TimerEntry cancellationKey;
    cancellationKey.timerId = 2;
    CHECK(firstTie == cancellationKey);
}

// Periodic copies of the basic task with the given periods, in input order.
static Scenario periodicScenario(std::initializer_list<Tick> periods) {
    Scenario s = basicScenario();
    s.taskCount = 0;
    for (Tick period : periods) {
        TaskSpec& t = s.tasks[s.taskCount];
        t = s.tasks[0];
        t.taskId = static_cast<TaskId>(s.taskCount);
        t.name = "T" + std::to_string(s.taskCount);
        t.basePriority = 3; // Input priorities must be ignored under RM.
        t.periodTicks = period;
        t.relativeDeadline = period;
        ++s.taskCount;
    }
    return s;
}

static void testRateMonotonicAssignment() {
    // Shortest period first; equal periods share a level; input order is irrelevant.
    Scenario s = periodicScenario({7, 5, 20, 7});
    assignRateMonotonicPriorities(s);
    CHECK(s.tasks[0].basePriority == 30 && s.tasks[1].basePriority == 31);
    CHECK(s.tasks[2].basePriority == 29 && s.tasks[3].basePriority == 30);
    Scenario reversed = periodicScenario({7, 20, 5, 7});
    assignRateMonotonicPriorities(reversed);
    CHECK(reversed.tasks[0].basePriority == 30 && reversed.tasks[1].basePriority == 29);
    CHECK(reversed.tasks[2].basePriority == 31 && reversed.tasks[3].basePriority == 30);

    // A longer-period task does not move the existing levels.
    Scenario longer = periodicScenario({7, 5, 20, 7, 100});
    assignRateMonotonicPriorities(longer);
    CHECK(longer.tasks[1].basePriority == 31 && longer.tasks[4].basePriority == 28);

    // 32 distinct periods use every level exactly once, down to 0.
    Scenario full = basicScenario();
    for (std::size_t i = 0; i < kMaxTasks; ++i) {
        full.tasks[i] = full.tasks[0];
        full.tasks[i].taskId = static_cast<TaskId>(i);
        full.tasks[i].periodTicks = static_cast<Tick>(kMaxTasks - i); // Longest first.
    }
    full.taskCount = kMaxTasks;
    assignRateMonotonicPriorities(full);
    CHECK(full.tasks[0].basePriority == 0 && full.tasks[kMaxTasks - 1].basePriority == 31);

    // An oversized count is not indexed past the task array (validation rejects it).
    full.taskCount = kMaxTasks + 1;
    assignRateMonotonicPriorities(full);
    CHECK(full.tasks[0].basePriority == 0);

    // The registry assigns RM priorities on its copy; releases start from them.
    Scenario rm = periodicScenario({7, 5});
    rm.config.policy = Policy::RateMonotonic;
    Registry rmRegistry(rm);
    CHECK(rm.tasks[0].basePriority == 3); // The caller's scenario is unchanged.
    CHECK(rmRegistry.findTask(0)->basePriority == 30 && rmRegistry.findTask(1)->basePriority == 31);
    CHECK(rmRegistry.releaseJob(1, 0).effectivePriority == 31);

    // Fixed priority keeps the input priorities.
    Scenario fixed = periodicScenario({7, 5});
    Registry fixedRegistry(fixed);
    CHECK(fixedRegistry.findTask(0)->basePriority == 3 && fixedRegistry.findTask(1)->basePriority == 3);

    // RM still requires periodic tasks.
    rejects([](Scenario& bad) { bad.config.policy = Policy::RateMonotonic; }, "periodic");
}

int main() {
    testValidation();
    testResourceDefinitions();
    testStableRegistryAndOverlappingJobs();
    testReleaseBoundariesAndCapacity();
    testTimerOrdering();
    testRateMonotonicAssignment();
    std::cout << "All kernel foundation tests passed.\n";
}
