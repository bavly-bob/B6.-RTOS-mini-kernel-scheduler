#ifndef B6_APP_SIMULATION_H
#define B6_APP_SIMULATION_H

#include "run_exporter.h"
#include "scheduler.h"

#include <optional>
#include <string>

namespace b6 {

// Everything one run needs as input: the scenario plus its timed
// suspend/resume controls (controls are not part of a task script).
struct ScenarioInput {
    Scenario scenario;
    ControlAction controls[kMaxControls]{};
    std::size_t controlCount = 0;
};

// Owns one run's components in the order docs/timers-and-export.md requires:
// Registry -> RunTrace -> Scheduler -> ResourceManager -> TimerManager.
// Members are destroyed in reverse, so nothing outlives the registry and the
// resource manager is gone before the scheduler (its hooks).
class Simulation {
public:
    // Throws std::invalid_argument for invalid input (scenario, controls,
    // or a mode a collaborator does not support yet, such as inheritance).
    explicit Simulation(const ScenarioInput& input);
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    // Runs boundaries 0..horizon once. Returns false if the engine stopped on
    // an error; the trace is then marked invalid and error() says why.
    bool run();

    const std::string& error() const noexcept { return trace_.error(); }
    const Registry& registry() const noexcept { return registry_; }
    const RunTrace& trace() const noexcept { return trace_; }
    const Scheduler& scheduler() const noexcept { return scheduler_; }

    // Metrics of a finished, valid run (metrics.h). An invalid run has none:
    // its numbers would describe a trace that stopped early.
    std::optional<RunMetrics> metrics() const;

    // Writes the run file (schemaVersion 1) as an engine_run, with metrics()
    // when there are any; otherwise the exporter writes {} and a warning.
    // Returns "" on success.
    std::string exportTo(const std::string& path, const RunInfo& info) const;
    void exportTo(std::ostream& out, const RunInfo& info) const;

private:
    Registry registry_;
    RunTrace trace_;
    Scheduler scheduler_;
    std::optional<ResourceManager> resources_;
    TimerManager timers_;
};

} // namespace b6
#endif
