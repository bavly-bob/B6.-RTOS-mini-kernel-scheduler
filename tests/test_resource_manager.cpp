#include "resource_manager.h"
#include "bitmap.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

using namespace b6;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    std::exit(1); } } while (false)

template <typename Exception, typename Operation>
static void rejects(Operation operation) {
    bool caught = false;
    try { operation(); } catch (const Exception&) { caught = true; }
    CHECK(caught);
}

// Test adapter only: exercises the exact hook responsibilities given to Youssef.
class TestScheduler : public ResourceSchedulerHooks {
public:
    CircularList<Job> ready[32];
    PriorityBitmap bitmap;
    Job* current = nullptr;
    int blocks = 0;
    int wakes = 0;
    Tick now = 0;
    struct Change { Tick tick; ResourcePriorityChange event; };
    Change changes[64]{};
    std::size_t changeCount = 0;

    void add(Job& job) {
        CHECK(!job.readyNode && job.state == JobState::Ready);
        job.readyNode = ready[job.effectivePriority].pushBack(&job);
        bitmap.set(job.effectivePriority);
    }
    void remove(Job& job) {
        CHECK(job.readyNode);
        CHECK(ready[job.effectivePriority].remove(job.readyNode));
        job.readyNode = nullptr;
        if (ready[job.effectivePriority].isEmpty()) bitmap.clear(job.effectivePriority);
        if (current == &job) current = nullptr;
    }
    void onBlocked(Job& job) override {
        CHECK(job.state == JobState::Blocked && job.waitNode);
        remove(job);
        ++blocks;
    }
    void onReady(Job& job) override {
        CHECK(job.blockReason == BlockReason::None && !job.waitNode && !job.suspended);
        add(job);
        ++wakes;
    }
    void onPriorityChanged(Job& job, const ResourcePriorityChange& change) override {
        CHECK(change.jobId == job.jobId && job.effectivePriority == change.to);
        CHECK(changeCount < 64);
        changes[changeCount++] = {now, change};
        if (!job.readyNode) return; // Handoff recipient is not enqueued yet.
        CHECK(ready[change.from].remove(job.readyNode));
        job.readyNode = nullptr;
        if (ready[change.from].isEmpty()) bitmap.clear(change.from);
        job.readyNode = ready[change.to].pushBack(&job);
        bitmap.set(change.to);
        // Keep state, current selection and the unexpired quantum unchanged.
    }
    void run(Job& job) {
        if (current && current->state == JobState::Running) current->state = JobState::Ready;
        CHECK(job.readyNode && !job.suspended && job.blockReason == BlockReason::None);
        current = &job;
        job.state = JobState::Running;
    }
    Job* dispatch() {
        const int priority = bitmap.highestSetBit();
        if (priority < 0) return nullptr;
        Job* job = ready[priority].front();
        run(*job);
        return job;
    }
};

static Scenario scenario(int count = 5) {
    Scenario s;
    s.config.scenarioId = "resource_test";
    s.config.horizonTicks = 10;
    s.mutexCount = 2;
    s.mutexes[0].resourceId = 0;
    s.mutexes[1].resourceId = 1;
    s.semaphoreCount = 1;
    s.semaphores[0] = {2, 0, 2};
    s.taskCount = static_cast<std::size_t>(count);
    const int priorities[] = {1, 3, 5, 5, 2};
    for (int i = 0; i < count; ++i) {
        TaskSpec& t = s.tasks[i];
        t.taskId = i;
        t.name = "job";
        t.basePriority = priorities[i];
        t.relativeDeadline = 10;
        t.scriptLength = 2;
        t.script[0] = {OpCode::Run, 1, {}};
        t.script[1] = {};
    }
    return s;
}

struct Fixture {
    Registry registry;
    TestScheduler scheduler;
    ResourceManager resources;
    explicit Fixture(const Scenario& s)
        : registry(s), resources(registry, scheduler) {
        for (std::size_t i = 0; i < s.taskCount; ++i)
            scheduler.add(registry.releaseJob(s.tasks[i].taskId, 0));
    }
    Job& job(int index) { return *registry.findJob(index + 1); }
    void run(int index) { scheduler.run(job(index)); }
};

static void checkOrder(const CircularList<Job>& queue, const JobId* expected, std::size_t count) {
    CHECK(queue.size() == count);
    if (!count) { CHECK(queue.front() == nullptr); return; }
    auto* node = queue.front()->waitNode;
    auto* first = node;
    for (std::size_t i = 0; i < count; ++i) {
        CHECK(node && node->getValue()->jobId == expected[i]);
        CHECK(node->getValue()->waitNode == node);
        node = node->getNext();
    }
    CHECK(node == first);
}

static void testMutexOwnershipAndOrder() {
    Fixture f(scenario());
    CHECK(!f.resources.findMutex(99) && !f.resources.findSemaphore(99));
    f.run(0);
    CHECK(f.resources.lock(1, 0) == AcquireResult::Acquired);
    CHECK(f.job(0).heldMutexId == 0 && f.resources.findMutex(0)->ownerJobId == 1);
    rejects<std::logic_error>([&] { f.resources.lock(1, 0); });
    rejects<std::logic_error>([&] { f.resources.lock(1, 1); });
    rejects<std::logic_error>([&] { f.resources.wait(1, 2); });
    f.run(1);
    rejects<std::logic_error>([&] { f.resources.unlock(2, 0); });
    rejects<std::invalid_argument>([&] { f.resources.lock(2, 99); });
    rejects<std::invalid_argument>([&] { f.resources.lock(999, 0); });

    // Exercise middle insertion and an equal-priority insertion before a lower tail.
    const int arrivalOrder[] = {2, 4, 1, 3};
    for (int i : arrivalOrder) {
        f.run(i);
        CHECK(f.resources.lock(i + 1, 0) == AcquireResult::Blocked);
        CHECK(!f.job(i).readyNode && f.job(i).waitNode);
        CHECK(f.job(i).blockReason == BlockReason::Mutex && f.job(i).blockedResourceId == 0);
    }
    const JobId expected[] = {3, 4, 2, 5};
    checkOrder(f.resources.findMutex(0)->waiters, expected, 4);
    CHECK(f.job(2).waitSequence < f.job(3).waitSequence);
    rejects<std::logic_error>([&] { f.resources.lock(3, 0); }); // Duplicate wait.
    CHECK(f.scheduler.blocks == 4 && f.scheduler.wakes == 0);
    CHECK(f.scheduler.bitmap.highestSetBit() == 1);

    int owner = 0;
    for (JobId id : expected) {
        f.run(owner);
        CHECK(f.resources.unlock(owner + 1, 0) == id);
        CHECK(!f.job(owner).heldMutexId);
        owner = id - 1;
        CHECK(f.job(owner).heldMutexId == 0 && f.job(owner).readyNode && !f.job(owner).waitNode);
        CHECK(f.resources.findMutex(0)->ownerJobId == id);
    }
    f.run(owner);
    CHECK(!f.resources.unlock(owner + 1, 0));
    CHECK(!f.resources.findMutex(0)->ownerJobId);
    CHECK(f.resources.findMutex(0)->waiters.isEmpty());
    CHECK(f.scheduler.wakes == 4);
    rejects<std::logic_error>([&] { f.resources.unlock(owner + 1, 0); });
}

static void testSemaphorePermitsAndOrder() {
    Scenario s = scenario();
    s.semaphores[0].initialCount = 2;
    Fixture f(s);
    f.run(0);
    CHECK(f.resources.wait(1, 2) == AcquireResult::Acquired);
    CHECK(f.resources.wait(1, 2) == AcquireResult::Acquired);
    CHECK(f.resources.findSemaphore(2)->count == 0);
    for (int i = 1; i < 5; ++i) {
        f.run(i);
        CHECK(f.resources.wait(i + 1, 2) == AcquireResult::Blocked);
    }
    const JobId expected[] = {3, 4, 2, 5};
    checkOrder(f.resources.findSemaphore(2)->waiters, expected, 4);
    rejects<std::logic_error>([&] { f.resources.wait(3, 2); });
    f.run(0);
    for (JobId id : expected) {
        CHECK(f.resources.signal(1, 2) == id);
        CHECK(f.resources.findSemaphore(2)->count == 0);
        CHECK(f.registry.findJob(id)->state == JobState::Ready);
        CHECK(!f.registry.findJob(id)->heldMutexId);
    }
    CHECK(f.scheduler.wakes == 4);
    CHECK(!f.resources.signal(1, 2));
    CHECK(!f.resources.signal(1, 2));
    CHECK(f.resources.findSemaphore(2)->count == 2);
    rejects<std::overflow_error>([&] { f.resources.signal(1, 2); });
    CHECK(f.resources.findSemaphore(2)->count == 2);
    rejects<std::invalid_argument>([&] { f.resources.wait(1, 99); });
    rejects<std::invalid_argument>([&] { f.resources.signal(1, 99); });
}

static void testNoStolenPermit() {
    Fixture f(scenario(3));
    f.run(1);
    CHECK(f.resources.wait(2, 2) == AcquireResult::Blocked);
    f.run(0);
    CHECK(f.resources.signal(1, 2) == 2);
    f.run(2);
    CHECK(f.resources.wait(3, 2) == AcquireResult::Blocked);
    CHECK(f.job(1).state == JobState::Ready && f.job(2).state == JobState::Blocked);
    CHECK(f.resources.findSemaphore(2)->count == 0);
}

static void testSuspendedWaitsAndCleanup() {
    Fixture f(scenario(3));
    f.run(1);
    f.resources.wait(2, 2);
    f.job(1).suspended = true;
    f.job(1).state = JobState::Suspended;
    f.run(0);
    CHECK(f.resources.signal(1, 2) == 2);
    CHECK(f.job(1).state == JobState::Suspended && !f.job(1).readyNode);
    CHECK(f.job(1).blockReason == BlockReason::None && !f.job(1).waitNode);
    CHECK(f.scheduler.wakes == 0);

    f.resources.lock(1, 0);
    f.run(2);
    f.resources.lock(3, 0);
    f.job(2).suspended = true;
    f.job(2).state = JobState::Suspended;
    f.run(0);
    CHECK(!f.resources.unlock(1, 0));
    CHECK(!f.job(2).heldMutexId && f.job(2).waitNode);
    rejects<std::logic_error>([&] { f.resources.resumeMutexWait(3); });
    f.job(2).suspended = false;
    f.job(2).state = JobState::Blocked;
    CHECK(f.resources.resumeMutexWait(3) == 3);
    CHECK(f.job(2).heldMutexId == 0 && f.job(2).state == JobState::Ready);
    CHECK(f.scheduler.wakes == 1);

    Registry registry(scenario(2));
    TestScheduler scheduler;
    Job& owner = registry.releaseJob(0, 0);
    Job& waiter = registry.releaseJob(1, 0);
    scheduler.add(owner);
    scheduler.add(waiter);
    {
        ResourceManager resources(registry, scheduler);
        scheduler.run(owner);
        resources.lock(owner.jobId, 0);
        scheduler.run(waiter);
        resources.lock(waiter.jobId, 0);
        CHECK(waiter.waitNode);
    }
    CHECK(!waiter.waitNode && !owner.heldMutexId);
}

static void testUnsupportedModeAndCaller() {
    for (Policy policy : {Policy::RateMonotonic, Policy::EDF}) {
        Scenario s = scenario(1);
        s.config.policy = policy;
        s.mutexCount = s.semaphoreCount = 0;
        s.tasks[0].periodTicks = 2;
        Registry registry(s);
        TestScheduler scheduler;
        rejects<std::invalid_argument>([&] { ResourceManager resources(registry, scheduler); });
    }
    Fixture f(scenario(1));
    rejects<std::logic_error>([&] { f.resources.lock(1, 0); }); // READY is not executing.
    f.job(0).state = JobState::Completed;
    rejects<std::logic_error>([&] { f.resources.signal(1, 2); });
}

static void testHookFailureAbortsRun() {
    class FailingScheduler : public TestScheduler {
    public:
        bool failBlock = false;
        void onBlocked(Job& job) override {
            if (failBlock) throw std::runtime_error("test block failure");
            TestScheduler::onBlocked(job);
        }
        void onReady(Job&) override { throw std::runtime_error("test wake failure"); }
    };
    for (bool failBlock : {false, true}) {
        Registry registry(scenario(2));
        FailingScheduler scheduler;
        scheduler.failBlock = failBlock;
        ResourceManager resources(registry, scheduler);
        Job& producer = registry.releaseJob(0, 0);
        Job& consumer = registry.releaseJob(1, 0);
        scheduler.add(producer);
        scheduler.add(consumer);
        scheduler.run(consumer);
        if (failBlock) {
            rejects<std::runtime_error>([&] { resources.wait(consumer.jobId, 2); });
            CHECK(consumer.waitNode && consumer.state == JobState::Blocked);
        } else {
            resources.wait(consumer.jobId, 2);
            scheduler.run(producer);
            rejects<std::runtime_error>([&] { resources.signal(producer.jobId, 2); });
            CHECK(!consumer.waitNode && consumer.blockReason == BlockReason::None);
        }
        CHECK(resources.findSemaphore(2)->count == 0);
        rejects<std::logic_error>([&] { resources.signal(producer.jobId, 2); });
    }
}

static void testProducerConsumerOracle() {
    Scenario s = scenario(2);
    s.config.scenarioId = "semaphore_signal";
    s.config.horizonTicks = 4;
    s.mutexCount = 0;
    s.semaphores[0].maxCount = 1;
    s.tasks[0].name = "C";
    s.tasks[0].basePriority = 3;
    s.tasks[0].scriptLength = 3;
    s.tasks[0].script[0] = {OpCode::Wait, 0, 2};
    s.tasks[0].script[1] = {OpCode::Run, 1, {}};
    s.tasks[0].script[2] = {};
    s.tasks[1].name = "P";
    s.tasks[1].basePriority = 1;
    s.tasks[1].scriptLength = 4;
    s.tasks[1].script[0] = {OpCode::Run, 2, {}};
    s.tasks[1].script[1] = {OpCode::Signal, 0, 2};
    s.tasks[1].script[2] = {OpCode::Run, 1, {}};
    s.tasks[1].script[3] = {};
    Fixture f(s);
    const JobId expected[] = {2, 2, 1, 2};
    Job* previous = nullptr;

    auto stepZeroTime = [&](Job& job, Tick tick) {
        const TaskSpec& task = *f.registry.findTask(job.taskId);
        const Instruction& op = task.script[job.scriptIndex];
        if (op.op == OpCode::Signal) {
            f.resources.signal(job.jobId, *op.resourceId);
        } else if (op.op == OpCode::Wait) {
            f.resources.wait(job.jobId, *op.resourceId);
        } else if (op.op == OpCode::End) {
            f.resources.validateMayComplete(job.jobId);
            f.scheduler.remove(job);
            job.state = JobState::Completed;
            job.completionTick = tick;
        } else {
            CHECK(false);
        }
        ++job.scriptIndex; // A granted wait resumes AFTER the resource instruction.
    };

    for (Tick tick = 0; tick <= 4; ++tick) {
        // Settle the previous burst and its immediate SIGNAL/END before dispatch.
        if (previous && previous->cpuBurstRemaining == 0) {
            ++previous->scriptIndex;
            while (previous->state == JobState::Running) {
                const auto op = f.registry.findTask(previous->taskId)->script[previous->scriptIndex].op;
                if (op != OpCode::Signal && op != OpCode::End) break;
                stepZeroTime(*previous, tick);
            }
        }
        Job* selected = nullptr;
        for (int steps = 0; steps < 16; ++steps) {
            selected = f.scheduler.dispatch();
            if (!selected) break;
            const Instruction& op = f.registry.findTask(selected->taskId)->script[selected->scriptIndex];
            if (op.op == OpCode::Run) {
                if (selected->cpuBurstRemaining == 0) selected->cpuBurstRemaining = op.durationTicks;
                break;
            }
            stepZeroTime(*selected, tick);
            CHECK(steps < 15);
        }
        CHECK(f.resources.findSemaphore(2)->count == 0);
        if (tick < 4) {
            CHECK(selected && selected->jobId == expected[tick]);
            --selected->cpuBurstRemaining;
            ++selected->totalCpuUsed;
        } else {
            CHECK(!selected);
        }
        previous = selected;
    }
    CHECK(f.job(0).completionTick == 3 && f.job(0).totalCpuUsed == 1);
    CHECK(f.job(1).completionTick == 4 && f.job(1).totalCpuUsed == 3);
    CHECK(f.scheduler.blocks == 1 && f.scheduler.wakes == 1);
    CHECK(f.scheduler.bitmap.isEmpty());
    std::cout << "Producer/consumer oracle: P [0,2), C [2,3), P [3,4).\n";
}

static void checkReadyInvariant(Registry& registry, TestScheduler& scheduler) {
    std::size_t members = 0;
    int running = 0;
    for (int p = 0; p < 32; ++p) {
        const auto& ring = scheduler.ready[p];
        CHECK(scheduler.bitmap.test(p) == !ring.isEmpty());
        if (ring.isEmpty()) continue;
        auto* node = ring.front()->readyNode;
        for (std::size_t i = 0; i < ring.size(); ++i) {
            Job& job = *node->getValue();
            CHECK(job.readyNode == node && job.effectivePriority == p);
            CHECK(job.state == JobState::Ready || job.state == JobState::Running);
            CHECK(!job.suspended && job.blockReason == BlockReason::None);
            ++members;
            node = node->getNext();
        }
    }
    std::size_t runnable = 0;
    for (std::size_t i = 1; i <= registry.jobCount(); ++i) {
        const Job& job = *registry.findJob(static_cast<JobId>(i));
        if (job.state == JobState::Ready || job.state == JobState::Running) {
            CHECK(job.readyNode);
            ++runnable;
        } else CHECK(!job.readyNode);
        if (job.state == JobState::Running) ++running;
    }
    CHECK(members == runnable && running <= 1);
}

static void testDonationAndRestoration() {
    Scenario s = scenario();
    s.config.inheritance = true;
    Fixture f(s);
    f.run(0);
    f.resources.lock(1, 0);
    f.job(0).quantumRemaining = 7; // Donation must not reset a partially used slice.
    f.run(1);
    f.resources.lock(2, 0);
    CHECK(f.job(0).effectivePriority == 3 && f.job(0).quantumRemaining == 7);
    CHECK(f.scheduler.ready[1].isEmpty() && f.scheduler.ready[3].front() == &f.job(0));
    CHECK(f.scheduler.changeCount == 1);
    checkReadyInvariant(f.registry, f.scheduler);
    f.run(2);
    f.resources.lock(3, 0);
    CHECK(f.job(0).effectivePriority == 5 && f.scheduler.changeCount == 2);
    f.run(3);
    f.resources.lock(4, 0);
    f.run(4);
    f.resources.lock(5, 0);
    CHECK(f.scheduler.changeCount == 2); // Equal/lower donors create no redundant event.
    const JobId order[] = {3, 4, 2, 5};
    checkOrder(f.resources.findMutex(0)->waiters, order, 4);
    f.run(0);
    CHECK(f.resources.unlock(1, 0) == 3);
    CHECK(f.job(0).state == JobState::Running && f.scheduler.current == &f.job(0));
    CHECK(f.job(0).effectivePriority == 1 && f.job(0).quantumRemaining == 7);
    CHECK(f.scheduler.changes[2].event.reason == PriorityChangeReason::MutexRelease);
    CHECK(f.scheduler.changeCount == 3);
    checkReadyInvariant(f.registry, f.scheduler);

    // A lower-priority waiter must never lower an owner's base priority.
    Fixture high(s);
    high.run(2);
    high.resources.lock(3, 0);
    high.run(0);
    high.resources.lock(1, 0);
    CHECK(high.job(2).effectivePriority == 5 && high.scheduler.changeCount == 0);

    // Semaphores have no owner or donation, even with inheritance enabled.
    Fixture sem(s);
    sem.run(2);
    sem.resources.wait(3, 2);
    CHECK(sem.scheduler.changeCount == 0 && sem.job(0).effectivePriority == 1);
}

static void testSuspendedDonorHandoff() {
    Scenario s = scenario(3);
    s.config.inheritance = true;
    Fixture f(s);
    f.run(0);
    f.resources.lock(1, 0);
    f.run(2);
    f.resources.lock(3, 0); // Priority 5 waiter remains a donor while suspended.
    f.resources.validateMaySuspend(3);
    f.job(2).suspended = true;
    f.job(2).state = JobState::Suspended;
    f.run(1);
    f.resources.lock(2, 0);
    f.run(0);
    CHECK(f.resources.unlock(1, 0) == 2);
    CHECK(f.job(1).effectivePriority == 5 && f.job(1).heldMutexId == 0);
    CHECK(f.job(1).readyNode && !f.job(2).heldMutexId);
    checkReadyInvariant(f.registry, f.scheduler);
    f.job(2).suspended = false;
    f.job(2).state = JobState::Blocked;
    CHECK(!f.resources.resumeMutexWait(3)); // Wait persists while another job owns R.
    f.run(1);
    CHECK(f.resources.unlock(2, 0) == 3);
    CHECK(f.job(1).effectivePriority == 3 && f.job(2).effectivePriority == 5);
    checkReadyInvariant(f.registry, f.scheduler);

    // A new caller can acquire an otherwise free mutex with suspended waiters.
    Fixture idle(s);
    idle.run(0);
    idle.resources.lock(1, 0);
    idle.run(2);
    idle.resources.lock(3, 0);
    idle.resources.validateMaySuspend(3);
    idle.job(2).suspended = true;
    idle.job(2).state = JobState::Suspended;
    idle.run(0);
    CHECK(!idle.resources.unlock(1, 0));
    idle.run(1);
    CHECK(idle.resources.lock(2, 0) == AcquireResult::Acquired);
    CHECK(idle.job(1).effectivePriority == 5);
    checkReadyInvariant(idle.registry, idle.scheduler);
}

static void testRuntimeTransitionGuards() {
    Fixture f(scenario(2));
    rejects<std::invalid_argument>([&] { f.resources.validateMaySuspend(999); });
    rejects<std::invalid_argument>([&] { f.resources.validateMaySleep(999); });
    rejects<std::invalid_argument>([&] { f.resources.validateMayComplete(999); });
    rejects<std::logic_error>([&] { f.resources.validateMaySleep(1); });
    f.run(0);
    f.resources.validateMaySleep(1);
    f.resources.validateMayComplete(1);
    f.resources.validateMaySuspend(1);
    f.resources.lock(1, 0);
    rejects<std::logic_error>([&] { f.resources.validateMaySleep(1); });
    rejects<std::logic_error>([&] { f.resources.validateMaySuspend(1); });
    rejects<std::logic_error>([&] { f.resources.validateMayComplete(1); });
    f.resources.unlock(1, 0);
    f.resources.wait(1, 2);
    rejects<std::logic_error>([&] { f.resources.validateMaySleep(1); });
    rejects<std::logic_error>([&] { f.resources.validateMayComplete(1); });
    f.resources.validateMaySuspend(1); // Waiting without ownership may be suspended.
    f.job(0).state = JobState::Suspended;
    f.job(0).suspended = true;
    rejects<std::logic_error>([&] { f.resources.validateMaySuspend(1); });
    f.run(1);
    f.resources.validateMayComplete(2);
    f.scheduler.remove(f.job(1));
    f.job(1).state = JobState::Completed;
    rejects<std::logic_error>([&] { f.resources.validateMayComplete(2); });
    rejects<std::logic_error>([&] { f.resources.validateMaySuspend(2); });
}

static void testWaitResumeContracts() {
    Fixture f(scenario(3));
    // Resume an unresolved semaphore wait: it must remain blocked, not re-WAIT.
    f.run(1);
    f.resources.wait(2, 2);
    auto* saved = f.job(1).waitNode;
    f.resources.validateMaySuspend(2);
    f.job(1).suspended = true;
    f.job(1).state = JobState::Suspended;
    f.job(1).suspended = false;
    f.job(1).state = JobState::Blocked;
    CHECK(f.job(1).waitNode == saved && !f.job(1).readyNode);
    f.run(0);
    CHECK(f.resources.signal(1, 2) == 2);
    CHECK(f.job(1).readyNode && f.scheduler.wakes == 1);

    // Guard coverage for a timer-blocked record, not a fake timer backend test.
    Job& sleeper = f.job(2);
    f.run(2);
    f.resources.validateMaySleep(3);
    f.scheduler.remove(sleeper);
    sleeper.state = JobState::Blocked;
    sleeper.blockReason = BlockReason::Timer;
    sleeper.timerId = 7;
    sleeper.wakeTick = 5;
    f.resources.validateMaySuspend(3);
    CHECK(sleeper.timerId == 7 && sleeper.wakeTick == 5);
    rejects<std::logic_error>([&] { f.resources.validateMayComplete(3); });
    rejects<std::logic_error>([&] { f.resources.validateMaySleep(3); });
    sleeper.suspended = true;
    sleeper.state = JobState::Suspended;
    rejects<std::logic_error>([&] { f.resources.validateMayComplete(3); });
    checkReadyInvariant(f.registry, f.scheduler);
}

static void testPriorityHookFailures() {
    struct LegacyHooks : ResourceSchedulerHooks {
        void onBlocked(Job&) override {}
        void onReady(Job&) override {}
    } legacy;
    Job job;
    const ResourcePriorityChange change{1, 0, 1, 3, PriorityChangeReason::Inheritance};
    rejects<std::logic_error>([&] { legacy.onPriorityChanged(job, change); });
    class FailingPriorityScheduler : public TestScheduler {
    public:
        ResourceManager* manager = nullptr;
        bool reenter = false;
        void onPriorityChanged(Job& job, const ResourcePriorityChange&) override {
            if (reenter) manager->signal(job.jobId, 2);
            throw std::runtime_error("priority hook failure");
        }
    };
    for (bool reenter : {false, true}) {
        Scenario s = scenario(2);
        s.config.inheritance = true;
        Registry registry(s);
        FailingPriorityScheduler scheduler;
        ResourceManager resources(registry, scheduler);
        scheduler.manager = &resources;
        scheduler.reenter = reenter;
        Job& owner = registry.releaseJob(0, 0);
        Job& waiter = registry.releaseJob(1, 0);
        scheduler.add(owner);
        scheduler.add(waiter);
        scheduler.run(owner);
        resources.lock(1, 0);
        scheduler.run(waiter);
        if (reenter) rejects<std::logic_error>([&] { resources.lock(2, 0); });
        else rejects<std::runtime_error>([&] { resources.lock(2, 0); });
        CHECK(waiter.state == JobState::Blocked && waiter.waitNode);
        CHECK(resources.findSemaphore(2)->count == 0);
        rejects<std::logic_error>([&] { resources.unlock(1, 0); });
        rejects<std::logic_error>([&] { resources.validateMaySuspend(2); });
    }
}

static void testInversionOracle(bool inheritance) {
    Scenario s = scenario(3);
    s.config.scenarioId = "priority_inversion";
    s.config.inheritance = inheritance;
    s.mutexCount = 1;
    s.semaphoreCount = 0;
    const int priorities[] = {1, 3, 2};
    for (int i = 0; i < 3; ++i) {
        TaskSpec& task = s.tasks[i];
        task.firstRelease = i;
        task.basePriority = priorities[i];
        task.relativeDeadline = i == 1 ? 5 : 20; // H releases at 1: absolute deadline 6.
        if (i < 2) {
            task.scriptLength = 4;
            task.script[0] = {OpCode::Lock, 0, 0};
            task.script[1] = {OpCode::Run, i == 0 ? 4 : 1, {}};
            task.script[2] = {OpCode::Unlock, 0, 0};
            task.script[3] = {};
        } else task.script[0].durationTicks = 4;
    }
    const JobId off[] = {1, 1, 3, 3, 3, 3, 1, 1, 2, 0};
    const JobId on[] = {1, 1, 1, 1, 2, 3, 3, 3, 3, 0};
    // A fresh registry and manager must reproduce every decision on a second run.
    for (int repeat = 0; repeat < 2; ++repeat) {
        Registry registry(s);
        TestScheduler scheduler;
        ResourceManager resources(registry, scheduler);
        Job* previous = nullptr;
        JobId actual[10]{};
        int misses = 0;
        auto step = [&](Job& job, Tick tick) {
            const Instruction& op = registry.findTask(job.taskId)->script[job.scriptIndex];
            if (op.op == OpCode::Lock) resources.lock(job.jobId, *op.resourceId);
            else if (op.op == OpCode::Unlock) resources.unlock(job.jobId, *op.resourceId);
            else if (op.op == OpCode::End) {
                resources.validateMayComplete(job.jobId);
                scheduler.remove(job);
                job.state = JobState::Completed;
                job.completionTick = tick;
            } else CHECK(false);
            ++job.scriptIndex;
        };
        for (Tick t = 0; t <= 10; ++t) {
            scheduler.now = t;
            if (previous && previous->cpuBurstRemaining == 0) {
                ++previous->scriptIndex;
                while (previous->state == JobState::Running) {
                    const auto op = registry.findTask(previous->taskId)->script[previous->scriptIndex].op;
                    if (op != OpCode::Unlock && op != OpCode::End) break;
                    step(*previous, t);
                }
            }
            for (std::size_t i = 0; i < s.taskCount; ++i)
                if (s.tasks[i].firstRelease == t) scheduler.add(registry.releaseJob(s.tasks[i].taskId, t));
            Job* selected = nullptr;
            for (int steps = 0; steps < 16; ++steps) {
                selected = scheduler.dispatch();
                if (!selected) break;
                const auto& op = registry.findTask(selected->taskId)->script[selected->scriptIndex];
                if (op.op == OpCode::Run) {
                    if (!selected->cpuBurstRemaining) selected->cpuBurstRemaining = op.durationTicks;
                    break;
                }
                step(*selected, t);
                CHECK(steps < 15);
            }
            for (std::size_t i = 1; i <= registry.jobCount(); ++i) {
                Job& job = *registry.findJob(static_cast<JobId>(i));
                if (job.state != JobState::Completed && job.absoluteDeadline <= t && !job.deadlineMissRecorded) {
                    job.deadlineMissRecorded = true;
                    job.deadlineMissTick = t;
                    ++misses;
                }
            }
            checkReadyInvariant(registry, scheduler);
            if (t < 10) {
                actual[t] = selected ? selected->jobId : 0;
                CHECK(actual[t] == (inheritance ? on[t] : off[t]));
                if (selected) {
                    if (!selected->firstStartTick) selected->firstStartTick = t;
                    --selected->cpuBurstRemaining;
                    ++selected->totalCpuUsed;
                }
            } else CHECK(!selected);
            previous = selected;
        }
        const Job& high = *registry.findJob(2);
        CHECK(high.completionTick == (inheritance ? 5 : 9));
        CHECK(high.deadlineMissRecorded == !inheritance && misses == (inheritance ? 0 : 1));
        CHECK(high.deadlineMissTick == (inheritance ? std::optional<Tick>{} : std::optional<Tick>{6}));
        CHECK(registry.findJob(1)->totalCpuUsed == 4 && high.totalCpuUsed == 1);
        CHECK(registry.findJob(3)->totalCpuUsed == 4);
        CHECK(scheduler.changeCount == (inheritance ? 2U : 0U));
        if (inheritance) {
            CHECK(scheduler.changes[0].tick == 1 && scheduler.changes[0].event.from == 1);
            CHECK(scheduler.changes[0].event.to == 3 && scheduler.changes[0].event.jobId == 1);
            CHECK(scheduler.changes[1].tick == 4 && scheduler.changes[1].event.from == 3);
            CHECK(scheduler.changes[1].event.to == 1 && scheduler.changes[1].event.jobId == 1);
        }
        for (JobId i = 1; i <= 3; ++i) {
            const Job& job = *registry.findJob(i);
            CHECK(!job.readyNode && !job.waitNode && !job.heldMutexId);
            CHECK(job.effectivePriority == registry.findTask(job.taskId)->basePriority);
        }
        CHECK(resources.findMutex(0)->waiters.isEmpty() && !resources.findMutex(0)->ownerJobId);
        if (repeat == 0) {
            const char* names[] = {"idle", "L", "H", "M"};
            std::cout << "Test-adapter inversion " << (inheritance ? "ON: " : "OFF: ");
            for (int start = 0; start < 10;) {
                int end = start + 1;
                while (end < 10 && actual[end] == actual[start]) ++end;
                std::cout << names[actual[start]] << " [" << start << ',' << end << ") ";
                start = end;
            }
            std::cout << "H completes=" << *high.completionTick << ", misses=" << misses << '\n';
            for (std::size_t i = 0; i < scheduler.changeCount; ++i)
                std::cout << "  priority job=" << scheduler.changes[i].event.jobId
                          << " at t=" << scheduler.changes[i].tick << ": "
                          << scheduler.changes[i].event.from << " -> " << scheduler.changes[i].event.to << '\n';
        }
    }
}

int main() {
    testMutexOwnershipAndOrder();
    testSemaphorePermitsAndOrder();
    testNoStolenPermit();
    testSuspendedWaitsAndCleanup();
    testUnsupportedModeAndCaller();
    testHookFailureAbortsRun();
    testProducerConsumerOracle();
    testDonationAndRestoration();
    testSuspendedDonorHandoff();
    testRuntimeTransitionGuards();
    testWaitResumeContracts();
    testPriorityHookFailures();
    testInversionOracle(false);
    testInversionOracle(true);
    std::cout << "All resource manager tests passed.\n";
}
