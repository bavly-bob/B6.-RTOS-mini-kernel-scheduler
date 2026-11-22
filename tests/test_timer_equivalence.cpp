// Timer equivalence through the whole engine (plan p.25, owner: Sarofem).
//
// The two required timer backends (sorted list and min-heap) must produce the
// same run: the same WAKE events in the same order, and therefore the same
// schedule, snapshots and outcomes. tests/test_timer_manager.cpp checks this
// for the TimerManager alone; this suite checks it end to end:
//   1. every preset, policy and inheritance setting the engine accepts;
//   2. seeded stress scenarios with many sleepers, equal wake ticks, several
//      timers due at once and sleepers that are suspended when they wake.
// For each pair the exported run files must be byte-for-byte identical once
// the "timerBackend" value is set to the same text.
#include "presets.h"
#include "validation.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace b6;

#define CHECK(condition) do { if (!(condition)) { \
    std::cerr << "Failed: " #condition " at line " << __LINE__ << '\n'; \
    std::exit(1); } } while (false)

namespace {

struct RunResult {
    std::string json;       // Exported run file, backend name normalized.
    std::size_t wakes = 0;  // WAKE events with reason "timer".
    std::size_t suspendedWakes = 0;
    std::size_t multiWakeTicks = 0;  // Ticks where two or more timers fired.
    bool valid = false;
};

RunResult runOnce(const ScenarioInput& input, TimerBackend backend, const std::string& runId) {
    static ScenarioInput copy;  // Large fixed arrays: keep them off the stack.
    copy = input;
    copy.scenario.config.timerBackend = backend;
    auto sim = std::make_unique<Simulation>(copy);
    RunResult r;
    r.valid = sim->run();

    const auto& events = sim->trace().events();
    Tick lastTick = -1;
    std::size_t atTick = 0;
    for (std::size_t i = 0; i < events.size(); ++i) {
        const TraceEvent& e = events.at(i);
        if (e.type != EventType::Wake || std::string(e.reason) != "timer") continue;
        ++r.wakes;
        for (std::size_t d = 0; d < e.dataCount; ++d)
            if (std::string(e.data[d].key) == "suspended" && e.data[d].value) ++r.suspendedWakes;
        atTick = e.tick == lastTick ? atTick + 1 : 1;
        if (atTick == 2) ++r.multiWakeTicks;
        lastTick = e.tick;
    }

    RunInfo info;
    info.runId = runId;
    info.label = runId;
    info.engineRevision = "test";  // Fixed, so the two files compare byte for byte.
    std::ostringstream out;
    sim->exportTo(out, info);
    r.json = out.str();
    const std::string from = std::string("\"timerBackend\":\"") + toString(backend) + "\"";
    const std::size_t at = r.json.find(from);
    CHECK(at != std::string::npos);
    r.json.replace(at, from.size(), "\"timerBackend\":\"<backend>\"");
    return r;
}

// Runs `input` on both backends and requires identical exported runs.
RunResult compareBackends(const ScenarioInput& input, const std::string& runId) {
    const RunResult list = runOnce(input, TimerBackend::SortedList, runId);
    const RunResult heap = runOnce(input, TimerBackend::Heap, runId);
    if (list.json != heap.json) {
        std::size_t i = 0;
        while (i < list.json.size() && i < heap.json.size() && list.json[i] == heap.json[i]) ++i;
        const std::size_t from = i < 120 ? 0 : i - 120;
        std::cerr << runId << ": sorted_list and heap runs differ at byte " << i
                  << "\n  sorted_list: ..." << list.json.substr(from, 240)
                  << "\n  heap:        ..." << heap.json.substr(from, 240) << '\n';
        std::exit(1);
    }
    CHECK(list.wakes == heap.wakes && list.valid == heap.valid);
    return list;
}

const char* name(Policy p) { return toString(p); }

// 1. Every preset under every policy/inheritance combination the engine accepts.
void testAllPresets() {
    static ScenarioInput input;
    std::size_t compared = 0, wakes = 0;
    for (std::size_t i = 0; i < presetCount(); ++i) {
        const Preset& preset = presetAt(i);
        for (Policy policy : {Policy::FixedPriority, Policy::EDF, Policy::RateMonotonic}) {
            for (bool inheritance : {false, true}) {
                preset.build(input);
                input.scenario.config.policy = policy;
                input.scenario.config.inheritance = inheritance;
                if (!validateScenario(input.scenario).empty()) continue;
                const std::string runId = std::string(preset.id) + "-" + name(policy) +
                                          (inheritance ? "-pi" : "");
                try {
                    wakes += compareBackends(input, runId).wakes;
                    ++compared;
                } catch (const std::invalid_argument&) {
                    // A mode a collaborator has not enabled yet (refused
                    // before running, identically for both backends).
                }
            }
        }
    }
    CHECK(compared >= presetCount());  // At least fixed priority for every preset.
    CHECK(wakes > 0);                  // sleep_order and suspend_resume use timers.
    std::cout << "PASS: " << compared << " preset runs identical on both timer backends ("
              << wakes << " timer wakes)\n";
}

// 2. Seeded stress scenarios: periodic tasks that sleep between CPU bursts,
// plus two long one-shot sleepers that are suspended and resumed while asleep.
std::uint64_t g_state = 0;
Tick draw(Tick low, Tick high) {  // xorshift64: same values on every compiler.
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return low + static_cast<Tick>(g_state % static_cast<std::uint64_t>(high - low + 1));
}

void addTask(Scenario& s, const std::string& taskName, int priority, Tick release, Tick period,
             Tick deadline, std::initializer_list<Instruction> script) {
    TaskSpec& t = s.tasks[s.taskCount];
    t.taskId = static_cast<TaskId>(s.taskCount);
    ++s.taskCount;
    t.name = taskName;
    t.basePriority = priority;
    t.firstRelease = release;
    t.periodTicks = period;
    t.relativeDeadline = deadline;
    t.scriptLength = 0;
    for (const Instruction& step : script) t.script[t.scriptLength++] = step;
}

Instruction run(Tick n) { return {OpCode::Run, n, std::nullopt}; }
Instruction sleep(Tick n) { return {OpCode::Sleep, n, std::nullopt}; }
Instruction end() { return {OpCode::End, 0, std::nullopt}; }

void buildStress(ScenarioInput& in, std::uint64_t seed) {
    in = ScenarioInput{};
    g_state = 0x9E3779B97F4A7C15ULL ^ (seed * 0x100000001B3ULL);
    Scenario& s = in.scenario;
    s.config.scenarioId = "timer_stress";
    s.config.horizonTicks = 300;
    s.config.policy = Policy::FixedPriority;  // RM/EDF refuse SLEEP (independent jobs only).
    s.config.quantumTicks = static_cast<int>(draw(1, 3));
    const Tick periods[] = {30, 40, 50, 60, 75, 100};
    for (int i = 0; i < 10; ++i) {
        const Tick period = periods[draw(0, 5)];
        // Small sleep range, so equal wake ticks and batches are common.
        addTask(s, "P" + std::to_string(i), static_cast<int>(draw(1, 6)), draw(0, 9), period, period,
                {run(draw(1, 3)), sleep(draw(1, 12)), run(draw(1, 2)), sleep(draw(1, 12)), run(1), end()});
    }
    // Long one-shot sleepers; their sleeps add up past the last control tick,
    // so each control finds an unfinished job (sleeping or ready).
    addTask(s, "Z1", 7, 0, 0, 300,
            {sleep(7), run(2), sleep(13), run(1), sleep(9), run(3), sleep(25), run(1), sleep(30),
             run(2), sleep(40), end()});
    addTask(s, "Z2", 2, 3, 0, 300,
            {sleep(draw(5, 15)), run(1), sleep(draw(20, 30)), run(1), sleep(draw(30, 50)), run(1),
             sleep(draw(40, 60)), end()});
    const TaskId z1 = 10, z2 = 11;
    const struct { Tick tick; TaskId task; ControlAction::Kind kind; } controls[] = {
        {5, z1, ControlAction::Kind::Suspend},   // Z1 asleep until 7: wakes suspended.
        {6, z2, ControlAction::Kind::Suspend},
        {12, z1, ControlAction::Kind::Resume},
        {40, z2, ControlAction::Kind::Resume},
        {60, z1, ControlAction::Kind::Suspend},
        {100, z1, ControlAction::Kind::Resume},
    };
    for (const auto& c : controls) in.controls[in.controlCount++] = {c.tick, c.task, c.kind};
}

void testStressScenarios() {
    static ScenarioInput input;
    std::size_t runs = 0, wakes = 0, suspendedWakes = 0, multiWakeTicks = 0;
    for (std::uint64_t seed = 1; seed <= 40; ++seed) {
        buildStress(input, seed);
        const std::string problem = validateScenario(input.scenario);
        if (!problem.empty()) {
            std::cerr << "stress seed " << seed << " rejected: " << problem << '\n';
            std::exit(1);
        }
        const std::string runId = "timer_stress-" + std::to_string(seed);
        const RunResult r = compareBackends(input, runId);
        if (!r.valid) {
            std::cerr << runId << " stopped as an invalid run\n";
            std::exit(1);
        }
        ++runs;
        wakes += r.wakes;
        suspendedWakes += r.suspendedWakes;
        multiWakeTicks += r.multiWakeTicks;
    }
    // The workloads really exercised the hard cases, not only single sleeps.
    CHECK(wakes > 1000);
    CHECK(multiWakeTicks > 50);
    CHECK(suspendedWakes > 0);
    std::cout << "PASS: " << runs << " stress runs identical on both timer backends (" << wakes
              << " wakes, " << multiWakeTicks << " ticks with several due timers, " << suspendedWakes
              << " suspended sleepers woken)\n";
}

} // namespace

int main() {
    testAllPresets();
    testStressScenarios();
    std::cout << "All timer equivalence tests passed.\n";
    return 0;
}
