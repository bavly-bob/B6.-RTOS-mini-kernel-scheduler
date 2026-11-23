#include "metrics.h"

#include <cmath>

namespace b6 {
namespace {

Tick cpuPerJob(const TaskSpec& task) {
    Tick total = 0;
    for (std::size_t i = 0; i < task.scriptLength; ++i)
        if (task.script[i].op == OpCode::Run) total += task.script[i].durationTicks;
    return total;
}

bool sleeps(const TaskSpec& task) {
    for (std::size_t i = 0; i < task.scriptLength; ++i)
        if (task.script[i].op == OpCode::Sleep) return true;
    return false;
}

// Empty when the Liu and Layland bound applies to this run.
std::string boundNotApplicableReason(const Scenario& s) {
    if (s.taskCount == 0) return "no tasks";
    for (std::size_t i = 0; i < s.taskCount; ++i)
        if (s.tasks[i].periodTicks == 0) return "one-shot tasks";
    if (s.mutexCount != 0 || s.semaphoreCount != 0) return "tasks share mutexes or semaphores";
    for (std::size_t i = 0; i < s.taskCount; ++i) {
        if (sleeps(s.tasks[i])) return "tasks sleep, so they are not independent";
        if (s.tasks[i].relativeDeadline != s.tasks[i].periodTicks) return "deadlines differ from periods";
    }
    if (s.config.policy != Policy::RateMonotonic)
        return std::string("policy is ") + toString(s.config.policy) + ", not rate_monotonic";
    return {};
}

} // namespace

RunMetrics computeMetrics(const Registry& registry, const RunTrace& trace) {
    const Scenario& s = registry.scenario();
    RunMetrics m;

    const auto& segments = trace.segments();
    for (std::size_t i = 0; i < segments.size(); ++i)
        if (segments.at(i).jobId) m.busyTicks += segments.at(i).end - segments.at(i).start;
    m.observedTicks = s.config.horizonTicks;
    if (m.observedTicks > 0)
        m.busyPercent = 100.0 * static_cast<double>(m.busyTicks) / static_cast<double>(m.observedTicks);

    m.taskCount = s.taskCount;
    Tick responseSum[kMaxTasks]{};
    for (std::size_t t = 0; t < s.taskCount; ++t) m.perTask[t].taskId = s.tasks[t].taskId;

    for (JobId id = 1; id <= static_cast<JobId>(registry.jobCount()); ++id) {
        const Job& job = *registry.findJob(id);
        std::size_t t = 0;
        while (s.tasks[t].taskId != job.taskId) ++t;  // Every job has a known task.
        TaskMetrics& tm = m.perTask[t];
        if (job.deadlineMissRecorded) {
            ++m.deadlineMisses;
            ++tm.misses;
        }
        if (!job.completionTick) {
            ++m.unfinishedJobs;
            continue;
        }
        ++m.completedJobs;
        const Tick response = *job.completionTick - job.releaseTick;
        if (!tm.responseMin || response < *tm.responseMin) tm.responseMin = response;
        if (!tm.responseMax || response > *tm.responseMax) tm.responseMax = response;
        responseSum[t] += response;
        ++tm.completedSamples;
    }
    for (std::size_t t = 0; t < s.taskCount; ++t) {
        TaskMetrics& tm = m.perTask[t];
        if (tm.completedSamples == 0) continue;
        tm.responseMean = static_cast<double>(responseSum[t]) / static_cast<double>(tm.completedSamples);
        if (tm.completedSamples >= 2) tm.jitter = *tm.responseMax - *tm.responseMin;
    }

    bool periodic = s.taskCount > 0;
    double demand = 0.0;
    for (std::size_t t = 0; t < s.taskCount && periodic; ++t) {
        if (s.tasks[t].periodTicks == 0) periodic = false;
        else demand += static_cast<double>(cpuPerJob(s.tasks[t])) / static_cast<double>(s.tasks[t].periodTicks);
    }
    if (periodic) m.demandUtilization = demand;

    m.rmBoundNote = boundNotApplicableReason(s);
    if (m.rmBoundNote.empty()) {
        const double n = static_cast<double>(s.taskCount);
        m.rmBound = n * (std::pow(2.0, 1.0 / n) - 1.0);
        m.rmBoundApplicable = true;
        if (demand <= *m.rmBound) m.rmBoundNote = "demand within the bound: every deadline is met";
        else if (demand <= 1.0) m.rmBoundNote = "demand above the bound is inconclusive";
        else m.rmBoundNote = "demand above 100%: overload, misses are unavoidable over a long enough run";
    }
    return m;
}

} // namespace b6
