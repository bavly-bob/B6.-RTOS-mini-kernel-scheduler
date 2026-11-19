// TimerManager tests. Every behavioral test runs on BOTH required backends and
// the results are compared. Checks stay active with NDEBUG.
#include "timer_manager.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

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

static const TimerBackend kBackends[] = {TimerBackend::SortedList, TimerBackend::Heap};

// One-shot tasks released at `release`, each with script SLEEP 1; END
// (the script only has to be valid; tests call sleep() with chosen durations).
static Scenario sleepers(int count, Tick release, Tick horizon) {
    Scenario s;
    s.config.scenarioId = "timer_test";
    s.config.horizonTicks = horizon;
    s.config.quantumTicks = 2;
    s.taskCount = static_cast<std::size_t>(count);
    for (int i = 0; i < count; ++i) {
        TaskSpec& t = s.tasks[i];
        t.taskId = i;
        t.name = std::string(1, static_cast<char>('A' + i));
        t.basePriority = 1;
        t.firstRelease = release;
        t.relativeDeadline = 1000;
        t.scriptLength = 2;
        t.script[0] = {OpCode::Sleep, 1, {}};
        t.script[1] = {};
    }
    return s;
}

// What the scheduler does before executing SLEEP: the job is the running one.
static Job& running(Job& job) {
    job.state = JobState::Running;
    return job;
}

static int wokenJob(const DynamicArray& woken, int i) {
    return static_cast<Job*>(woken.at(i))->jobId;
}

// Plan p.12 / golden trace C: from tick 100, A sleeps 30, B 10, C 20, plus an
// equal-time sleeper D (10). Expected B@110, D@110 (insertion order), C@120, A@130.
static void testWakeOrderOracle() {
    for (TimerBackend backend : kBackends) {
        Registry registry(sleepers(4, 100, 200));
        TimerManager timers(registry, backend);
        DynamicArray woken(static_cast<int>(kMaxJobs));
        Job& a = running(registry.releaseJob(0, 100));
        Job& b = running(registry.releaseJob(1, 100));
        Job& c = running(registry.releaseJob(2, 100));
        Job& d = running(registry.releaseJob(3, 100));
        CHECK(!timers.nextWake());
        CHECK(timers.expireDue(100, woken) == 0);  // Empty structure.
        timers.sleep(a.jobId, 100, 30);
        timers.sleep(b.jobId, 100, 10);
        timers.sleep(c.jobId, 100, 20);
        timers.sleep(d.jobId, 100, 10);
        CHECK(timers.pendingCount() == 4);
        CHECK(*timers.nextWake() == 110);
        CHECK(a.state == JobState::Blocked && a.blockReason == BlockReason::Timer);
        CHECK(a.wakeTick && *a.wakeTick == 130 && a.timerId);

        int order[4];
        Tick when[4];
        int seen = 0;
        for (Tick t = 101; t <= 140; ++t) {
            std::size_t n = timers.expireDue(t, woken);
            CHECK(n == static_cast<std::size_t>(woken.size()));
            for (std::size_t i = 0; i < n; ++i) {
                order[seen] = wokenJob(woken, static_cast<int>(i));
                when[seen] = t;
                ++seen;
            }
        }
        CHECK(seen == 4);
        CHECK(order[0] == b.jobId && when[0] == 110);
        CHECK(order[1] == d.jobId && when[1] == 110);
        CHECK(order[2] == c.jobId && when[2] == 120);
        CHECK(order[3] == a.jobId && when[3] == 130);
        // A woken job is READY with its wait cleared and a fresh quantum.
        CHECK(a.state == JobState::Ready && a.blockReason == BlockReason::None);
        CHECK(!a.wakeTick && !a.timerId && a.quantumRemaining == 2);
        CHECK(!timers.nextWake() && timers.pendingCount() == 0);
    }
    std::cout << "PASS: wake-order oracle (B, D @110, C @120, A @130) on both backends\n";
}

// Skipping ticks: all due timers come out in one batch, in wake order.
static void testBatchExpiry() {
    for (TimerBackend backend : kBackends) {
        Registry registry(sleepers(4, 0, 100));
        TimerManager timers(registry, backend);
        DynamicArray woken(static_cast<int>(kMaxJobs));
        Job* jobs[4];
        const Tick sleeps[4] = {30, 10, 20, 10};
        for (int i = 0; i < 4; ++i) {
            jobs[i] = &running(registry.releaseJob(i, 0));
            timers.sleep(jobs[i]->jobId, 0, sleeps[i]);
        }
        CHECK(timers.expireDue(25, woken) == 3);
        CHECK(wokenJob(woken, 0) == jobs[1]->jobId);
        CHECK(wokenJob(woken, 1) == jobs[3]->jobId);
        CHECK(wokenJob(woken, 2) == jobs[2]->jobId);
        CHECK(timers.expireDue(29, woken) == 0 && woken.size() == 0);  // Cleared.
        CHECK(timers.expireDue(30, woken) == 1 && wokenJob(woken, 0) == jobs[0]->jobId);
    }
    std::cout << "PASS: several due timers in one batch, in order\n";
}

static void testCancellation() {
    for (TimerBackend backend : kBackends) {
        Registry registry(sleepers(3, 0, 100));
        TimerManager timers(registry, backend);
        DynamicArray woken(static_cast<int>(kMaxJobs));
        Job& a = running(registry.releaseJob(0, 0));
        Job& b = running(registry.releaseJob(1, 0));
        Job& c = running(registry.releaseJob(2, 0));
        timers.sleep(a.jobId, 0, 5);
        TimerId middle = timers.sleep(b.jobId, 0, 7);
        timers.sleep(c.jobId, 0, 9);
        CHECK(timers.cancel(middle));        // Middle of the order.
        CHECK(!timers.cancel(middle));       // Already gone.
        CHECK(!timers.cancel(9999));         // Never issued.
        CHECK(!b.timerId && !b.wakeTick);    // Job's timer metadata cleared.
        CHECK(timers.pendingCount() == 2);
        CHECK(timers.expireDue(20, woken) == 2);
        CHECK(wokenJob(woken, 0) == a.jobId && wokenJob(woken, 1) == c.jobId);
        // B was never woken by its cancelled timer.
        CHECK(b.state == JobState::Blocked);
    }
    std::cout << "PASS: cancellation removes exactly one timer\n";
}

// Golden trace C, suspension part: sleep at 0 until 5; suspended at 2; expiry
// at 5 resolves the wait but the job stays SUSPENDED; resume at 7 -> READY.
static void testSuspendedSleeper() {
    for (TimerBackend backend : kBackends) {
        Registry registry(sleepers(1, 0, 20));
        TimerManager timers(registry, backend);
        DynamicArray woken(static_cast<int>(kMaxJobs));
        Job& job = running(registry.releaseJob(0, 0));
        timers.sleep(job.jobId, 0, 5);
        for (Tick t = 1; t < 5; ++t) {
            if (t == 2) {  // Scheduler's SUSPEND control keeps the wait metadata.
                job.suspended = true;
                job.state = JobState::Suspended;
            }
            CHECK(timers.expireDue(t, woken) == 0);
        }
        CHECK(timers.expireDue(5, woken) == 1 && wokenJob(woken, 0) == job.jobId);
        CHECK(job.state == JobState::Suspended && job.suspended);
        CHECK(job.blockReason == BlockReason::None && !job.timerId && !job.wakeTick);
        // The scheduler's RESUME at 7 finds no unresolved wait, so READY.
        CHECK(timers.expireDue(7, woken) == 0);
        job.suspended = false;
        job.state = job.blockReason == BlockReason::None ? JobState::Ready : JobState::Blocked;
        CHECK(job.state == JobState::Ready);
    }
    std::cout << "PASS: suspended sleeper keeps suspension after expiry\n";
}

static void testRejectedCalls() {
    Registry registry(sleepers(2, 0, 100));
    TimerManager timers(registry, TimerBackend::Heap);
    DynamicArray woken(static_cast<int>(kMaxJobs));
    Job& job = registry.releaseJob(0, 0);
    rejects<std::logic_error>([&] { timers.sleep(job.jobId, 0, 3); });   // READY, not RUNNING.
    running(job);
    rejects<std::invalid_argument>([&] { timers.sleep(job.jobId, 0, 0); });
    rejects<std::invalid_argument>([&] { timers.sleep(job.jobId, 0, -4); });
    rejects<std::invalid_argument>([&] { timers.sleep(999, 0, 3); });
    job.heldMutexId = 0;
    rejects<std::logic_error>([&] { timers.sleep(job.jobId, 0, 3); });
    job.heldMutexId.reset();
    job.suspended = true;
    rejects<std::logic_error>([&] { timers.sleep(job.jobId, 0, 3); });
    job.suspended = false;
    CHECK(timers.pendingCount() == 0);  // Rejected calls queued nothing.

    timers.sleep(job.jobId, 10, 3);
    rejects<std::logic_error>([&] { timers.expireDue(9, woken); });     // Time went back.
    job.state = JobState::Running;  // Corrupt state: already has a timer.
    rejects<std::logic_error>([&] { timers.sleep(job.jobId, 10, 3); });
    job.state = JobState::Blocked;
    // A completed job must never be woken: the stale-timer check refuses it.
    job.state = JobState::Completed;
    rejects<std::logic_error>([&] { timers.expireDue(13, woken); });
    std::cout << "PASS: invalid sleeps and broken invariants are rejected\n";
}

static void testReset() {
    for (TimerBackend backend : kBackends) {
        Registry registry(sleepers(2, 0, 100));
        TimerManager timers(registry, backend);
        Job& a = running(registry.releaseJob(0, 0));
        Job& b = running(registry.releaseJob(1, 0));
        timers.sleep(a.jobId, 0, 4);
        timers.sleep(b.jobId, 0, 8);
        timers.reset();
        CHECK(timers.pendingCount() == 0 && !timers.nextWake());
        CHECK(!a.timerId && !a.wakeTick && !b.timerId && !b.wakeTick);
    }
    std::cout << "PASS: reset clears every timer and job reference\n";
}

// 512 jobs (the full job limit) with pseudo-random sleeps and cancellations.
// Both backends must report identical (tick, job) wake sequences, and every
// job must wake exactly once, at exactly its wake tick.
static void testBackendEquivalence() {
    int wakeTickOf[2][kMaxJobs + 1];
    int wakeJobs[2][kMaxJobs];
    Tick wakeTicks[2][kMaxJobs];
    int wakeCount[2] = {0, 0};
    for (int b = 0; b < 2; ++b) {
        Scenario s;
        s.config.scenarioId = "timer_equivalence";
        s.config.horizonTicks = 64;
        s.taskCount = 32;
        for (int i = 0; i < 32; ++i) {
            TaskSpec& t = s.tasks[i];
            t.taskId = i;
            t.name = "T" + std::to_string(i);
            t.periodTicks = 4;  // 16 releases each -> 512 jobs.
            t.relativeDeadline = 4;
            t.scriptLength = 2;
            t.script[0] = {OpCode::Sleep, 1, {}};
            t.script[1] = {};
        }
        Registry registry(s);
        TimerManager timers(registry, kBackends[b]);
        DynamicArray woken(static_cast<int>(kMaxJobs));
        unsigned seed = 2026;
        auto next = [&seed]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7fff; };
        for (int j = 0; j <= static_cast<int>(kMaxJobs); ++j) wakeTickOf[b][j] = -1;
        int cancelled = 0;
        for (Tick t = 0; t <= 200; ++t) {
            std::size_t n = timers.expireDue(t, woken);
            for (std::size_t i = 0; i < n; ++i) {
                Job* job = static_cast<Job*>(woken.at(static_cast<int>(i)));
                CHECK(wakeTickOf[b][job->jobId] == -1);  // Exactly once.
                wakeTickOf[b][job->jobId] = static_cast<int>(t);
                wakeJobs[b][wakeCount[b]] = job->jobId;
                wakeTicks[b][wakeCount[b]] = t;
                ++wakeCount[b];
            }
            if (t < 64 && t % 4 == 0) {
                for (int task = 0; task < 32; ++task) {
                    Job& job = running(registry.releaseJob(task, t));
                    Tick duration = 1 + static_cast<Tick>(next() % 40);
                    TimerId id = timers.sleep(job.jobId, t, duration);
                    CHECK(*job.wakeTick == t + duration);
                    if (next() % 16 == 0) {  // Same random stream on both backends.
                        CHECK(timers.cancel(id));
                        ++cancelled;
                    }
                }
            }
        }
        CHECK(timers.pendingCount() == 0);
        CHECK(registry.jobCount() == kMaxJobs);
        CHECK(wakeCount[b] + cancelled == static_cast<int>(kMaxJobs));
    }
    CHECK(wakeCount[0] == wakeCount[1]);
    for (int i = 0; i < wakeCount[0]; ++i) {
        CHECK(wakeJobs[0][i] == wakeJobs[1][i]);
        CHECK(wakeTicks[0][i] == wakeTicks[1][i]);
    }
    std::cout << "PASS: 512 jobs, identical wake sequences on both backends ("
              << wakeCount[0] << " wakes)\n";
}

static void testBackendSelection() {
    Scenario s = sleepers(1, 0, 10);
    s.config.timerBackend = TimerBackend::Heap;
    Registry registry(s);
    TimerManager fromConfig(registry);
    CHECK(fromConfig.backend() == TimerBackend::Heap);
    CHECK(std::string(fromConfig.backendName()) == "heap");
    TimerManager explicitList(registry, TimerBackend::SortedList);
    CHECK(std::string(explicitList.backendName()) == "sorted_list");
    std::cout << "PASS: backend chosen from RunConfig or explicitly\n";
}

int main() {
    testWakeOrderOracle();
    testBatchExpiry();
    testCancellation();
    testSuspendedSleeper();
    testRejectedCalls();
    testReset();
    testBackendEquivalence();
    testBackendSelection();
    std::cout << "All timer manager tests passed.\n";
    return 0;
}
