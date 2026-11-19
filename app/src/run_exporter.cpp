#include "run_exporter.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <stdexcept>

#ifndef B6_ENGINE_REVISION
#define B6_ENGINE_REVISION ""
#endif

namespace b6 {
namespace {

// ---------------------------------------------------------------------------
// JsonWriter: a minimal streaming writer for this known schema.
// It tracks whether a comma is needed at each nesting level, escapes every
// string, and writes non-finite doubles as null (JSON has no NaN/Infinity).
// Containers at depth <= kPrettyDepth start each element on a new line, so
// run files stay readable and diff well; deeper values stay on one line.
// ---------------------------------------------------------------------------
class JsonWriter {
public:
    explicit JsonWriter(std::ostream& out) : out_(out) {}

    void beginObject() { beforeValue(); open('{'); }
    void endObject() { close('}'); }
    void beginArray() { beforeValue(); open('['); }
    void endArray() { close(']'); }

    void key(const char* name) {
        separate();
        writeString(name);
        out_ << ':';
        afterKey_ = true;
    }

    void value(std::int64_t v) { beforeValue(); out_ << v; }
    void value(int v) { value(static_cast<std::int64_t>(v)); }
    void value(std::size_t v) { beforeValue(); out_ << v; }
    void value(bool v) { beforeValue(); out_ << (v ? "true" : "false"); }
    void value(double v) {
        beforeValue();
        if (!std::isfinite(v)) { out_ << "null"; return; }
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "%.10g", v);
        out_ << buffer;
    }
    void value(const char* s) { beforeValue(); writeString(s ? s : ""); }
    void value(const std::string& s) { beforeValue(); writeString(s); }
    void null() { beforeValue(); out_ << "null"; }

    template <typename T>
    void optional(const std::optional<T>& v) { if (v) value(*v); else null(); }

    // key + value shorthands.
    template <typename T> void field(const char* name, const T& v) { key(name); value(v); }
    template <typename T> void optionalField(const char* name, const std::optional<T>& v) {
        key(name);
        optional(v);
    }
    void nullField(const char* name) { key(name); null(); }

private:
    static constexpr int kMaxDepth = 16;
    static constexpr int kPrettyDepth = 2;
    std::ostream& out_;
    bool first_[kMaxDepth] = {};
    bool isArray_[kMaxDepth] = {};
    int depth_ = 0;
    bool afterKey_ = false;

    void open(char c) {
        if (depth_ + 1 >= kMaxDepth) throw std::logic_error("JSON nesting too deep");
        out_ << c;
        ++depth_;
        first_[depth_] = true;
        isArray_[depth_] = (c == '[');
    }
    void close(char c) {
        const bool empty = first_[depth_];
        --depth_;
        if (!empty && depth_ < kPrettyDepth) newline();
        out_ << c;
    }
    void newline() {
        out_ << '\n';
        for (int i = 0; i < depth_; ++i) out_ << "  ";
    }
    // Comma + line break before a new element of the current container.
    void separate() {
        if (depth_ == 0) return;
        if (!first_[depth_]) out_ << ',';
        first_[depth_] = false;
        if (depth_ <= kPrettyDepth) newline();
    }
    void beforeValue() {
        if (afterKey_) { afterKey_ = false; return; }  // Value follows its key.
        separate();                                    // Array element.
    }

    // RFC 8259 escaping. '<', '>' and '&' are also escaped so a run file can
    // never close a script element if someone embeds it in a page. Bytes
    // >= 0x80 pass through unchanged (input names are expected to be UTF-8).
    void writeString(const std::string& s) {
        static const char* hex = "0123456789abcdef";
        out_ << '"';
        for (unsigned char c : s) {
            switch (c) {
            case '"': out_ << "\\\""; break;
            case '\\': out_ << "\\\\"; break;
            case '\b': out_ << "\\b"; break;
            case '\f': out_ << "\\f"; break;
            case '\n': out_ << "\\n"; break;
            case '\r': out_ << "\\r"; break;
            case '\t': out_ << "\\t"; break;
            default:
                if (c < 0x20 || c == '<' || c == '>' || c == '&')
                    out_ << "\\u00" << hex[c >> 4] << hex[c & 0xF];
                else
                    out_ << static_cast<char>(c);
            }
        }
        out_ << '"';
    }
};

const char* opName(OpCode op) {
    switch (op) {
    case OpCode::Run: return "RUN";
    case OpCode::Sleep: return "SLEEP";
    case OpCode::Lock: return "LOCK";
    case OpCode::Unlock: return "UNLOCK";
    case OpCode::Wait: return "WAIT";
    case OpCode::Signal: return "SIGNAL";
    case OpCode::End: return "END";
    }
    return "END";
}

void writeRun(JsonWriter& w, const RunInfo& info, const Scenario& s) {
    w.key("run");
    w.beginObject();
    w.field("runId", info.runId);
    w.field("scenarioId", s.config.scenarioId);
    w.field("label", info.label);
    w.field("evidenceType",
            info.evidence == RunInfo::Evidence::EngineRun ? "engine_run" : "design_fixture");
    if (info.engineRevision) {
        w.field("engineRevision", *info.engineRevision);
    } else if (std::string(B6_ENGINE_REVISION).empty()) {
        w.nullField("engineRevision");
    } else {
        w.field("engineRevision", B6_ENGINE_REVISION);
    }
    w.optionalField("createdAt", info.createdAt);
    w.endObject();
}

void writeConfig(JsonWriter& w, const RunConfig& c) {
    w.key("config");
    w.beginObject();
    w.field("policy", toString(c.policy));
    w.field("timerBackend", toString(c.timerBackend));
    w.field("inheritance", c.inheritance);
    w.field("quantumTicks", static_cast<std::int64_t>(c.quantumTicks));
    w.field("tickMs", static_cast<std::int64_t>(c.tickMs));
    w.field("horizonTicks", static_cast<std::int64_t>(c.horizonTicks));
    w.field("contextSwitchCostTicks", 0);
    w.key("limits");
    w.beginObject();
    w.field("maxTasks", c.limits.maxTasks);
    w.field("maxJobs", c.limits.maxJobs);
    w.field("maxMutexes", c.limits.maxMutexes);
    w.field("maxSemaphores", c.limits.maxSemaphores);
    w.field("maxScriptLength", c.limits.maxScriptLength);
    w.field("maxHorizonTicks", static_cast<std::int64_t>(c.limits.maxHorizonTicks));
    w.field("maxEvents", c.limits.maxEvents);
    w.field("maxSnapshotJobRows", c.limits.maxSnapshotJobRows);
    w.endObject();
    w.key("restrictions");
    w.beginArray();
    w.value("single CPU");
    w.value("zero context-switch cost");
    w.value("resource operations take zero CPU time");
    w.value("at most one held mutex per job");
    w.endArray();
    w.endObject();
}

void writeTasks(JsonWriter& w, const Scenario& s) {
    w.key("tasks");
    w.beginArray();
    for (std::size_t i = 0; i < s.taskCount; ++i) {
        const TaskSpec& t = s.tasks[i];
        w.beginObject();
        w.field("taskId", t.taskId);
        w.field("name", t.name);
        w.field("basePriority", t.basePriority);
        w.field("firstRelease", static_cast<std::int64_t>(t.firstRelease));
        w.field("periodTicks", static_cast<std::int64_t>(t.periodTicks));
        w.field("relativeDeadline", static_cast<std::int64_t>(t.relativeDeadline));
        w.field("script", scriptText(t));
        w.endObject();
    }
    w.endArray();
}

void writeJobs(JsonWriter& w, const Registry& registry) {
    w.key("jobs");
    w.beginArray();
    for (JobId id = 1; id <= static_cast<JobId>(registry.jobCount()); ++id) {
        const Job* j = registry.findJob(id);
        if (!j) throw std::logic_error("registry job IDs are not contiguous");
        w.beginObject();
        w.field("jobId", j->jobId);
        w.field("taskId", j->taskId);
        w.field("releaseTick", static_cast<std::int64_t>(j->releaseTick));
        w.field("absoluteDeadline", static_cast<std::int64_t>(j->absoluteDeadline));
        w.optionalField("firstStartTick", j->firstStartTick);
        w.optionalField("completionTick", j->completionTick);
        w.field("totalCpuUsed", static_cast<std::int64_t>(j->totalCpuUsed));
        w.field("finalState", toString(j->state));
        w.field("deadlineMissed", j->deadlineMissRecorded);
        w.optionalField("deadlineMissTick", j->deadlineMissTick);
        w.endObject();
    }
    w.endArray();
}

void writeSegments(JsonWriter& w, const RunTrace& trace) {
    w.key("segments");
    w.beginArray();
    for (std::size_t i = 0; i < trace.segments().size(); ++i) {
        const TraceSegment& s = trace.segments().at(i);
        w.beginObject();
        w.field("start", static_cast<std::int64_t>(s.start));
        w.field("end", static_cast<std::int64_t>(s.end));
        w.optionalField("jobId", s.jobId);
        w.optionalField("basePriority", s.basePriority);
        w.optionalField("effectivePriority", s.effectivePriority);
        w.endObject();
    }
    w.endArray();
}

void writeEvents(JsonWriter& w, const RunTrace& trace) {
    w.key("events");
    w.beginArray();
    for (std::size_t i = 0; i < trace.events().size(); ++i) {
        const TraceEvent& e = trace.events().at(i);
        w.beginObject();
        w.field("tick", static_cast<std::int64_t>(e.tick));
        w.field("phase", static_cast<int>(e.phase));
        w.field("seq", static_cast<std::int64_t>(e.seq));
        w.field("type", toString(e.type));
        w.optionalField("jobId", e.jobId);
        w.optionalField("taskId", e.taskId);
        w.optionalField("resourceId", e.resourceId);
        w.field("reason", e.reason);
        w.key("data");
        w.beginObject();
        for (std::size_t d = 0; d < e.dataCount; ++d) {
            const EventDatum& datum = e.data[d];
            w.key(datum.key);
            if (datum.kind == EventDatum::Kind::Integer) w.value(datum.value);
            else if (datum.kind == EventDatum::Kind::Boolean) w.value(datum.value != 0);
            else w.null();
        }
        w.endObject();
        w.endObject();
    }
    w.endArray();
}

void writeIdRange(JsonWriter& w, const BoundedArray<JobId>& pool, std::size_t first,
                  std::size_t count) {
    w.beginArray();
    for (std::size_t i = 0; i < count; ++i) w.value(pool.at(first + i));
    w.endArray();
}

void writeSnapshots(JsonWriter& w, const RunTrace& trace) {
    w.key("snapshots");
    w.beginArray();
    for (std::size_t i = 0; i < trace.snapshots().size(); ++i) {
        const TraceSnapshot& s = trace.snapshots().at(i);
        w.beginObject();
        w.field("tick", static_cast<std::int64_t>(s.tick));
        w.optionalField("runningJobId", s.runningJobId);
        w.key("readyOrder");
        writeIdRange(w, trace.idPool(), s.firstReady, s.readyCount);
        w.key("jobs");
        w.beginArray();
        for (std::size_t r = 0; r < s.rowCount; ++r) {
            const SnapshotJobRow& row = trace.jobRows().at(s.firstRow + r);
            w.beginObject();
            w.field("jobId", row.jobId);
            w.field("taskId", row.taskId);
            w.field("state", toString(row.state));
            w.field("suspended", row.suspended);
            w.field("basePriority", row.basePriority);
            w.field("effectivePriority", row.effectivePriority);
            w.field("cpuBurstRemaining", static_cast<std::int64_t>(row.cpuBurstRemaining));
            w.field("totalCpuUsed", static_cast<std::int64_t>(row.totalCpuUsed));
            w.field("scriptIndex", row.scriptIndex);
            w.field("blockReason", toString(row.blockReason));
            w.optionalField("blockedResourceId", row.blockedResourceId);
            w.optionalField("heldMutexId", row.heldMutexId);
            w.optionalField("wakeTick", row.wakeTick);
            w.field("deadlineMissed", row.deadlineMissed);
            w.endObject();
        }
        w.endArray();
        w.key("mutexes");
        w.beginArray();
        for (std::size_t r = 0; r < s.resourceCount; ++r) {
            const SnapshotResourceRow& row = trace.resourceRows().at(s.firstResource + r);
            if (!row.isMutex) continue;
            w.beginObject();
            w.field("resourceId", row.resourceId);
            w.optionalField("ownerJobId", row.ownerJobId);
            w.key("waiters");
            writeIdRange(w, trace.idPool(), row.firstWaiter, row.waiterCount);
            w.endObject();
        }
        w.endArray();
        w.key("semaphores");
        w.beginArray();
        for (std::size_t r = 0; r < s.resourceCount; ++r) {
            const SnapshotResourceRow& row = trace.resourceRows().at(s.firstResource + r);
            if (row.isMutex) continue;
            w.beginObject();
            w.field("resourceId", row.resourceId);
            w.field("count", row.count);
            w.field("maxCount", row.maxCount);
            w.key("waiters");
            writeIdRange(w, trace.idPool(), row.firstWaiter, row.waiterCount);
            w.endObject();
        }
        w.endArray();
        w.endObject();
    }
    w.endArray();
}

void writeMetrics(JsonWriter& w, const RunMetrics* m) {
    w.key("metrics");
    w.beginObject();
    w.key("overall");
    w.beginObject();
    if (m) {
        w.field("busyTicks", static_cast<std::int64_t>(m->busyTicks));
        w.field("observedTicks", static_cast<std::int64_t>(m->observedTicks));
        w.optionalField("busyPercent", m->busyPercent);
        w.field("completedJobs", m->completedJobs);
        w.field("unfinishedJobs", m->unfinishedJobs);
        w.field("deadlineMisses", m->deadlineMisses);
        w.optionalField("demandUtilization", m->demandUtilization);
        w.optionalField("rmBound", m->rmBound);
        w.field("rmBoundApplicable", m->rmBoundApplicable);
        w.field("rmBoundNote", m->rmBoundNote);
    }
    w.endObject();
    w.key("perTask");
    w.beginArray();
    for (std::size_t i = 0; m && i < m->taskCount; ++i) {
        const TaskMetrics& t = m->perTask[i];
        w.beginObject();
        w.field("taskId", t.taskId);
        w.field("completedSamples", t.completedSamples);
        w.optionalField("responseMin", t.responseMin);
        w.optionalField("responseMean", t.responseMean);
        w.optionalField("responseMax", t.responseMax);
        w.optionalField("jitter", t.jitter);
        w.field("misses", t.misses);
        w.endObject();
    }
    w.endArray();
    w.endObject();
}

void writeValidation(JsonWriter& w, const RunTrace& trace, bool metricsMissing) {
    w.key("validation");
    w.beginObject();
    w.field("status", trace.isValid() ? "ok" : "invalid");
    if (trace.isValid()) w.nullField("error");
    else w.field("error", trace.error());
    w.key("warnings");
    w.beginArray();
    for (std::size_t i = 0; i < trace.warnings().size(); ++i) w.value(trace.warnings().at(i));
    if (metricsMissing) w.value("metrics were not computed for this run");
    w.endArray();
    w.key("limitsHit");
    w.beginArray();
    if (trace.eventsTruncated()) w.value("maxEvents");
    if (trace.snapshotsTruncated()) w.value("maxSnapshotJobRows");
    w.endArray();
    w.field("traceTruncated", trace.isTruncated());
    w.endObject();
}

} // namespace

const char* toString(Policy policy) {
    switch (policy) {
    case Policy::FixedPriority: return "fixed_priority";
    case Policy::RateMonotonic: return "rate_monotonic";
    case Policy::EDF: return "edf";
    }
    return "fixed_priority";
}

const char* toString(TimerBackend backend) {
    return backend == TimerBackend::Heap ? "heap" : "sorted_list";
}

std::string scriptText(const TaskSpec& task) {
    std::string text;
    for (std::size_t i = 0; i < task.scriptLength; ++i) {
        const Instruction& ins = task.script[i];
        if (i) text += "; ";
        text += opName(ins.op);
        if (ins.op == OpCode::Run || ins.op == OpCode::Sleep) {
            text += ' ';
            text += std::to_string(ins.durationTicks);
        } else if (ins.resourceId) {
            text += ' ';
            text += std::to_string(*ins.resourceId);
        }
    }
    return text;
}

void exportRun(std::ostream& out, const RunInfo& info, const Registry& registry,
               const RunTrace& trace, const RunMetrics* metrics) {
    const Scenario& s = registry.scenario();
    JsonWriter w(out);
    w.beginObject();
    w.field("schemaVersion", 1);
    writeRun(w, info, s);
    writeConfig(w, s.config);
    writeTasks(w, s);
    writeJobs(w, registry);
    writeSegments(w, trace);
    writeEvents(w, trace);
    writeSnapshots(w, trace);
    writeMetrics(w, metrics);
    writeValidation(w, trace, metrics == nullptr);
    w.endObject();
    out << '\n';
}

std::string exportRunToFile(const std::string& path, const RunInfo& info,
                            const Registry& registry, const RunTrace& trace,
                            const RunMetrics* metrics) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return "cannot open run file for writing: " + path;
    exportRun(file, info, registry, trace, metrics);
    file.flush();
    if (!file) return "failed while writing run file: " + path;
    const std::streamoff bytes = file.tellp();
    if (bytes > static_cast<std::streamoff>(kMaxRunFileBytes))
        return "run file is " + std::to_string(bytes) +
               " bytes, above the dashboard's 32 MiB limit; lower the horizon or snapshot limit";
    return "";
}

} // namespace b6
