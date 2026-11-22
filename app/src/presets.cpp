#include "presets.h"

#include <initializer_list>
#include <stdexcept>

namespace b6 {
namespace {

// Script-step helpers so presets read like the plan's task descriptions.
Instruction RUN(Tick n) { return {OpCode::Run, n, std::nullopt}; }
Instruction SLEEP(Tick n) { return {OpCode::Sleep, n, std::nullopt}; }
Instruction LOCK(ResourceId r) { return {OpCode::Lock, 0, r}; }
Instruction UNLOCK(ResourceId r) { return {OpCode::Unlock, 0, r}; }
Instruction WAIT(ResourceId r) { return {OpCode::Wait, 0, r}; }
Instruction SIGNAL(ResourceId r) { return {OpCode::Signal, 0, r}; }
Instruction END() { return {OpCode::End, 0, std::nullopt}; }

void begin(ScenarioInput& in, const char* id, Tick horizon, Policy policy = Policy::FixedPriority) {
    in = ScenarioInput{};
    in.scenario.config.scenarioId = id;
    in.scenario.config.horizonTicks = horizon;
    in.scenario.config.policy = policy;
    in.scenario.config.quantumTicks = 1;
}

void task(ScenarioInput& in, TaskId id, const char* name, int priority, Tick release,
          Tick period, Tick deadline, std::initializer_list<Instruction> script) {
    Scenario& s = in.scenario;
    if (s.taskCount == kMaxTasks || script.size() > kMaxScriptLength)
        throw std::length_error("preset exceeds task or script capacity");
    TaskSpec& t = s.tasks[s.taskCount++];
    t.taskId = id;
    t.name = name;
    t.basePriority = priority;
    t.firstRelease = release;
    t.periodTicks = period;
    t.relativeDeadline = deadline;
    t.scriptLength = 0;
    for (const Instruction& step : script) t.script[t.scriptLength++] = step;
}

void control(ScenarioInput& in, Tick tick, TaskId taskId, ControlAction::Kind kind) {
    if (in.controlCount == kMaxControls) throw std::length_error("preset exceeds control capacity");
    in.controls[in.controlCount++] = {tick, taskId, kind};
}

// A. Two equal-priority peers share the CPU; an urgent job arrives at t=2.
// Expected: A[0,1) B[1,2) H[2,3) A[3,4) B[4,5) A[5,6) B[6,7); H=3, A=6, B=7.
void rrPreemption(ScenarioInput& in) {
    begin(in, "rr_preemption", 8);
    task(in, 0, "A", 5, 0, 0, 10, {RUN(3), END()});
    task(in, 1, "B", 5, 0, 0, 10, {RUN(3), END()});
    task(in, 2, "H", 8, 2, 0, 2, {RUN(1), END()});
}

// C. Four jobs sleep at t=100 for 30/10/20/10 ticks. Expected wake order:
// B@110, D@110 (inserted after B), C@120, A@130; sleepers use no CPU.
void sleepOrder(ScenarioInput& in) {
    begin(in, "sleep_order", 140);
    task(in, 0, "A", 4, 100, 0, 40, {SLEEP(30), RUN(1), END()});
    task(in, 1, "B", 4, 100, 0, 40, {SLEEP(10), RUN(1), END()});
    task(in, 2, "C", 4, 100, 0, 40, {SLEEP(20), RUN(1), END()});
    task(in, 3, "D", 4, 100, 0, 40, {SLEEP(10), RUN(1), END()});
}

// Suspension of a ready job and of a sleeping job. S sleeps until 5 but is
// suspended at 2; its timer fires at 5 while suspended, so it stays off the
// CPU until RESUME at 7. X is suspended while ready at 1 and resumed at 3.
// Expected: X[0,1) idle[1,3) X[3,6) idle[6,7) S[7,9) idle[9,10); X=6, S=9.
void suspendResume(ScenarioInput& in) {
    begin(in, "suspend_resume", 10);
    task(in, 0, "S", 5, 0, 0, 10, {SLEEP(5), RUN(2), END()});
    task(in, 1, "X", 3, 0, 0, 10, {RUN(4), END()});
    control(in, 1, 1, ControlAction::Kind::Suspend);
    control(in, 2, 0, ControlAction::Kind::Suspend);
    control(in, 3, 1, ControlAction::Kind::Resume);
    control(in, 7, 0, ControlAction::Kind::Resume);
}

// D. Consumer C waits on S (count 0) until producer P signals.
// Expected: P[0,2) C[2,3) P[3,4); C completes 3, P completes 4.
void semaphoreSignal(ScenarioInput& in) {
    begin(in, "semaphore_signal", 5);
    in.scenario.semaphores[0] = {0, 0, 1};
    in.scenario.semaphoreCount = 1;
    task(in, 0, "C", 3, 0, 0, 10, {WAIT(0), RUN(1), END()});
    task(in, 1, "P", 1, 0, 0, 10, {RUN(2), SIGNAL(0), RUN(1), END()});
}

// B. L holds mutex R, H blocks on it, M runs in between.
// Inheritance OFF expected: L[0,2) M[2,6) L[6,8) H[8,9); H completes 9 and
// misses its deadline at 6. Inheritance ON needs Moataz's donation code.
void priorityInversion(ScenarioInput& in) {
    begin(in, "priority_inversion", 10);
    in.scenario.mutexes[0] = {0};
    in.scenario.mutexCount = 1;
    task(in, 0, "L", 1, 0, 0, 20, {LOCK(0), RUN(4), UNLOCK(0), END()});
    task(in, 1, "H", 3, 1, 0, 5, {LOCK(0), RUN(1), UNLOCK(0), END()});
    task(in, 2, "M", 2, 2, 0, 20, {RUN(4), END()});
}

// E. Periodic A (C=2, T=D=5) and B (C=4, T=D=7), horizon 8. Default policy
// EDF: A0[0,2) B0[2,6) A1[6,8); B0 completes 6 (deadline 7 met). Under
// rate_monotonic, A (shorter period) gets 31 and B 30: A0[0,2) B0[2,5)
// A1[5,7) B0[7,8), B0 completes 8 and misses at 7. The base priorities below
// follow period order too, so fixed_priority gives the same CPU trace.
void rmEdfCompare(ScenarioInput& in) {
    begin(in, "rm_edf_compare", 8, Policy::EDF);
    task(in, 0, "A", 2, 0, 5, 5, {RUN(2), END()});
    task(in, 1, "B", 1, 0, 7, 7, {RUN(4), END()});
}

// Demand 3/4 + 3/5 = 135% of the CPU: misses and backlog must be visible,
// and every release must still appear as its own job.
void overload(ScenarioInput& in) {
    begin(in, "overload", 20);
    task(in, 0, "A", 2, 0, 4, 4, {RUN(3), END()});
    task(in, 1, "B", 1, 0, 5, 5, {RUN(3), END()});
}

const Preset kPresets[] = {
    {"rr_preemption", "Round-robin peers plus a higher-priority arrival", rrPreemption},
    {"sleep_order", "Four sleepers wake in timer order", sleepOrder},
    {"suspend_resume", "Suspending a ready job and a sleeping job", suspendResume},
    {"semaphore_signal", "Consumer waits until the producer signals", semaphoreSignal},
    {"priority_inversion", "L holds a mutex, H blocks, M interferes", priorityInversion},
    {"rm_edf_compare", "Periodic A and B under RM or EDF", rmEdfCompare},
    {"overload", "Demand above 100%: deadline misses and backlog", overload},
};

} // namespace

std::size_t presetCount() { return sizeof(kPresets) / sizeof(kPresets[0]); }

const Preset& presetAt(std::size_t index) {
    if (index >= presetCount()) throw std::out_of_range("preset index out of range");
    return kPresets[index];
}

const Preset* findPreset(const std::string& id) {
    for (const Preset& p : kPresets)
        if (id == p.id) return &p;
    return nullptr;
}

} // namespace b6
