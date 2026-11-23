// Scheduler tests (Youssef Gohar). Expected results come from the completion
// plan's golden traces (pp. 15-16), worked out by hand from the scenario
// definitions, not copied from the program's output.
//
// usage: test_scheduler <directory for exported run files>
// The exported runs are then checked by tests/validate_runs.js with the
// dashboard's own import validator.

#include "presets.h"
#include "simulation.h"
#include "validation.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace b6;

// Checks stay active in Release builds too.
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    std::exit(1); } } while (false)

#define CHECK_EQ(actual, expected) do { const auto a_ = (actual); const auto e_ = (expected); \
    if (!(a_ == e_)) { std::cerr << "Failed at line " << __LINE__ << ":\n  got      " << a_ \
                                 << "\n  expected " << e_ << '\n'; std::exit(1); } } while (false)

static std::string g_outputDir;

// ---- Helpers ------------------------------------------------------------------

static ScenarioInput& presetInput(const std::string& id) {
    static ScenarioInput input;  // Large fixed arrays: keep them off the stack.
    const Preset* p = findPreset(id);
    CHECK(p != nullptr);
    p->build(input);
    return input;
}

static std::unique_ptr<Simulation> runPreset(const std::string& id,
                                             std::optional<Policy> policy = std::nullopt,
                                             TimerBackend timer = TimerBackend::SortedList) {
    ScenarioInput& in = presetInput(id);
    if (policy) in.scenario.config.policy = *policy;
    in.scenario.config.timerBackend = timer;
    auto sim = std::make_unique<Simulation>(in);
    if (!sim->run()) {
        std::cerr << id << " run failed: " << sim->error() << '\n';
        std::exit(1);
    }
    return sim;
}

// "1[0,1) 2[1,2) idle[7,8)": job ID (or idle) per CPU segment.
static std::string cpu(const Simulation& sim) {
    std::ostringstream out;
    const auto& segments = sim.trace().segments();
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const TraceSegment& s = segments.at(i);
        if (i) out << ' ';
        if (s.jobId) out << *s.jobId;
        else out << "idle";
        out << '[' << s.start << ',' << s.end << ')';
    }
    return out.str();
}

static const Job& job(const Simulation& sim, JobId id) {
    const Job* j = sim.registry().findJob(id);
    CHECK(j != nullptr);
    return *j;
}

// "2@110 4@110" for every event of `type` (optionally only one reason).
static std::string events(const Simulation& sim, EventType type, const char* reason = nullptr) {
    std::ostringstream out;
    bool first = true;
    const auto& all = sim.trace().events();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const TraceEvent& e = all.at(i);
        if (e.type != type || (reason && std::string(reason) != e.reason)) continue;
        if (!first) out << ' ';
        first = false;
        out << (e.jobId ? std::to_string(*e.jobId) : "-") << '@' << e.tick;
    }
    return out.str();
}

static const TraceEvent* findEvent(const Simulation& sim, EventType type, Tick tick, JobId jobId) {
    const auto& all = sim.trace().events();
    for (std::size_t i = 0; i < all.size(); ++i) {
        const TraceEvent& e = all.at(i);
        if (e.type == type && e.tick == tick && e.jobId == jobId) return &e;
    }
    return nullptr;
}

static std::optional<std::int64_t> datum(const TraceEvent& e, const char* key) {
    for (std::size_t i = 0; i < e.dataCount; ++i)
        if (std::string(e.data[i].key) == key && e.data[i].kind != EventDatum::Kind::Null)
            return e.data[i].value;
    return std::nullopt;
}

static const TraceSnapshot& snapshotAt(const Simulation& sim, Tick tick) {
    const auto& snaps = sim.trace().snapshots();
    for (std::size_t i = 0; i < snaps.size(); ++i)
        if (snaps.at(i).tick == tick) return snaps.at(i);
    std::cerr << "no snapshot at tick " << tick << '\n';
    std::exit(1);
}

static const SnapshotJobRow& rowAt(const Simulation& sim, Tick tick, JobId id) {
    const TraceSnapshot& snap = snapshotAt(sim, tick);
    for (std::size_t i = 0; i < snap.rowCount; ++i) {
        const SnapshotJobRow& row = sim.trace().jobRows().at(snap.firstRow + i);
        if (row.jobId == id) return row;
    }
    std::cerr << "job " << id << " missing from snapshot " << tick << '\n';
    std::exit(1);
}

static std::string exported(const Simulation& sim, const std::string& runId) {
    std::ostringstream out;
    RunInfo info;
    info.runId = runId;
    info.label = runId;
    info.engineRevision = "test";  // Fixed, so two runs compare byte for byte.
    sim.exportTo(out, info);
    return out.str();
}

template <typename Exception, typename F>
static std::string throwsWith(F f) {
    try {
        f();
    } catch (const Exception& e) {
        return e.what();
    }
    std::cerr << "expected an exception\n";
    std::exit(1);
}

// ---- Golden traces ---------------------------------------------------------------

// Plan trace A: A and B (priority 5, 3 ticks each) share the CPU, quantum 1;
// H (priority 8, 1 tick) arrives at 2 and preempts immediately.
static void testRoundRobinAndPreemption() {
    for (TimerBackend timer : {TimerBackend::SortedList, TimerBackend::Heap}) {
        auto sim = runPreset("rr_preemption", std::nullopt, timer);
        CHECK_EQ(cpu(*sim), std::string("1[0,1) 2[1,2) 3[2,3) 1[3,4) 2[4,5) 1[5,6) 2[6,7) idle[7,8)"));
        CHECK_EQ(*job(*sim, 1).completionTick, 6);  // A
        CHECK_EQ(*job(*sim, 2).completionTick, 7);  // B
        CHECK_EQ(*job(*sim, 3).completionTick, 3);  // H: response 1
        for (JobId id = 1; id <= 3; ++id) CHECK(!job(*sim, id).deadlineMissRecorded);
        // Slice expiry hands A's turn to B at t=1; H's arrival preempts B at t=2.
        CHECK_EQ(std::string(findEvent(*sim, EventType::Preempt, 1, 1)->reason), std::string("quantum_expired"));
        CHECK_EQ(std::string(findEvent(*sim, EventType::Preempt, 2, 2)->reason), std::string("higher_priority"));
        // Snapshot at t=2: H running; ready ring 5 has A then B (B just rotated).
        const TraceSnapshot& snap = snapshotAt(*sim, 2);
        CHECK(snap.runningJobId == 3);
        CHECK_EQ(snap.readyCount, std::size_t(2));
        CHECK_EQ(sim->trace().idPool().at(snap.firstReady), 1);
        CHECK_EQ(sim->trace().idPool().at(snap.firstReady + 1), 2);
    }
}

// Plan trace B, inheritance OFF: L locks R, H blocks on R at 1, M runs [2,6).
static void testPriorityInversionWithoutInheritance() {
    auto sim = runPreset("priority_inversion");
    // Job IDs follow release order: L=1 (t=0), H=2 (t=1), M=3 (t=2).
    CHECK_EQ(cpu(*sim), std::string("1[0,2) 3[2,6) 1[6,8) 2[8,9) idle[9,10)"));
    const Job& h = job(*sim, 2);
    CHECK_EQ(*h.completionTick, 9);  // Response 8.
    CHECK(h.deadlineMissRecorded && *h.deadlineMissTick == 6);
    CHECK_EQ(events(*sim, EventType::DeadlineMiss), std::string("2@6"));  // Recorded once.
    const TraceEvent* block = findEvent(*sim, EventType::Block, 1, 2);
    CHECK(block && std::string(block->reason) == "mutex_owned");
    CHECK(datum(*block, "ownerJobId") == 1);
    // L's UNLOCK at 8 grants R straight to H.
    CHECK_EQ(events(*sim, EventType::Wake, "mutex_granted"), std::string("2@8"));
    const SnapshotJobRow& waiting = rowAt(*sim, 4, 2);
    CHECK(waiting.state == JobState::Blocked && waiting.blockReason == BlockReason::Mutex);
    CHECK(waiting.blockedResourceId == 0);
    CHECK(rowAt(*sim, 4, 1).heldMutexId == 0);
}

// Inheritance ON is Moataz's donation work. Until it exists the run must be
// refused clearly rather than silently running without inheritance.
// When inheritance lands, replace this with the plan's ON oracle:
// L[0,4) H[4,5) M[5,9); H completes 5; L's priority 1->3 at 1, back at 4.
static void testInheritanceOnIsRefusedUntilImplemented() {
    ScenarioInput& in = presetInput("priority_inversion");
    in.scenario.config.inheritance = true;
    const std::string error = throwsWith<std::invalid_argument>([&] { Simulation sim(in); });
    CHECK(error.find("inheritance") != std::string::npos);
}

// Plan trace C: four sleeps started at 100. Both timer backends must agree.
static void testSleepOrderOnBothBackends() {
    auto list = runPreset("sleep_order", std::nullopt, TimerBackend::SortedList);
    auto heap = runPreset("sleep_order", std::nullopt, TimerBackend::Heap);
    // Job IDs: A=1, B=2, C=3, D=4. B and D both wake at 110; B slept first.
    const std::string expectedCpu =
        "idle[0,110) 2[110,111) 4[111,112) idle[112,120) 3[120,121) idle[121,130) 1[130,131) idle[131,140)";
    CHECK_EQ(cpu(*list), expectedCpu);
    CHECK_EQ(cpu(*heap), expectedCpu);
    CHECK_EQ(events(*list, EventType::Wake), std::string("2@110 4@110 3@120 1@130"));
    // Same calls, same ticks: the whole event history is identical.
    for (EventType t : {EventType::Release, EventType::Block, EventType::Wake, EventType::Dispatch,
                        EventType::Preempt, EventType::Complete})
        CHECK_EQ(events(*list, t), events(*heap, t));
    // A sleeper uses no CPU while it waits.
    CHECK(rowAt(*list, 105, 1).state == JobState::Blocked);
    CHECK(rowAt(*list, 105, 1).wakeTick == 130);
}

// Plan trace C (second part): suspension must keep the reason for waiting.
static void testSuspendResume() {
    auto sim = runPreset("suspend_resume");
    // S=1 (task 0), X=2 (task 1).
    CHECK_EQ(cpu(*sim), std::string("2[0,1) idle[1,3) 2[3,6) idle[6,7) 1[7,9) idle[9,10)"));
    CHECK_EQ(*job(*sim, 2).completionTick, 6);
    CHECK_EQ(*job(*sim, 1).completionTick, 9);
    // At 4, S is suspended and still waiting on its timer.
    const SnapshotJobRow& before = rowAt(*sim, 4, 1);
    CHECK(before.state == JobState::Suspended && before.suspended);
    CHECK(before.blockReason == BlockReason::Timer && before.wakeTick == 5);
    // The timer fires at 5 while S is suspended: wait resolved, still suspended.
    const TraceEvent* wake = findEvent(*sim, EventType::Wake, 5, 1);
    CHECK(wake && datum(*wake, "suspended") == 1);
    const SnapshotJobRow& after = rowAt(*sim, 6, 1);
    CHECK(after.state == JobState::Suspended && after.blockReason == BlockReason::None);
    CHECK(!after.wakeTick);
    CHECK_EQ(events(*sim, EventType::Suspend), std::string("2@1 1@2"));
    CHECK_EQ(events(*sim, EventType::Resume), std::string("2@3 1@7"));
}

// Plan trace D: the permit goes straight to the waiting consumer.
static void testSemaphoreSignal() {
    auto sim = runPreset("semaphore_signal");
    // C=1 (task 0), P=2 (task 1).
    CHECK_EQ(cpu(*sim), std::string("2[0,2) 1[2,3) 2[3,4) idle[4,5)"));
    CHECK_EQ(*job(*sim, 1).completionTick, 3);
    CHECK_EQ(*job(*sim, 2).completionTick, 4);
    const TraceEvent* signal = findEvent(*sim, EventType::Signal, 2, 2);
    CHECK(signal && datum(*signal, "grantedJobId") == 1);
    CHECK(rowAt(*sim, 1, 1).blockReason == BlockReason::Semaphore);
    // Granted directly: the count never rose to 1, so nobody else could take it.
    const TraceSnapshot& last = snapshotAt(*sim, 5);
    CHECK_EQ(last.resourceCount, std::size_t(1));
    CHECK_EQ(sim->trace().resourceRows().at(last.firstResource).count, 0);
}

// Plan trace E under EDF and RM, and under fixed priority (whose hand-set
// base priorities follow period order, so it must match RM).
static void testRmAndEdfComparison() {
    auto edf = runPreset("rm_edf_compare");
    // A0=1, B0=2 (t=0), A1=3 (t=5), B1=4 (t=7).
    CHECK_EQ(cpu(*edf), std::string("1[0,2) 2[2,6) 3[6,8)"));
    CHECK_EQ(*job(*edf, 2).completionTick, 6);
    CHECK(!job(*edf, 2).deadlineMissRecorded);
    CHECK_EQ(edf->registry().jobCount(), std::size_t(4));
    CHECK(!job(*edf, 4).completionTick && job(*edf, 4).releaseTick == 7);  // B1 visible, unfinished.

    // RM: A (T=5) gets 31, B (T=7) gets 30; the preset's own priorities are ignored.
    auto rm = runPreset("rm_edf_compare", Policy::RateMonotonic);
    CHECK_EQ(rm->registry().findTask(0)->basePriority, 31);
    CHECK_EQ(rm->registry().findTask(1)->basePriority, 30);
    CHECK_EQ(cpu(*rm), std::string("1[0,2) 2[2,5) 3[5,7) 2[7,8)"));  // A1 preempts B0 at 5.
    CHECK_EQ(*job(*rm, 2).completionTick, 8);
    CHECK(job(*rm, 2).deadlineMissRecorded && *job(*rm, 2).deadlineMissTick == 7);
    CHECK(!job(*rm, 4).completionTick && job(*rm, 4).releaseTick == 7);
    CHECK_EQ(rm->registry().jobCount(), std::size_t(4));  // Same releases as EDF.

    auto fp = runPreset("rm_edf_compare", Policy::FixedPriority);
    CHECK_EQ(cpu(*fp), cpu(*rm));
    CHECK_EQ(*job(*fp, 2).completionTick, 8);
    CHECK(job(*fp, 2).deadlineMissRecorded && *job(*fp, 2).deadlineMissTick == 7);
    CHECK(!job(*fp, 4).completionTick);
}

// 135% demand: every release exists as its own job; misses are recorded.
static void testOverload() {
    for (Policy policy : {Policy::FixedPriority, Policy::RateMonotonic, Policy::EDF}) {
        auto sim = runPreset("overload", policy);
        CHECK_EQ(sim->registry().jobCount(), std::size_t(9));  // A: 0,4,..,16; B: 0,5,10,15.
        std::size_t misses = 0;
        for (JobId id = 1; id <= 9; ++id) misses += job(*sim, id).deadlineMissRecorded ? 1 : 0;
        CHECK(misses > 0);
        CHECK(cpu(*sim).find("idle") == std::string::npos);  // The CPU never idles.
    }
}

static void checkRunProperties(const Simulation& sim);

// Plan acceptance "boundary deadline": completion exactly at the deadline is
// on time; a job unfinished at its deadline is flagged once, and a later
// completion keeps the miss; a deadline after the horizon is never assumed missed.
static void testDeadlineBoundary() {
    static ScenarioInput in;  // Large fixed arrays: keep them off the stack.
    in = ScenarioInput{};
    Scenario& s = in.scenario;
    s.config.scenarioId = "deadline_boundary";
    s.config.horizonTicks = 8;
    s.config.quantumTicks = 10;  // No rotation; only priorities matter here.
    struct Spec { const char* name; int priority; Tick run; Tick deadline; };
    const Spec specs[] = {{"on_time", 5, 3, 3}, {"late", 3, 2, 3}, {"future", 1, 10, 50}};
    for (const Spec& spec : specs) {
        TaskSpec& t = s.tasks[s.taskCount];
        t.taskId = static_cast<TaskId>(s.taskCount++);
        t.name = spec.name;
        t.basePriority = spec.priority;
        t.relativeDeadline = spec.deadline;
        t.scriptLength = 2;
        t.script[0] = {OpCode::Run, spec.run, std::nullopt};
        t.script[1] = {OpCode::End, 0, std::nullopt};
    }
    Simulation sim(in);
    CHECK(sim.run());
    CHECK_EQ(cpu(sim), std::string("1[0,3) 2[3,5) 3[5,8)"));

    const Job& onTime = job(sim, 1);  // Completes at 3 == its deadline 3.
    CHECK_EQ(*onTime.completionTick, 3);
    CHECK(!onTime.deadlineMissRecorded);

    const Job& late = job(sim, 2);    // Unfinished at 3, completes at 5.
    CHECK(late.deadlineMissRecorded && *late.deadlineMissTick == 3);
    CHECK_EQ(*late.completionTick, 5);
    CHECK_EQ(events(sim, EventType::DeadlineMiss), std::string("2@3"));  // Once, not at 4 again.
    const TraceEvent* done = findEvent(sim, EventType::Complete, 5, 2);
    CHECK(done && datum(*done, "late") == 1);

    const Job& future = job(sim, 3);  // Unfinished at the horizon, deadline 50.
    CHECK(!future.completionTick && !future.deadlineMissRecorded);
    checkRunProperties(sim);
}

// ---- Properties checked on every preset, policy and timer backend --------------

static void checkRunProperties(const Simulation& sim) {
    const RunTrace& trace = sim.trace();
    const Registry& registry = sim.registry();
    const Tick horizon = registry.scenario().config.horizonTicks;

    // CPU conservation: each job's segments add up to the CPU it reports.
    for (JobId id = 1; id <= static_cast<JobId>(registry.jobCount()); ++id) {
        Tick fromSegments = 0;
        for (std::size_t i = 0; i < trace.segments().size(); ++i) {
            const TraceSegment& s = trace.segments().at(i);
            if (s.jobId == id) fromSegments += s.end - s.start;
        }
        CHECK_EQ(fromSegments, job(sim, id).totalCpuUsed);
    }

    // Segments cover the whole horizon without gaps.
    Tick covered = 0;
    for (std::size_t i = 0; i < trace.segments().size(); ++i) {
        const TraceSegment& s = trace.segments().at(i);
        CHECK(s.start == covered);
        covered = s.end;
    }
    CHECK_EQ(covered, horizon);

    // One snapshot per boundary; the job shown RUNNING is the one that runs
    // next; never more than one RUNNING job; nobody blocked/suspended runs.
    CHECK_EQ(trace.snapshots().size(), static_cast<std::size_t>(horizon + 1));
    std::size_t segment = 0;
    for (Tick t = 0; t < horizon; ++t) {
        while (trace.segments().at(segment).end <= t) ++segment;
        const TraceSnapshot& snap = snapshotAt(sim, t);
        CHECK(snap.runningJobId == trace.segments().at(segment).jobId);
        std::size_t runningRows = 0;
        for (std::size_t i = 0; i < snap.rowCount; ++i) {
            const SnapshotJobRow& row = trace.jobRows().at(snap.firstRow + i);
            if (row.state == JobState::Running) {
                ++runningRows;
                CHECK(snap.runningJobId == row.jobId);
                CHECK(!row.suspended && row.blockReason == BlockReason::None);
            }
        }
        CHECK(runningRows <= 1);
    }
    CHECK(trace.isValid() && !trace.isTruncated());
}

static void testAllPresetsAndRepeatability() {
    std::size_t written = 0;
    for (std::size_t i = 0; i < presetCount(); ++i) {
        const std::string id = presetAt(i).id;
        for (Policy policy : {Policy::FixedPriority, Policy::RateMonotonic, Policy::EDF}) {
            // RM/EDF are only defined for periodic, resource-free task sets (validator).
            ScenarioInput& probe = presetInput(id);
            probe.scenario.config.policy = policy;
            if (!validateScenario(probe.scenario).empty()) continue;
            for (TimerBackend timer : {TimerBackend::SortedList, TimerBackend::Heap}) {
                auto first = runPreset(id, policy, timer);
                checkRunProperties(*first);
                // Same input, same decisions: the exported files are identical.
                auto second = runPreset(id, policy, timer);
                const std::string runId = id + "-" + toString(policy) + "-" + toString(timer);
                const std::string text = exported(*first, runId);
                CHECK(text == exported(*second, runId));
                std::ofstream(g_outputDir + "/" + runId + ".json") << text;
                ++written;
            }
        }
    }
    // FP everywhere; RM and EDF for the 2 periodic presets.
    CHECK(written == 2 * presetCount() + 2 * 2 * 2);
}

// ---- Requeue hook for inheritance ---------------------------------------------------

static void testPriorityMoveKeepsRingsAndBitmapInStep() {
    static Scenario s;  // Large fixed arrays: keep them off the stack.
    s = Scenario{};
    s.config.scenarioId = "priority_move";
    s.config.horizonTicks = 5;
    s.taskCount = 2;
    for (int i = 0; i < 2; ++i) {
        TaskSpec& t = s.tasks[i];
        t.taskId = i;
        t.name = i ? "high" : "low";
        t.basePriority = i ? 3 : 1;
        t.relativeDeadline = 5;
        t.scriptLength = 2;
        t.script[0] = {OpCode::Run, 1, std::nullopt};
        t.script[1] = {OpCode::End, 0, std::nullopt};
    }
    Registry registry(s);
    RunTrace trace(s.config);
    Scheduler scheduler(registry, trace);
    TimerManager timers(registry);
    scheduler.attach(nullptr, timers);

    Job& low = registry.releaseJob(0, 0);
    scheduler.onReady(low);
    Job& high = registry.releaseJob(1, 0);
    scheduler.onReady(high);
    scheduler.checkInvariants();

    scheduler.changeEffectivePriority(low, 7, "inheritance");  // Donation upward.
    CHECK(low.effectivePriority == 7 && low.readyNode != nullptr);
    scheduler.checkInvariants();  // Ring 1 now empty, bit 1 clear; ring 7 holds low.
    scheduler.changeEffectivePriority(low, 1, "restore");      // Back to base.
    scheduler.checkInvariants();
    CHECK(trace.events().size() == 2);
    const TraceEvent& up = trace.events().at(0);
    CHECK(up.type == EventType::PriorityChange && datum(up, "from") == 1 && datum(up, "to") == 7);
    throwsWith<std::invalid_argument>([&] { scheduler.changeEffectivePriority(low, 32, "bad"); });
    (void)high;
}

// ---- Invalid input and runtime errors -------------------------------------------------

static void testInvalidInputs() {
    {   // Control for a task that does not exist: rejected before the run.
        ScenarioInput& in = presetInput("rr_preemption");
        in.controls[0] = {1, 99, ControlAction::Kind::Suspend};
        in.controlCount = 1;
        const std::string e = throwsWith<std::invalid_argument>([&] { Simulation sim(in); });
        CHECK(e.find("unknown task") != std::string::npos);
    }
    {   // Controls must be in tick order.
        ScenarioInput& in = presetInput("suspend_resume");
        in.controls[0].tick = 5;
        const std::string e = throwsWith<std::invalid_argument>([&] { Simulation sim(in); });
        CHECK(e.find("out of order") != std::string::npos);
    }
    {   // RM/EDF runs must be resource-free (validator).
        ScenarioInput& in = presetInput("semaphore_signal");
        in.scenario.config.policy = Policy::EDF;
        throwsWith<std::invalid_argument>([&] { Simulation sim(in); });
    }
    {   // Suspending a job that holds a mutex is unsupported: the run stops
        // with an explicit error and the trace says invalid.
        ScenarioInput& in = presetInput("priority_inversion");
        in.controls[0] = {1, 0, ControlAction::Kind::Suspend};  // L owns R at t=1.
        in.controlCount = 1;
        Simulation sim(in);
        CHECK(!sim.run());
        CHECK(sim.error().find("holding a mutex") != std::string::npos);
        CHECK(exported(sim, "invalid").find("\"invalid\"") != std::string::npos);
        CHECK(events(sim, EventType::Error) == "-@1");
    }
    {   // RESUME of a job that is not suspended.
        ScenarioInput& in = presetInput("rr_preemption");
        in.controls[0] = {1, 0, ControlAction::Kind::Resume};
        in.controlCount = 1;
        Simulation sim(in);
        CHECK(!sim.run());
        CHECK(sim.error().find("not suspended") != std::string::npos);
    }
    {   // A control before the task's first release has no job to act on.
        ScenarioInput& in = presetInput("rr_preemption");
        in.controls[0] = {1, 2, ControlAction::Kind::Suspend};  // H releases at 2.
        in.controlCount = 1;
        Simulation sim(in);
        CHECK(!sim.run());
        CHECK(sim.error().find("no released") != std::string::npos);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: test_scheduler <output directory>\n";
        return 2;
    }
    g_outputDir = argv[1];
    testRoundRobinAndPreemption();
    testPriorityInversionWithoutInheritance();
    testInheritanceOnIsRefusedUntilImplemented();
    testSleepOrderOnBothBackends();
    testSuspendResume();
    testSemaphoreSignal();
    testRmAndEdfComparison();
    testOverload();
    testDeadlineBoundary();
    testAllPresetsAndRepeatability();
    testPriorityMoveKeepsRingsAndBitmapInStep();
    testInvalidInputs();
    std::cout << "scheduler tests passed\n";
    return 0;
}
