#include "validation.h"
#include <limits>

namespace b6 {
namespace {
bool hasMutex(const Scenario& s, ResourceId id) {
    for (std::size_t i = 0; i < s.mutexCount; ++i)
        if (s.mutexes[i].resourceId == id) return true;
    return false;
}

bool hasSemaphore(const Scenario& s, ResourceId id) {
    for (std::size_t i = 0; i < s.semaphoreCount; ++i)
        if (s.semaphores[i].resourceId == id) return true;
    return false;
}
} // namespace

std::string validateScenario(const Scenario& s) {
    const RunConfig& c = s.config;
    const RunLimits& l = c.limits;
    if (c.scenarioId.empty()) return "config.scenarioId must not be empty";
    if (c.policy != Policy::FixedPriority && c.policy != Policy::RateMonotonic &&
        c.policy != Policy::EDF) return "config.policy is unsupported";
    if (c.timerBackend != TimerBackend::SortedList && c.timerBackend != TimerBackend::Heap)
        return "config.timerBackend is unsupported";
    if (l.maxTasks == 0 || l.maxTasks > kMaxTasks || l.maxJobs == 0 || l.maxJobs > kMaxJobs ||
        l.maxMutexes > kMaxResources || l.maxSemaphores > kMaxResources ||
        l.maxScriptLength == 0 || l.maxScriptLength > kMaxScriptLength ||
        l.maxHorizonTicks <= 0 || l.maxHorizonTicks > kMaxHorizon ||
        l.maxEvents == 0 || l.maxEvents > 50000 ||
        l.maxSnapshotJobRows == 0 || l.maxSnapshotJobRows > 100000)
        return "config.limits exceed supported storage ceilings or use an invalid zero";
    if (c.horizonTicks <= 0 || c.horizonTicks > l.maxHorizonTicks)
        return "config.horizonTicks must be positive and within the run limit";
    if (c.tickMs <= 0 || c.quantumTicks <= 0)
        return "config.tickMs and quantumTicks must be positive";

    // Check counts before indexing any caller-supplied array.
    if (s.taskCount > l.maxTasks) return "tasks exceed config.limits.maxTasks";
    if (s.mutexCount > l.maxMutexes) return "mutexes exceed config.limits.maxMutexes";
    if (s.semaphoreCount > l.maxSemaphores)
        return "semaphores exceed config.limits.maxSemaphores";
    if (c.policy != Policy::FixedPriority &&
        (c.inheritance || s.mutexCount != 0 || s.semaphoreCount != 0))
        return "RM/EDF initially require independent jobs without resources or inheritance";

    for (std::size_t i = 0; i < s.mutexCount; ++i) {
        const int id = s.mutexes[i].resourceId;
        if (id < 0) return "mutex.resourceId must be nonnegative";
        for (std::size_t j = 0; j < i; ++j)
            if (s.mutexes[j].resourceId == id) return "duplicate mutex.resourceId";
    }
    for (std::size_t i = 0; i < s.semaphoreCount; ++i) {
        const SemaphoreSpec& sem = s.semaphores[i];
        if (sem.resourceId < 0 || hasMutex(s, sem.resourceId))
            return "semaphore.resourceId must be nonnegative and unique across resources";
        for (std::size_t j = 0; j < i; ++j)
            if (s.semaphores[j].resourceId == sem.resourceId)
                return "duplicate semaphore.resourceId";
        if (sem.maxCount <= 0 || sem.initialCount < 0 || sem.initialCount > sem.maxCount)
            return "semaphore counts must satisfy 0 <= initialCount <= maxCount and maxCount > 0";
    }

    std::size_t releases = 0;
    for (std::size_t i = 0; i < s.taskCount; ++i) {
        const TaskSpec& task = s.tasks[i];
        const std::string field = "tasks[" + std::to_string(i) + "]";
        if (task.taskId < 0) return field + ".taskId must be nonnegative";
        for (std::size_t j = 0; j < i; ++j)
            if (s.tasks[j].taskId == task.taskId) return field + ".taskId is duplicated";
        if (task.name.empty()) return field + ".name must not be empty";
        if (task.basePriority < 0 || task.basePriority > 31)
            return field + ".basePriority must be in 0..31";
        if (task.firstRelease < 0 || task.periodTicks < 0 || task.relativeDeadline <= 0)
            return field + " requires nonnegative releases/periods and a positive deadline";
        if (task.relativeDeadline > std::numeric_limits<Tick>::max() - c.horizonTicks)
            return field + ".relativeDeadline would overflow an absolute deadline";
        if (c.policy != Policy::FixedPriority && task.periodTicks == 0)
            return field + " must be periodic for the initial RM/EDF comparison";
        if (task.scriptLength == 0 || task.scriptLength > l.maxScriptLength)
            return field + ".scriptLength must be positive and within the run limit";

        std::optional<ResourceId> heldMutex;
        Tick totalCpu = 0;
        for (std::size_t j = 0; j < task.scriptLength; ++j) {
            const Instruction& instruction = task.script[j];
            const std::string at = field + ".script[" + std::to_string(j) + "]";
            const bool timed = instruction.op == OpCode::Run || instruction.op == OpCode::Sleep;
            const bool resource = instruction.op == OpCode::Lock || instruction.op == OpCode::Unlock ||
                                  instruction.op == OpCode::Wait || instruction.op == OpCode::Signal;
            if (timed) {
                if (instruction.durationTicks <= 0 || instruction.resourceId)
                    return at + " requires a positive duration and no resource ID";
            } else if (instruction.durationTicks != 0) {
                return at + " is zero-time and requires durationTicks=0";
            }
            if (resource != instruction.resourceId.has_value())
                return at + " has a missing or unexpected resource ID";
            if (c.policy != Policy::FixedPriority &&
                instruction.op != OpCode::Run && instruction.op != OpCode::End)
                return at + " is outside the initial independent RM/EDF model";

            switch (instruction.op) {
            case OpCode::Run:
                if (instruction.durationTicks > std::numeric_limits<Tick>::max() - totalCpu)
                    return at + " makes total CPU work overflow";
                totalCpu += instruction.durationTicks;
                break;
            case OpCode::Sleep:
                if (heldMutex) return at + " cannot sleep while holding a mutex";
                if (instruction.durationTicks > std::numeric_limits<Tick>::max() - c.horizonTicks)
                    return at + " would overflow a wake tick";
                break;
            case OpCode::Lock:
                if (!hasMutex(s, *instruction.resourceId)) return at + " references an unknown mutex";
                if (heldMutex) return at + " uses recursive or nested mutex locking";
                heldMutex = instruction.resourceId;
                break;
            case OpCode::Unlock:
                if (!heldMutex || heldMutex != instruction.resourceId)
                    return at + " unlocks a mutex not held by this job";
                heldMutex.reset();
                break;
            case OpCode::Wait:
                if (heldMutex) return at + " cannot wait on a semaphore while holding a mutex";
                [[fallthrough]];
            case OpCode::Signal:
                if (!hasSemaphore(s, *instruction.resourceId))
                    return at + " references an unknown semaphore";
                break;
            case OpCode::End:
                if (heldMutex) return at + " cannot complete while holding a mutex";
                if (j + 1 != task.scriptLength) return at + " END must be the last instruction";
                break;
            default:
                return at + " has an unsupported opcode";
            }
        }
        if (task.script[task.scriptLength - 1].op != OpCode::End)
            return field + ".script must finish with END";

        // No release at the final boundary: it settles existing jobs only.
        if (task.firstRelease < c.horizonTicks) {
            const Tick count = task.periodTicks == 0 ? 1 :
                1 + (c.horizonTicks - 1 - task.firstRelease) / task.periodTicks;
            releases += static_cast<std::size_t>(count);
            if (releases > l.maxJobs) return "scheduled releases exceed config.limits.maxJobs";
        }
    }
    return {};
}
} // namespace b6
