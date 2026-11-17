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
    Scenario s = scenario(1);
    s.config.inheritance = true;
    Registry registry(s);
    TestScheduler scheduler;
    rejects<std::invalid_argument>([&] { ResourceManager resources(registry, scheduler); });
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

int main() {
    testMutexOwnershipAndOrder();
    testSemaphorePermitsAndOrder();
    testNoStolenPermit();
    testSuspendedWaitsAndCleanup();
    testUnsupportedModeAndCaller();
    testHookFailureAbortsRun();
    testProducerConsumerOracle();
    std::cout << "All resource manager tests passed.\n";
}
