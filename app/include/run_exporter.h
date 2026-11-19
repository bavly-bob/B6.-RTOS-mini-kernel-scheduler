#ifndef B6_APP_RUN_EXPORTER_H
#define B6_APP_RUN_EXPORTER_H

#include "run_trace.h"

#include <iosfwd>
#include <string>

namespace b6 {

// Identity of one exported run ("run" object in docs/run-schema-v1.md).
struct RunInfo {
    enum class Evidence { EngineRun, DesignFixture };
    std::string runId;                       // e.g. "inversion-on"
    std::string label;                       // Human-readable title.
    Evidence evidence = Evidence::EngineRun; // DesignFixture for hand-made data.
    std::optional<std::string> engineRevision; // Defaults to the build's git commit.
    std::optional<std::string> createdAt;      // ISO-8601 text, or null.
};

// Per-task outcome, computed by the metrics code (Marc). The exporter only
// serializes it. Unavailable values stay nullopt and become JSON null.
struct TaskMetrics {
    TaskId taskId = 0;
    std::size_t completedSamples = 0;
    std::optional<Tick> responseMin;
    std::optional<double> responseMean;
    std::optional<Tick> responseMax;
    std::optional<Tick> jitter;       // max - min; nullopt with < 2 samples.
    std::size_t misses = 0;
};

// Whole-run outcome ("metrics" object). Definitions: plan p.26.
struct RunMetrics {
    Tick busyTicks = 0;
    Tick observedTicks = 0;
    std::optional<double> busyPercent;
    std::size_t completedJobs = 0;
    std::size_t unfinishedJobs = 0;
    std::size_t deadlineMisses = 0;
    std::optional<double> demandUtilization;  // sum(Ci/Ti) for periodic sets.
    std::optional<double> rmBound;            // n(2^(1/n) - 1) when applicable.
    bool rmBoundApplicable = false;
    std::string rmBoundNote;                  // Why it is (not) applicable.
    TaskMetrics perTask[kMaxTasks]{};
    std::size_t taskCount = 0;
};

// Writes one complete schemaVersion 1 run file to `out`.
// - tasks/jobs come from the registry (final job states);
// - segments/events/snapshots and run status come from the trace;
// - metrics may be null: "overall" is then {} and a warning is written.
// Never writes C++ pointers; unknown times are null; all text is escaped.
void exportRun(std::ostream& out, const RunInfo& info, const Registry& registry,
               const RunTrace& trace, const RunMetrics* metrics);

// Same, to a file. Returns "" on success or an actionable error message
// (cannot open/write the file, or the file exceeds the dashboard's 32 MiB limit).
std::string exportRunToFile(const std::string& path, const RunInfo& info,
                            const Registry& registry, const RunTrace& trace,
                            const RunMetrics* metrics);

// Schema spellings used in the run file.
const char* toString(Policy policy);
const char* toString(TimerBackend backend);
std::string scriptText(const TaskSpec& task);  // e.g. "LOCK 1; RUN 4; UNLOCK 1; END"

constexpr std::size_t kMaxRunFileBytes = 32u * 1024u * 1024u;

} // namespace b6
#endif
