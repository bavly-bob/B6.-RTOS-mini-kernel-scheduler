#include "simulation.h"
#include "metrics.h"

#include <stdexcept>

namespace b6 {

Simulation::Simulation(const ScenarioInput& input)
    : registry_(input.scenario),  // Validates and copies the scenario.
      trace_(registry_.scenario().config),
      scheduler_(registry_, trace_, input.controls, input.controlCount),
      timers_(registry_) {
    // The resource manager supports fixed priority only (the validator keeps
    // RM/EDF scenarios resource-free). Its constructor rejects modes it does
    // not implement yet, e.g. inheritance=true, with std::invalid_argument.
    if (registry_.scenario().config.policy == Policy::FixedPriority)
        resources_.emplace(registry_, scheduler_);
    scheduler_.attach(resources_ ? &*resources_ : nullptr, timers_);
}

bool Simulation::run() {
    try {
        scheduler_.run();
    } catch (const std::exception&) {
        return false;  // Scheduler::run already marked the trace invalid.
    }
    return trace_.isValid();
}

std::optional<RunMetrics> Simulation::metrics() const {
    if (!trace_.isValid()) return std::nullopt;
    return computeMetrics(registry_, trace_);
}

std::string Simulation::exportTo(const std::string& path, const RunInfo& info) const {
    const std::optional<RunMetrics> m = metrics();
    return exportRunToFile(path, info, registry_, trace_, m ? &*m : nullptr);
}

void Simulation::exportTo(std::ostream& out, const RunInfo& info) const {
    const std::optional<RunMetrics> m = metrics();
    exportRun(out, info, registry_, trace_, m ? &*m : nullptr);
}

} // namespace b6
