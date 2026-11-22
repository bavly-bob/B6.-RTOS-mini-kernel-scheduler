// Tests for BoundedArray, RunTrace and the schemaVersion 1 exporter.
// Usage: test_run_export [output-directory]
// With a directory, it also writes two run files that
// tests/validate_export.js checks with the dashboard's own validator.
#include "run_exporter.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
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

static bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

static TaskSpec task(TaskId id, const std::string& name, int priority, Tick release, Tick cpu) {
    TaskSpec t;
    t.taskId = id;
    t.name = name;
    t.basePriority = priority;
    t.firstRelease = release;
    t.relativeDeadline = 10;
    t.scriptLength = 2;
    t.script[0] = {OpCode::Run, cpu, {}};
    t.script[1] = {};
    return t;
}

// ---- BoundedArray -------------------------------------------------------------

static void testBoundedArray() {
    BoundedArray<int> array(3);
    CHECK(array.isEmpty() && array.capacity() == 3);
    rejects<std::out_of_range>([&] { array.back(); });
    CHECK(array.push(1) && array.push(2) && array.push(3));
    CHECK(array.isFull() && !array.push(4));  // Full: refused, not grown.
    CHECK(array.size() == 3 && array.at(0) == 1 && array.back() == 3);
    rejects<std::out_of_range>([&] { array.at(3); });
    array.clear();
    CHECK(array.isEmpty() && array.push(9) && array.at(0) == 9);
    BoundedArray<int> none(0);
    CHECK(!none.push(1));
    std::cout << "PASS: BoundedArray never grows and checks bounds\n";
}

// ---- RunTrace ordering, merging and limits -------------------------------------

static void testSegmentsMergeAndSplit() {
    Scenario s;
    s.config.scenarioId = "segments";
    s.config.horizonTicks = 6;
    s.taskCount = 1;
    s.tasks[0] = task(0, "A", 2, 0, 3);
    Registry registry(s);
    Job& a = registry.releaseJob(0, 0);
    RunTrace trace(s.config);
    trace.recordInterval(0, &a, 2);
    trace.recordInterval(1, &a, 2);  // Contiguous and identical: extends.
    a.effectivePriority = 5;         // e.g. inheritance: must split.
    trace.recordInterval(2, &a, 2);
    trace.recordIdle(3);
    trace.recordIdle(4);
    CHECK(trace.segments().size() == 3);
    CHECK(trace.segments().at(0).start == 0 && trace.segments().at(0).end == 2);
    CHECK(trace.segments().at(1).effectivePriority == 5);
    CHECK(!trace.segments().at(2).jobId && trace.segments().at(2).end == 5);
    rejects<std::logic_error>([&] { trace.recordIdle(4); });  // Overlap.
    rejects<std::logic_error>([&] { trace.recordIdle(6); });  // At the horizon.
    rejects<std::logic_error>([&] { trace.recordIdle(-1); });
    std::cout << "PASS: CPU segments merge, split on priority change, never overlap\n";
}

static void testEventOrderAndTruncation() {
    Scenario s;
    s.config.scenarioId = "events";
    s.config.horizonTicks = 4;
    s.config.limits.maxEvents = 2;
    Registry registry(s);
    RunTrace trace(s.config);
    CHECK(trace.addEvent(TraceEvent::at(1, TickPhase::Inputs, EventType::Release, "first_release")));
    rejects<std::logic_error>([&] {
        trace.addEvent(TraceEvent::at(0, TickPhase::Inputs, EventType::Release, "x"));
    });
    rejects<std::logic_error>([&] {
        trace.addEvent(TraceEvent::at(1, TickPhase::Settle, EventType::Complete, "x"));
    });
    CHECK(trace.addEvent(TraceEvent::at(1, TickPhase::Stabilize, EventType::Dispatch, "x")));
    CHECK(trace.events().at(1).seq == 2);
    CHECK(!trace.addEvent(TraceEvent::at(2, TickPhase::Stabilize, EventType::Dispatch, "x")));
    CHECK(trace.eventsTruncated() && trace.isTruncated() && !trace.isValid());
    CHECK(trace.events().size() == 2);  // Nothing silently added after the limit.
    TraceEvent e = TraceEvent::at(0, TickPhase::Settle, EventType::Error, "x");
    e.with("a", 1).flag("b", true).withNull("c").with("d", 4);
    rejects<std::length_error>([&] { e.with("e", 5); });
    std::cout << "PASS: events keep (tick, phase) order and truncate explicitly\n";
}

// No-op scheduler hooks: enough for ResourceManager to record waits.
struct NoHooks : ResourceSchedulerHooks {
    void onBlocked(Job&) override {}
    void onReady(Job&) override {}
};

static void testSnapshotWithResources() {
    Scenario s;
    s.config.scenarioId = "snapshot";
    s.config.horizonTicks = 10;
    s.mutexCount = 1;
    s.mutexes[0].resourceId = 7;
    s.semaphoreCount = 1;
    s.semaphores[0] = {8, 1, 2};
    s.taskCount = 3;
    for (int i = 0; i < 3; ++i) {
        TaskSpec& t = s.tasks[i];
        t = task(i, "T" + std::to_string(i), 1 + i, 0, 1);
        t.scriptLength = 4;
        t.script[0] = {OpCode::Lock, 0, 7};
        t.script[1] = {OpCode::Run, 1, {}};
        t.script[2] = {OpCode::Unlock, 0, 7};
        t.script[3] = {};
    }
    Registry registry(s);
    NoHooks hooks;
    ResourceManager resources(registry, hooks);
    Job& owner = registry.releaseJob(0, 0);
    Job& waiterLow = registry.releaseJob(1, 0);
    Job& waiterHigh = registry.releaseJob(2, 0);
    owner.state = JobState::Running;
    CHECK(resources.lock(owner.jobId, 7) == AcquireResult::Acquired);
    owner.state = JobState::Ready;
    waiterLow.state = JobState::Running;
    CHECK(resources.lock(waiterLow.jobId, 7) == AcquireResult::Blocked);
    waiterHigh.state = JobState::Running;
    CHECK(resources.lock(waiterHigh.jobId, 7) == AcquireResult::Blocked);

    RunTrace trace(s.config);
    const JobId ready[] = {owner.jobId};
    CHECK(trace.captureSnapshot(0, owner.jobId, ready, 1, registry, &resources));
    const TraceSnapshot& snap = trace.snapshots().at(0);
    CHECK(snap.rowCount == 3 && snap.readyCount == 1 && snap.resourceCount == 2);
    const SnapshotResourceRow& mutex = trace.resourceRows().at(snap.firstResource);
    CHECK(mutex.isMutex && mutex.ownerJobId == owner.jobId && mutex.waiterCount == 2);
    // Waiters in the manager's order: higher priority first.
    CHECK(trace.idPool().at(mutex.firstWaiter) == waiterHigh.jobId);
    CHECK(trace.idPool().at(mutex.firstWaiter + 1) == waiterLow.jobId);
    const SnapshotResourceRow& sem = trace.resourceRows().at(snap.firstResource + 1);
    CHECK(!sem.isMutex && sem.count == 1 && sem.maxCount == 2 && sem.waiterCount == 0);
    const SnapshotJobRow& low = trace.jobRows().at(snap.firstRow + 1);
    CHECK(low.state == JobState::Blocked && low.blockReason == BlockReason::Mutex);
    CHECK(low.blockedResourceId == 7);
    CHECK(trace.jobRows().at(snap.firstRow).heldMutexId == 7);
    rejects<std::logic_error>([&] {  // Ticks must strictly increase.
        trace.captureSnapshot(0, std::nullopt, nullptr, 0, registry, &resources);
    });

    std::ostringstream out;
    exportRun(out, {"snap", "snapshot test", RunInfo::Evidence::DesignFixture, {}, {}},
              registry, trace, nullptr);
    const std::string json = out.str();
    CHECK(contains(json, "\"mutexes\":[{\"resourceId\":7,\"ownerJobId\":1,\"waiters\":[3,2]}]"));
    CHECK(contains(json, "\"semaphores\":[{\"resourceId\":8,\"count\":1,\"maxCount\":2,\"waiters\":[]}]"));
    CHECK(contains(json, "\"blockReason\":\"mutex\",\"blockedResourceId\":7"));
    CHECK(contains(json, "\"overall\":{}"));  // Metrics not supplied...
    CHECK(contains(json, "metrics were not computed for this run"));  // ...and said so.
    std::cout << "PASS: snapshots copy job rows, owners and ordered waiters\n";
}

static void testSnapshotRowLimit() {
    Scenario s;
    s.config.scenarioId = "rows";
    s.config.horizonTicks = 5;
    s.config.limits.maxSnapshotJobRows = 3;
    s.taskCount = 2;
    s.tasks[0] = task(0, "A", 1, 0, 1);
    s.tasks[1] = task(1, "B", 1, 0, 1);
    Registry registry(s);
    registry.releaseJob(0, 0);
    registry.releaseJob(1, 0);
    RunTrace trace(s.config);
    CHECK(trace.captureSnapshot(0, std::nullopt, nullptr, 0, registry, nullptr));
    CHECK(!trace.captureSnapshot(1, std::nullopt, nullptr, 0, registry, nullptr));
    CHECK(trace.snapshots().size() == 1 && trace.snapshotsTruncated() && !trace.isValid());
    std::ostringstream out;
    exportRun(out, {"rows", "rows", RunInfo::Evidence::DesignFixture, {}, {}}, registry, trace, nullptr);
    CHECK(contains(out.str(), "\"limitsHit\":[\"maxSnapshotJobRows\"]"));
    CHECK(contains(out.str(), "\"traceTruncated\":true"));
    std::cout << "PASS: snapshot row limit truncates explicitly, no partial snapshot\n";
}

// ---- Golden trace A through the whole recording + export path ------------------
// A, B: priority 5, CPU 3, release 0. H: priority 8, CPU 1, release 2. Quantum 1.
// Expected intervals: A B H A B A B idle; completions H=3, A=6, B=7.
// The schedule is supplied by hand here (no scheduler exists yet), so the run
// is labelled design_fixture. It proves recording and export, not scheduling.
static void testGoldenTraceA(const char* outputDir) {
    Scenario s;
    s.config.scenarioId = "rr_preemption";
    s.config.horizonTicks = 8;
    s.config.quantumTicks = 1;
    s.taskCount = 3;
    s.tasks[0] = task(1, "A", 5, 0, 3);
    s.tasks[1] = task(2, "B", 5, 0, 3);
    s.tasks[2] = task(3, "H", 8, 2, 1);
    Registry registry(s);
    RunTrace trace(s.config);
    const int schedule[8] = {1, 2, 3, 1, 2, 1, 2, 0};  // Job per tick; 0 = idle.
    Job* running = nullptr;

    for (Tick t = 0; t <= 8; ++t) {
        // Phase 1: settle the previous interval.
        if (running && running->totalCpuUsed == registry.findTask(running->taskId)->script[0].durationTicks) {
            running->state = JobState::Completed;
            running->completionTick = t;
            running->cpuBurstRemaining = 0;
            trace.addEvent(TraceEvent::at(t, TickPhase::Settle, EventType::Complete, "script_end")
                               .job(*running));
            running = nullptr;
        }
        // Phase 2: releases in task-ID order.
        for (std::size_t i = 0; i < s.taskCount && t < 8; ++i) {
            if (s.tasks[i].firstRelease != t) continue;
            Job& job = registry.releaseJob(s.tasks[i].taskId, t);
            job.cpuBurstRemaining = s.tasks[i].script[0].durationTicks;
            trace.addEvent(TraceEvent::at(t, TickPhase::Inputs, EventType::Release, "first_release")
                               .job(job).with("absoluteDeadline", job.absoluteDeadline));
        }
        // Phase 3: dispatch the scheduled job.
        Job* next = t < 8 && schedule[t] ? registry.findJob(schedule[t]) : nullptr;
        if (running && running != next && running->state == JobState::Running)
            running->state = JobState::Ready;
        if (next && next != running) {
            next->state = JobState::Running;
            if (!next->firstStartTick) next->firstStartTick = t;
            trace.addEvent(TraceEvent::at(t, TickPhase::Stabilize, EventType::Dispatch, "highest_ready")
                               .job(*next).with("priority", next->effectivePriority));
        }
        running = next;
        // Phase 5: snapshot (ready order = unfinished jobs, most urgent first).
        JobId ready[3];
        std::size_t readyCount = 0;
        if (running) ready[readyCount++] = running->jobId;
        for (JobId id = 1; id <= static_cast<JobId>(registry.jobCount()); ++id) {
            Job* job = registry.findJob(id);
            if (job != running && job->state == JobState::Ready) ready[readyCount++] = id;
        }
        CHECK(trace.captureSnapshot(t, running ? std::optional<JobId>(running->jobId) : std::nullopt,
                                    ready, readyCount, registry, nullptr));
        // Phase 6: execute [t, t+1).
        if (t < 8) {
            if (running) {
                trace.recordInterval(t, running, registry.findTask(running->taskId)->basePriority);
                ++running->totalCpuUsed;
                --running->cpuBurstRemaining;
            } else {
                trace.recordIdle(t);
            }
        }
    }

    RunMetrics metrics;
    metrics.busyTicks = 7;
    metrics.observedTicks = 8;
    metrics.busyPercent = 87.5;
    metrics.completedJobs = 3;
    metrics.rmBoundNote = "not applicable: one-shot jobs";
    const Tick responses[3] = {6, 7, 1};
    metrics.taskCount = 3;
    for (int i = 0; i < 3; ++i) {
        TaskMetrics& m = metrics.perTask[i];
        m.taskId = i + 1;
        m.completedSamples = 1;
        m.responseMin = m.responseMax = responses[i];
        m.responseMean = static_cast<double>(responses[i]);
    }

    RunInfo info{"rr-preemption-export", "Golden trace A recorded through RunTrace",
                 RunInfo::Evidence::DesignFixture, {}, {}};
    std::ostringstream out;
    exportRun(out, info, registry, trace, &metrics);
    const std::string json = out.str();
    CHECK(contains(json, "\"schemaVersion\":1"));
    CHECK(contains(json, "\"evidenceType\":\"design_fixture\""));
    CHECK(contains(json, "\"timerBackend\":\"sorted_list\""));
    CHECK(contains(json, "\"script\":\"RUN 3; END\""));
    CHECK(contains(json, "{\"start\":7,\"end\":8,\"jobId\":null,\"basePriority\":null,\"effectivePriority\":null}"));
    CHECK(contains(json, "\"busyPercent\":87.5"));
    CHECK(contains(json, "\"status\":\"ok\"") && contains(json, "\"error\":null"));
    CHECK(registry.findJob(3)->completionTick == 3);
    CHECK(registry.findJob(1)->completionTick == 6);
    CHECK(registry.findJob(2)->completionTick == 7);
    CHECK(trace.segments().size() == 8);
    if (outputDir) {
        CHECK(exportRunToFile(std::string(outputDir) + "/rr_preemption.export.json", info,
                              registry, trace, &metrics).empty());
    }
    std::cout << "PASS: golden trace A recorded and exported\n";
}

// Hostile names, unfinished jobs and an event limit in one exported file.
static void testEscapingAndNulls(const char* outputDir) {
    Scenario s;
    s.config.scenarioId = "escaping";
    s.config.horizonTicks = 3;
    s.config.limits.maxEvents = 1;
    s.taskCount = 1;
    s.tasks[0] = task(0, "Quote\" back\\slash\nline\ttab <script>&\x01 \xC3\xA9", 1, 0, 5);
    Registry registry(s);
    Job& job = registry.releaseJob(0, 0);
    job.state = JobState::Running;
    job.firstStartTick = 0;
    RunTrace trace(s.config);
    CHECK(trace.addEvent(TraceEvent::at(0, TickPhase::Inputs, EventType::Release, "first_release").job(job)));
    CHECK(!trace.addEvent(TraceEvent::at(0, TickPhase::Stabilize, EventType::Dispatch, "highest_ready").job(job)));
    for (Tick t = 0; t < 3; ++t) {
        trace.recordInterval(t, &job, 1);
        ++job.totalCpuUsed;
    }
    RunInfo info{"escaping", "label with \"quotes\"", RunInfo::Evidence::DesignFixture,
                 std::string("test-rev"), std::string("2026-11-19T12:00:00Z")};
    std::ostringstream out;
    exportRun(out, info, registry, trace, nullptr);
    const std::string json = out.str();
    CHECK(contains(json, "Quote\\\" back\\\\slash\\nline\\ttab \\u003cscript\\u003e\\u0026\\u0001 \xC3\xA9"));
    CHECK(contains(json, "\"completionTick\":null"));
    CHECK(contains(json, "\"engineRevision\":\"test-rev\""));
    CHECK(contains(json, "\"segments\":[\n    {\"start\":0,\"end\":3,\"jobId\":1"));  // Merged.
    CHECK(contains(json, "\"status\":\"invalid\""));
    CHECK(!exportRunToFile("/nonexistent-directory/x.json", info, registry, trace, nullptr).empty());
    if (outputDir) {
        CHECK(exportRunToFile(std::string(outputDir) + "/escaping.export.json", info, registry,
                              trace, nullptr).empty());
    }
    std::cout << "PASS: escaping, null times and invalid status\n";
}

// ---- File size at the run limits ---------------------------------------------
// The dashboard refuses files over 32 MiB (kMaxRunFileBytes). This fills a trace
// to its limits with the widest values each field can hold (4-digit ticks,
// 3-digit job IDs, the longest reason and data keys the engine emits, every
// event carrying four data values, ready lists and 16 resources per snapshot).
static std::size_t g_worstJobs = 0;

static std::size_t worstCaseExport(std::size_t maxSnapshotJobRows, const std::string& path,
                                   std::string& fileError) {
    struct NoHooks : ResourceSchedulerHooks {
        void onBlocked(Job&) override {}
        void onReady(Job&) override {}
    };
    static Scenario s;
    s = Scenario{};
    s.config.scenarioId = "worst_case_size";
    s.config.horizonTicks = kMaxHorizon;
    s.config.limits.maxSnapshotJobRows = maxSnapshotJobRows;
    s.taskCount = kMaxTasks;
    for (std::size_t i = 0; i < kMaxTasks; ++i) {
        TaskSpec& t = s.tasks[i];
        t.taskId = static_cast<TaskId>(i);
        t.name = "task-" + std::to_string(i);
        t.basePriority = static_cast<int>(10 + i % 22);
        t.firstRelease = 0;
        t.periodTicks = i < 20 ? 667 : 0;  // Room for a second release of 20 tasks.
        t.relativeDeadline = 1999;
        t.scriptLength = kMaxScriptLength;
        for (std::size_t k = 0; k + 1 < kMaxScriptLength; ++k) t.script[k] = {OpCode::Run, 1999, {}};
        t.script[kMaxScriptLength - 1] = {};
    }
    s.mutexCount = kMaxResources;
    s.semaphoreCount = kMaxResources;
    for (std::size_t r = 0; r < kMaxResources; ++r) {
        s.mutexes[r] = {static_cast<ResourceId>(r)};
        s.semaphores[r] = {static_cast<ResourceId>(kMaxResources + r), 0, 1000};
    }
    Registry registry(s);
    // As many jobs as fit a snapshot row at every one of the 2,001 boundaries.
    const std::size_t jobs = maxSnapshotJobRows / static_cast<std::size_t>(kMaxHorizon + 1);
    CHECK(jobs > 0 && jobs <= kMaxTasks + 20);
    g_worstJobs = jobs;
    for (std::size_t j = 0; j < jobs; ++j) {
        Job& job = registry.releaseJob(static_cast<TaskId>(j < kMaxTasks ? j : j - kMaxTasks),
                                       j < kMaxTasks ? 0 : 667);
        job.firstStartTick = 1999;
        job.completionTick = 1999;
        job.state = JobState::Suspended;          // Longest state spelling.
        job.blockReason = BlockReason::Semaphore;  // Longest wait spelling.
        job.wakeTick = 1999;
        job.totalCpuUsed = 1999;
        job.cpuBurstRemaining = 1999;
        job.scriptIndex = 63;
    }
    NoHooks hooks;
    ResourceManager resources(registry, hooks);
    RunTrace trace(s.config);
    for (std::size_t e = 0; e < s.config.limits.maxEvents; ++e) {
        const Tick t = static_cast<Tick>(e * static_cast<std::size_t>(kMaxHorizon) / s.config.limits.maxEvents);
        const Job& job = *registry.findJob(static_cast<JobId>(1 + e % jobs));
        CHECK(trace.addEvent(TraceEvent::at(t, TickPhase::Stabilize, EventType::PriorityChange,
                                            "unfinished_at_deadline")
                                 .job(job).resource(15)
                                 .with("absoluteDeadline", 1999).with("grantedJobId", 512)
                                 .with("ownerJobId", 512).flag("stillWaiting", false)));
    }
    static JobId ready[kMaxJobs];
    for (std::size_t j = 0; j < jobs; ++j) ready[j] = static_cast<JobId>(j + 1);
    for (Tick t = 0; t <= kMaxHorizon; ++t) {
        CHECK(trace.captureSnapshot(t, std::optional<JobId>(1), ready + 1, jobs - 1, registry, &resources));
        if (t < kMaxHorizon) trace.recordInterval(t, registry.findJob(static_cast<JobId>(1 + t % jobs)), 31);
    }
    CHECK(trace.isValid() && !trace.isTruncated());  // Inside every limit.
    const RunInfo info{"worst_case_size", "worst case size", RunInfo::Evidence::DesignFixture,
                       std::string("0123456789abcdef0123456789abcdef01234567"),
                       std::string("2026-11-22T12:00:00Z")};
    std::ostringstream out;
    exportRun(out, info, registry, trace, nullptr);
    fileError = exportRunToFile(path, info, registry, trace, nullptr);
    std::remove(path.c_str());
    return out.str().size();
}

static void testFileSizeAtLimits(const char* outputDir) {
    const std::string path = std::string(outputDir ? outputDir : ".") + "/size_check.tmp.json";
    std::string error;

    // 1. Default limits. Whatever the size, the result is never an oversized
    //    file reported as a success: it fits, or export fails with a clear message.
    const std::size_t defaults = RunLimits{}.maxSnapshotJobRows;
    const std::size_t biggest = worstCaseExport(defaults, path, error);
    const std::size_t defaultJobs = g_worstJobs;
    if (biggest > kMaxRunFileBytes) {
        CHECK(contains(error, "above the dashboard's 32 MiB limit"));
    } else {
        CHECK(error.empty());
    }

    // 2. A snapshot-row limit that always fits: 36 jobs at all 2,001 boundaries
    //    (72,036 rows). The docs recommend at most 70,000 to leave a margin for
    //    layouts this test does not build (docs/run-schema-v1.md).
    const std::size_t safeRows = 36 * static_cast<std::size_t>(kMaxHorizon + 1);
    const std::size_t safe = worstCaseExport(safeRows, path, error);
    CHECK(safe <= kMaxRunFileBytes);
    CHECK(error.empty());

    std::cout << "PASS: file size at the limits: default limits (" << defaultJobs * 2001
              << " snapshot rows, 50,000 events) = " << biggest << " bytes ("
              << (biggest > kMaxRunFileBytes ? "over 32 MiB, export refused explicitly" : "fits")
              << "); " << safeRows << " rows = " << safe << " bytes (fits)\n";
}

static void testScriptText() {
    TaskSpec t;
    t.scriptLength = 7;
    t.script[0] = {OpCode::Lock, 0, 1};
    t.script[1] = {OpCode::Run, 4, {}};
    t.script[2] = {OpCode::Unlock, 0, 1};
    t.script[3] = {OpCode::Sleep, 2, {}};
    t.script[4] = {OpCode::Wait, 0, 3};
    t.script[5] = {OpCode::Signal, 0, 3};
    t.script[6] = {};
    CHECK(scriptText(t) == "LOCK 1; RUN 4; UNLOCK 1; SLEEP 2; WAIT 3; SIGNAL 3; END");
    CHECK(std::string(toString(Policy::EDF)) == "edf");
    CHECK(std::string(toString(Policy::RateMonotonic)) == "rate_monotonic");
    CHECK(std::string(toString(EventType::PriorityChange)) == "PRIORITY_CHANGE");
    std::cout << "PASS: script, policy and event spellings\n";
}

int main(int argc, char** argv) {
    const char* outputDir = argc > 1 ? argv[1] : nullptr;
    testBoundedArray();
    testSegmentsMergeAndSplit();
    testEventOrderAndTruncation();
    testSnapshotWithResources();
    testSnapshotRowLimit();
    testGoldenTraceA(outputDir);
    testEscapingAndNulls(outputDir);
    testFileSizeAtLimits(outputDir);
    testScriptText();
    std::cout << "All run export tests passed.\n";
    return 0;
}
