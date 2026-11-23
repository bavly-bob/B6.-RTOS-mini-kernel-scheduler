// Metrics tests (Marc). Expected values are worked out by hand from the
// golden traces in docs/scheduler.md, not copied from the program's output.

#include "metrics.h"
#include "presets.h"
#include "simulation.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace b6;

// Checks stay active in Release builds too.
#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    std::exit(1); } } while (false)

#define CHECK_EQ(actual, expected) do { const auto a_ = (actual); const auto e_ = (expected); \
    if (!(a_ == e_)) { std::cerr << "Failed at line " << __LINE__ << ":\n  got      " << a_ \
                                 << "\n  expected " << e_ << '\n'; std::exit(1); } } while (false)

static bool near(std::optional<double> value, double expected) {
    return value && std::fabs(*value - expected) < 1e-9;
}

static ScenarioInput& presetInput(const std::string& id) {
    static ScenarioInput input;  // Large fixed arrays: keep them off the stack.
    const Preset* p = findPreset(id);
    CHECK(p != nullptr);
    p->build(input);
    return input;
}

static std::unique_ptr<Simulation> run(const std::string& id, std::optional<Policy> policy = std::nullopt) {
    ScenarioInput& in = presetInput(id);
    if (policy) in.scenario.config.policy = *policy;
    auto sim = std::make_unique<Simulation>(in);
    CHECK(sim->run());
    return sim;
}

static std::string exported(const Simulation& sim) {
    std::ostringstream out;
    RunInfo info;
    info.runId = "metrics_test";
    sim.exportTo(out, info);
    return out.str();
}

// rm_edf_compare under RM: A0[0,2) B0[2,5) A1[5,7) B0[7,8); B1 released at 7.
static void testRateMonotonic() {
    auto sim = run("rm_edf_compare", Policy::RateMonotonic);
    const RunMetrics m = *sim->metrics();
    CHECK_EQ(m.busyTicks, Tick(8));
    CHECK_EQ(m.observedTicks, Tick(8));
    CHECK(near(m.busyPercent, 100.0));
    CHECK_EQ(m.completedJobs, std::size_t(3));
    CHECK_EQ(m.unfinishedJobs, std::size_t(1));  // B1.
    CHECK_EQ(m.deadlineMisses, std::size_t(1));  // B0 at 7.
    CHECK(near(m.demandUtilization, 2.0 / 5 + 4.0 / 7));  // 34/35.
    CHECK(m.rmBoundApplicable);
    CHECK(near(m.rmBound, 2 * (std::sqrt(2.0) - 1)));
    CHECK_EQ(m.rmBoundNote, std::string("demand above the bound is inconclusive"));

    CHECK_EQ(m.taskCount, std::size_t(2));
    const TaskMetrics& a = m.perTask[0];  // A0 2-0, A1 7-5.
    CHECK_EQ(a.taskId, 0);
    CHECK_EQ(a.completedSamples, std::size_t(2));
    CHECK(*a.responseMin == 2 && *a.responseMax == 2 && near(a.responseMean, 2.0));
    CHECK(a.jitter && *a.jitter == 0);
    CHECK_EQ(a.misses, std::size_t(0));
    const TaskMetrics& b = m.perTask[1];  // B0 completed late at 8: still a sample.
    CHECK_EQ(b.completedSamples, std::size_t(1));
    CHECK(*b.responseMin == 8 && *b.responseMax == 8 && near(b.responseMean, 8.0));
    CHECK(!b.jitter);  // One sample.
    CHECK_EQ(b.misses, std::size_t(1));
}

// Same releases under EDF: A0[0,2) B0[2,6) A1[6,8); A1 completes at the horizon.
static void testEdf() {
    auto sim = run("rm_edf_compare");
    const RunMetrics m = *sim->metrics();
    CHECK(m.busyTicks == 8 && m.completedJobs == 3 && m.unfinishedJobs == 1);
    CHECK_EQ(m.deadlineMisses, std::size_t(0));
    CHECK(near(m.demandUtilization, 34.0 / 35));  // Demand depends on the tasks only.
    CHECK(!m.rmBoundApplicable && !m.rmBound);
    CHECK_EQ(m.rmBoundNote, std::string("policy is edf, not rate_monotonic"));
    const TaskMetrics& a = m.perTask[0];  // A0 2, A1 8-5 = 3.
    CHECK(*a.responseMin == 2 && *a.responseMax == 3 && near(a.responseMean, 2.5));
    CHECK(a.jitter && *a.jitter == 1);
    CHECK(*m.perTask[1].responseMin == 6 && m.perTask[1].misses == 0);
}

// One-shot tasks: A[0,1) B[1,2) H[2,3) A[3,4) B[4,5) A[5,6) B[6,7) idle[7,8).
static void testOneShotTasks() {
    auto sim = run("rr_preemption");
    const RunMetrics m = *sim->metrics();
    CHECK(m.busyTicks == 7 && m.observedTicks == 8 && near(m.busyPercent, 87.5));
    CHECK(m.completedJobs == 3 && m.unfinishedJobs == 0 && m.deadlineMisses == 0);
    CHECK(!m.demandUtilization && !m.rmBound && !m.rmBoundApplicable);
    CHECK_EQ(m.rmBoundNote, std::string("one-shot tasks"));
    CHECK(*m.perTask[0].responseMin == 6 && *m.perTask[1].responseMin == 7);  // A, B.
    CHECK(*m.perTask[2].responseMin == 1);  // H released 2, completes 3.
    for (std::size_t i = 0; i < 3; ++i) CHECK(!m.perTask[i].jitter);
}

// Inversion OFF: L[0,2) M[2,6) L[6,8) H[8,9) idle[9,10); H misses at 6.
static void testInversionMisses() {
    auto sim = run("priority_inversion");
    const RunMetrics m = *sim->metrics();
    CHECK(m.busyTicks == 9 && near(m.busyPercent, 90.0));
    CHECK_EQ(m.deadlineMisses, std::size_t(1));
    CHECK(m.perTask[1].misses == 1 && *m.perTask[1].responseMin == 8);  // H: 9 - 1.
    CHECK(*m.perTask[0].responseMin == 8 && *m.perTask[2].responseMin == 4);  // L, M.
}

// Overload (135% demand): the bound applies only to the RM run.
static void testOverload() {
    for (Policy policy : {Policy::FixedPriority, Policy::RateMonotonic, Policy::EDF}) {
        auto sim = run("overload", policy);
        const RunMetrics m = *sim->metrics();
        CHECK(m.busyTicks == 20 && near(m.busyPercent, 100.0));
        CHECK_EQ(m.completedJobs + m.unfinishedJobs, std::size_t(9));
        CHECK(m.deadlineMisses > 0);
        CHECK_EQ(m.perTask[0].misses + m.perTask[1].misses, m.deadlineMisses);
        CHECK(near(m.demandUtilization, 3.0 / 4 + 3.0 / 5));
        CHECK_EQ(m.rmBoundApplicable, policy == Policy::RateMonotonic);
        if (policy == Policy::RateMonotonic)
            CHECK_EQ(m.rmBoundNote, std::string("demand above 100%: overload, misses are unavoidable over a long enough run"));
    }
}

// Within the bound: one task at 2/5 under RM (bound for n = 1 is 100%).
static void testWithinBound() {
    ScenarioInput& in = presetInput("rm_edf_compare");
    in.scenario.taskCount = 1;
    in.scenario.config.policy = Policy::RateMonotonic;
    Simulation sim(in);
    CHECK(sim.run());
    const RunMetrics m = *sim.metrics();
    CHECK(near(m.rmBound, 1.0) && near(m.demandUtilization, 0.4));
    CHECK_EQ(m.rmBoundNote, std::string("demand within the bound: every deadline is met"));
    CHECK_EQ(m.deadlineMisses, std::size_t(0));
}

// Deadlines that differ from periods: Liu and Layland's assumption fails.
static void testConstrainedDeadline() {
    ScenarioInput& in = presetInput("rm_edf_compare");
    in.scenario.config.policy = Policy::RateMonotonic;
    in.scenario.tasks[1].relativeDeadline = 6;
    Simulation sim(in);
    CHECK(sim.run());
    const RunMetrics m = *sim.metrics();
    CHECK(!m.rmBoundApplicable && !m.rmBound && m.demandUtilization);
    CHECK_EQ(m.rmBoundNote, std::string("deadlines differ from periods"));
}

// The run file carries the metrics; an invalid run carries none, and says so.
static void testExport() {
    auto sim = run("rm_edf_compare", Policy::RateMonotonic);
    const std::string text = exported(*sim);
    CHECK(text.find("\"deadlineMisses\":1") != std::string::npos);
    CHECK(text.find("\"rmBoundApplicable\":true") != std::string::npos);
    CHECK(text.find("\"completedSamples\":2") != std::string::npos);
    CHECK(text.find("metrics were not computed") == std::string::npos);

    ScenarioInput& in = presetInput("priority_inversion");
    in.controls[0] = {1, 0, ControlAction::Kind::Suspend};  // L owns R: the run stops.
    in.controlCount = 1;
    Simulation invalid(in);
    CHECK(!invalid.run());
    CHECK(!invalid.metrics());
    const std::string bad = exported(invalid);
    CHECK(bad.find("\"overall\":{}") != std::string::npos);
    CHECK(bad.find("metrics were not computed") != std::string::npos);
}

int main() {
    testRateMonotonic();
    testEdf();
    testOneShotTasks();
    testInversionMisses();
    testOverload();
    testWithinBound();
    testConstrainedDeadline();
    testExport();
    std::cout << "metrics tests passed\n";
    return 0;
}
