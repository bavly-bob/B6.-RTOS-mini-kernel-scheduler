// b6_sim: run one preset scenario through the C++ scheduler and export the
// run file that the dashboard imports.
//
//   b6_sim --list
//   b6_sim --scenario rr_preemption [--policy fixed_priority|rate_monotonic|edf]
//          [--timer sorted_list|heap] [--inheritance off|on] [--quantum N]
//          [--output runs/name.json]
//
// Exit codes: 0 run ok and exported; 1 run invalid (still exported, so the
// problem can be inspected); 2 bad command line or invalid scenario.

#include "presets.h"
#include "simulation.h"

#include <filesystem>
#include <iostream>
#include <string>

using namespace b6;

namespace {

void usage(std::ostream& out) {
    out << "usage: b6_sim --list\n"
           "       b6_sim --scenario ID [--policy fixed_priority|rate_monotonic|edf]\n"
           "              [--timer sorted_list|heap]\n"
           "              [--inheritance off|on] [--quantum N] [--output FILE]\n";
}

void listPresets() {
    std::cout << "Scenarios:\n";
    for (std::size_t i = 0; i < presetCount(); ++i) {
        const Preset& p = presetAt(i);
        std::cout << "  " << p.id << std::string(20 - std::string(p.id).size(), ' ')
                  << p.description << '\n';
    }
}

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

void printSummary(const Simulation& sim) {
    const Registry& registry = sim.registry();
    std::cout << "CPU:";
    const auto& segments = sim.trace().segments();
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const TraceSegment& s = segments.at(i);
        std::cout << ' ' << (s.jobId ? jobLabel(registry, *s.jobId) : std::string("idle"))
                  << '[' << s.start << ',' << s.end << ')';
    }
    std::cout << "\nJobs:\n";
    for (JobId id = 1; id <= static_cast<JobId>(registry.jobCount()); ++id) {
        const Job& j = *registry.findJob(id);
        std::cout << "  " << jobLabel(registry, id) << " (job " << id << ")"
                  << "  released " << j.releaseTick << ", deadline " << j.absoluteDeadline
                  << ", cpu " << j.totalCpuUsed << ", ";
        if (j.completionTick) std::cout << "completed " << *j.completionTick
                                        << " (response " << *j.completionTick - j.releaseTick << ")";
        else std::cout << "not completed (" << toString(j.state) << ")";
        if (j.deadlineMissRecorded) std::cout << ", MISSED deadline at " << *j.deadlineMissTick;
        std::cout << '\n';
    }

    const std::optional<RunMetrics> m = sim.metrics();
    if (!m) return;
    std::cout << "Metrics:\n  CPU busy " << m->busyTicks << " of " << m->observedTicks << " ticks ("
              << *m->busyPercent << "%), completed " << m->completedJobs << ", unfinished "
              << m->unfinishedJobs << ", deadline misses " << m->deadlineMisses << '\n';
    std::cout << "  demand ";
    if (m->demandUtilization) std::cout << *m->demandUtilization * 100 << '%';
    else std::cout << "not applicable";
    std::cout << ", RM bound ";
    if (m->rmBound) std::cout << *m->rmBound * 100 << '%';
    else std::cout << "not applicable";
    std::cout << " (" << m->rmBoundNote << ")\n";
    for (std::size_t i = 0; i < m->taskCount; ++i) {
        const TaskMetrics& t = m->perTask[i];
        std::cout << "  " << registry.findTask(t.taskId)->name << ": " << t.completedSamples << " completed";
        if (t.completedSamples > 0)
            std::cout << ", response min " << *t.responseMin << " mean " << *t.responseMean
                      << " max " << *t.responseMax;
        std::cout << ", jitter ";
        if (t.jitter) std::cout << *t.jitter;
        else std::cout << "insufficient samples";
        std::cout << ", misses " << t.misses << '\n';
    }
}

bool parsePolicy(const std::string& text, Policy& out) {
    if (text == "fixed_priority") out = Policy::FixedPriority;
    else if (text == "edf") out = Policy::EDF;
    else if (text == "rate_monotonic") out = Policy::RateMonotonic;
    else return false;
    return true;
}

bool parseTimer(const std::string& text, TimerBackend& out) {
    if (text == "sorted_list") out = TimerBackend::SortedList;
    else if (text == "heap") out = TimerBackend::Heap;
    else return false;
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string scenarioId, output;
    std::optional<Policy> policy;
    std::optional<TimerBackend> timer;
    std::optional<bool> inheritance;
    std::optional<Tick> quantum;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        std::string v;
        if (arg == "--list") { listPresets(); return 0; }
        if (arg == "--help" || arg == "-h") { usage(std::cout); return 0; }
        if (arg == "--scenario" && value(v)) { scenarioId = v; continue; }
        if (arg == "--output" && value(v)) { output = v; continue; }
        if (arg == "--policy" && value(v)) {
            Policy p;
            if (!parsePolicy(v, p)) { std::cerr << "unknown policy: " << v << '\n'; return 2; }
            policy = p;
            continue;
        }
        if (arg == "--timer" && value(v)) {
            TimerBackend t;
            if (!parseTimer(v, t)) { std::cerr << "unknown timer backend: " << v << '\n'; return 2; }
            timer = t;
            continue;
        }
        if (arg == "--inheritance" && value(v)) {
            if (v != "on" && v != "off") { std::cerr << "--inheritance takes on or off\n"; return 2; }
            inheritance = v == "on";
            continue;
        }
        if (arg == "--quantum" && value(v)) {
            try { quantum = std::stoll(v); } catch (...) { quantum = 0; }
            continue;
        }
        std::cerr << "unrecognized or incomplete option: " << arg << '\n';
        usage(std::cerr);
        return 2;
    }
    if (scenarioId.empty()) { usage(std::cerr); return 2; }

    const Preset* preset = findPreset(scenarioId);
    if (!preset) {
        std::cerr << "unknown scenario: " << scenarioId << " (see --list)\n";
        return 2;
    }

    static ScenarioInput input;  // Large fixed arrays: keep them off the stack.
    preset->build(input);
    RunConfig& config = input.scenario.config;
    if (policy) config.policy = *policy;
    if (timer) config.timerBackend = *timer;
    if (inheritance) config.inheritance = *inheritance;
    if (quantum) config.quantumTicks = *quantum;

    std::string runId = scenarioId + "-" + toString(config.policy) + "-" + toString(config.timerBackend);
    if (config.inheritance) runId += "-inheritance";
    if (output.empty()) output = "runs/" + runId + ".json";

    try {
        Simulation sim(input);
        const bool ok = sim.run();

        std::cout << preset->id << ": " << preset->description << '\n'
                  << "policy " << toString(config.policy) << ", timer " << toString(config.timerBackend)
                  << ", inheritance " << (config.inheritance ? "on" : "off")
                  << ", quantum " << config.quantumTicks << ", horizon " << config.horizonTicks << '\n';
        printSummary(sim);
        if (!ok) std::cout << "RUN INVALID: " << sim.error() << '\n';

        const std::filesystem::path parent = std::filesystem::path(output).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent);
        RunInfo info;
        info.runId = runId;
        info.label = preset->description;
        info.evidence = RunInfo::Evidence::EngineRun;
        const std::string error = sim.exportTo(output, info);
        if (!error.empty()) {
            std::cerr << "export failed: " << error << '\n';
            return 1;
        }
        std::cout << "wrote " << output << '\n';
        return ok ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "cannot run " << scenarioId << ": " << e.what() << '\n';
        return 2;
    }
}
