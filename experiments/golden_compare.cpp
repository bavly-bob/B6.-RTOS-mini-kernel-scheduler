// b6_golden: run every preset through the shared engine on both timer
// backends and compare the measured outcome with its golden trace.
// Prints Markdown tables (docs/results.md is made from this output).
//
// Golden values are copied from docs/scheduler.md and scenarios/*.md, which
// were worked out by hand from the plan (pp. 15-16); never from engine output.
// Exit code: 0 when every row matches, 1 otherwise.

#include "presets.h"
#include "simulation.h"

#include <iostream>
#include <memory>
#include <sstream>
#include <string>

using namespace b6;

namespace {

// What a row compares. CPU and outcomes come from the golden traces; Wakes is
// sleep_order's golden wake order; Properties is overload's stated properties.
enum class Kind { Cpu, Wakes, Properties };

struct Golden {
    const char* scenario;
    Policy policy;
    bool inheritance;
    Kind kind;
    const char* expected;  // CPU trace, wake order, or the overload properties.
    const char* outcome;   // "label:completion[!miss]" per job in job-ID order; "" = not stated.
};

// Outcome format: A:6 = completed at 6; B0:8!7 = completed at 8, missed at 7;
// B1:open = released, unfinished at the horizon.
const Golden kGolden[] = {
    {"rr_preemption", Policy::FixedPriority, false, Kind::Cpu,
     "A[0,1) B[1,2) H[2,3) A[3,4) B[4,5) A[5,6) B[6,7) idle[7,8)", "A:6 B:7 H:3"},
    {"sleep_order", Policy::FixedPriority, false, Kind::Wakes, "B@110 D@110 C@120 A@130", ""},
    {"suspend_resume", Policy::FixedPriority, false, Kind::Cpu,
     "X[0,1) idle[1,3) X[3,6) idle[6,7) S[7,9) idle[9,10)", "S:9 X:6"},
    {"semaphore_signal", Policy::FixedPriority, false, Kind::Cpu,
     "P[0,2) C[2,3) P[3,4) idle[4,5)", "C:3 P:4"},
    {"priority_inversion", Policy::FixedPriority, false, Kind::Cpu,
     "L[0,2) M[2,6) L[6,8) H[8,9) idle[9,10)", "L:8 H:9!6 M:6"},
    {"priority_inversion", Policy::FixedPriority, true, Kind::Cpu,
     "L[0,4) H[4,5) M[5,9) idle[9,10)", "L:4 H:5 M:9"},
    {"rm_edf_compare", Policy::RateMonotonic, false, Kind::Cpu,
     "A0[0,2) B0[2,5) A1[5,7) B0[7,8)", "A0:2 B0:8!7 A1:7 B1:open"},
    {"rm_edf_compare", Policy::EDF, false, Kind::Cpu,
     "A0[0,2) B0[2,6) A1[6,8)", "A0:2 B0:6 A1:8 B1:open"},
    {"rm_edf_compare", Policy::FixedPriority, false, Kind::Cpu,
     "A0[0,2) B0[2,5) A1[5,7) B0[7,8)", "A0:2 B0:8!7 A1:7 B1:open"},
    {"overload", Policy::FixedPriority, false, Kind::Properties, "9 jobs, misses > 0, idle 0", ""},
    {"overload", Policy::RateMonotonic, false, Kind::Properties, "9 jobs, misses > 0, idle 0", ""},
    {"overload", Policy::EDF, false, Kind::Properties, "9 jobs, misses > 0, idle 0", ""},
};

// "A" for a one-shot task, "A0", "A1", ... for the jobs of a periodic task.
std::string jobLabel(const Registry& registry, JobId id) {
    const Job* job = registry.findJob(id);
    const TaskSpec* task = registry.findTask(job->taskId);
    if (task->periodTicks == 0) return task->name;
    int ordinal = 0;
    for (JobId other = 1; other < id; ++other)
        if (registry.findJob(other)->taskId == job->taskId) ++ordinal;
    return task->name + std::to_string(ordinal);
}

// Golden traces state who owns the CPU, not at which priority. The engine
// starts a new segment when a job's priority changes (run-schema-v1, so the
// Gantt chart shows inheritance), so adjacent segments of the same job (or
// of idle) are joined here: L[0,1) L[1,4) is printed as L[0,4).
std::string cpuTrace(const Simulation& sim) {
    std::ostringstream out;
    const auto& segments = sim.trace().segments();
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const TraceSegment& s = segments.at(i);
        Tick end = s.end;
        while (i + 1 < segments.size() && segments.at(i + 1).jobId == s.jobId &&
               segments.at(i + 1).start == end)
            end = segments.at(++i).end;
        if (out.tellp() > 0) out << ' ';
        out << (s.jobId ? jobLabel(sim.registry(), *s.jobId) : std::string("idle"))
            << '[' << s.start << ',' << end << ')';
    }
    return out.str();
}

std::string wakeOrder(const Simulation& sim) {
    std::ostringstream out;
    const auto& events = sim.trace().events();
    bool first = true;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const TraceEvent& e = events.at(i);
        if (e.type != EventType::Wake || !e.jobId) continue;
        out << (first ? "" : " ") << jobLabel(sim.registry(), *e.jobId) << '@' << e.tick;
        first = false;
    }
    return out.str();
}

std::string outcomes(const Simulation& sim) {
    std::ostringstream out;
    const Registry& r = sim.registry();
    for (JobId id = 1; id <= static_cast<JobId>(r.jobCount()); ++id) {
        const Job& j = *r.findJob(id);
        out << (id > 1 ? " " : "") << jobLabel(r, id) << ':';
        if (j.completionTick) out << *j.completionTick;
        else out << "open";
        if (j.deadlineMissRecorded) out << '!' << *j.deadlineMissTick;
    }
    return out.str();
}

// Overload's golden trace states properties, not a timeline.
std::string properties(const Simulation& sim, bool& holds) {
    std::size_t misses = 0;
    for (JobId id = 1; id <= static_cast<JobId>(sim.registry().jobCount()); ++id)
        misses += sim.registry().findJob(id)->deadlineMissRecorded ? 1 : 0;
    Tick idle = 0;
    const auto& segments = sim.trace().segments();
    for (std::size_t i = 0; i < segments.size(); ++i)
        if (!segments.at(i).jobId) idle += segments.at(i).end - segments.at(i).start;
    holds = sim.registry().jobCount() == 9 && misses > 0 && idle == 0;
    return std::to_string(sim.registry().jobCount()) + " jobs, misses " + std::to_string(misses) +
           ", idle " + std::to_string(idle);
}

struct Measured {
    std::string main;     // Same kind as Golden::expected, or the run error.
    std::string outcome;
    bool valid = false;
    bool propertiesHold = false;
};

Measured measure(const Golden& g, TimerBackend timer) {
    static ScenarioInput input;  // Large fixed arrays: keep them off the stack.
    findPreset(g.scenario)->build(input);
    input.scenario.config.policy = g.policy;
    input.scenario.config.inheritance = g.inheritance;
    input.scenario.config.timerBackend = timer;
    Measured m;
    try {
        Simulation sim(input);
        if (!sim.run()) {
            m.main = "run invalid: " + sim.error();
            return m;
        }
        m.valid = true;
        m.outcome = outcomes(sim);
        switch (g.kind) {
        case Kind::Cpu: m.main = cpuTrace(sim); break;
        case Kind::Wakes: m.main = wakeOrder(sim); break;
        case Kind::Properties: m.main = properties(sim, m.propertiesHold); break;
        }
    } catch (const std::exception& e) {
        m.main = std::string("refused: ") + e.what();
    }
    return m;
}

bool matches(const Golden& g, const Measured& m) {
    if (!m.valid) return false;
    if (g.kind == Kind::Properties) return m.propertiesHold;
    return m.main == g.expected && (*g.outcome == '\0' || m.outcome == g.outcome);
}

} // namespace

int main() {
    std::cout << "| Scenario | Policy | Inheritance | Golden | Measured | Golden outcome | Measured outcome "
                 "| Backends agree | Match |\n"
                 "| --- | --- | --- | --- | --- | --- | --- | --- | --- |\n";
    std::size_t matched = 0;
    const std::size_t rows = sizeof(kGolden) / sizeof(kGolden[0]);
    for (const Golden& g : kGolden) {
        const Measured heap = measure(g, TimerBackend::Heap);
        const Measured list = measure(g, TimerBackend::SortedList);
        const bool agree = heap.main == list.main && heap.outcome == list.outcome;
        const bool ok = agree && matches(g, heap) && matches(g, list);
        matched += ok ? 1 : 0;
        std::cout << "| `" << g.scenario << "` | " << toString(g.policy) << " | "
                  << (g.inheritance ? "on" : "off") << " | " << g.expected << " | " << heap.main
                  << " | " << (*g.outcome ? g.outcome : "not stated") << " | "
                  << (heap.outcome.empty() ? "-" : heap.outcome) << " | " << (agree ? "yes" : "NO")
                  << " | " << (ok ? "yes" : "**NO**") << " |\n";
    }
    std::cout << "\n" << matched << " of " << rows << " golden traces match on both timer backends.\n";
    return matched == rows ? 0 : 1;
}
