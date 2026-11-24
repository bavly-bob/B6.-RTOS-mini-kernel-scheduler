"use strict";
// B6 Scheduler Dashboard: imports schemaVersion 1 run files and displays them.
// It never makes or recomputes scheduling decisions; every number shown comes from the file.
// All file text is inserted with textContent, never as HTML.

const MAX_FILE_BYTES = 32 * 1024 * 1024;
const MAX_HORIZON = 2000; // Kernel ceiling (kernel/include/types.h); also bounds the chart size.
const REQUIRED_ARRAYS = ["tasks", "jobs", "segments", "events", "snapshots"];
const REQUIRED_OBJECTS = ["run", "config", "metrics", "validation"];
const POLICIES = ["fixed_priority", "rate_monotonic", "edf"];
const TIMER_BACKENDS = ["sorted_list", "heap"];
const STATES = ["READY", "RUNNING", "BLOCKED", "SUSPENDED", "COMPLETED"];
const BLOCK_REASONS = ["none", "timer", "mutex", "semaphore"];
const EVENT_TYPES = ["RELEASE", "DISPATCH", "PREEMPT", "BLOCK", "WAKE", "SUSPEND", "RESUME", "COMPLETE",
  "LOCK", "UNLOCK", "WAIT", "SIGNAL", "PRIORITY_CHANGE", "DEADLINE_MISS", "ERROR"];

const isInt = (v) => Number.isInteger(v);
const isCount = (v) => Number.isInteger(v) && v >= 0;
const isIntOrNull = (v) => v === null || Number.isInteger(v);
const isPriority = (v) => Number.isInteger(v) && v >= 0 && v <= 31;
const isObj = (v) => v !== null && typeof v === "object" && !Array.isArray(v);

// Returns a list of human-readable problems; an empty list means the run is accepted.
function validateRun(run) {
  const errs = [];
  const need = (ok, message) => { if (!ok) errs.push(message); return ok; };
  if (!isObj(run)) return ["Top level must be a JSON object."];
  if (run.schemaVersion !== 1) return [`schemaVersion is ${JSON.stringify(run.schemaVersion)}; this dashboard supports only 1.`];
  for (const k of REQUIRED_OBJECTS) need(isObj(run[k]), `Missing object "${k}".`);
  for (const k of REQUIRED_ARRAYS)
    if (need(Array.isArray(run[k]), `Missing array "${k}".`))
      run[k].forEach((row, i) => need(isObj(row), `${k}[${i}] must be an object.`));
  if (errs.length) return errs; // Nothing below can be read safely.

  const c = run.config;
  const horizon = c.horizonTicks;
  if (!need(isCount(horizon) && horizon <= MAX_HORIZON, `config.horizonTicks must be an integer from 0 to ${MAX_HORIZON}.`)) return errs;
  const inRun = (t, from = 0) => isInt(t) && t >= from && t <= horizon;

  need(typeof run.run.runId === "string", "run.runId must be text.");
  need(["engine_run", "design_fixture"].includes(run.run.evidenceType), `run.evidenceType must be "engine_run" or "design_fixture", got ${JSON.stringify(run.run.evidenceType)}.`);
  need(POLICIES.includes(c.policy), `config.policy ${JSON.stringify(c.policy)} is not one of ${POLICIES.join(", ")}.`);
  need(TIMER_BACKENDS.includes(c.timerBackend), `config.timerBackend ${JSON.stringify(c.timerBackend)} is not one of ${TIMER_BACKENDS.join(", ")}.`);
  need(typeof c.inheritance === "boolean", "config.inheritance must be true or false.");
  need(isInt(c.quantumTicks) && c.quantumTicks >= 1, "config.quantumTicks must be an integer of at least 1.");
  if (c.policy !== "fixed_priority")
    need(c.inheritance !== true, `config: unsupported combination: ${c.policy} with priority inheritance.`);

  const v = run.validation;
  need(v.status === "ok" || v.status === "invalid", `validation.status must be "ok" or "invalid", got ${JSON.stringify(v.status)}.`);
  need(!(v.status === "ok" && v.traceTruncated), 'validation.traceTruncated must be false when status is "ok".');
  for (const f of ["warnings", "limitsHit"]) need(v[f] === undefined || Array.isArray(v[f]), `validation.${f} must be an array.`);

  const taskIds = new Set();
  run.tasks.forEach((t, i) => {
    if (need(isInt(t.taskId), `tasks[${i}].taskId must be an integer.`))
      need(!taskIds.has(t.taskId), `tasks[${i}]: duplicate taskId ${t.taskId}.`);
    taskIds.add(t.taskId);
    need(typeof t.name === "string", `tasks[${i}].name must be text.`);
    need(isPriority(t.basePriority), `tasks[${i}].basePriority must be an integer from 0 to 31.`);
  });

  const jobTask = new Map(); // jobId -> taskId
  run.jobs.forEach((j, i) => {
    if (need(isInt(j.jobId), `jobs[${i}].jobId must be an integer.`))
      need(!jobTask.has(j.jobId), `jobs[${i}]: duplicate jobId ${j.jobId}.`);
    jobTask.set(j.jobId, j.taskId);
    need(taskIds.has(j.taskId), `jobs[${i}]: unknown taskId ${j.taskId}.`);
    for (const f of ["releaseTick", "absoluteDeadline", "totalCpuUsed"])
      need(isCount(j[f]), `jobs[${i}].${f} must be a nonnegative integer.`);
    for (const f of ["firstStartTick", "completionTick", "deadlineMissTick"])
      if (need(isIntOrNull(j[f]), `jobs[${i}].${f} must be an integer or null.`) && j[f] !== null)
        need(inRun(j[f], j.releaseTick), `jobs[${i}].${f} ${j[f]} is outside the job's release ${j.releaseTick} to horizon ${horizon}.`);
    need(STATES.includes(j.finalState), `jobs[${i}].finalState ${JSON.stringify(j.finalState)} is not a known state.`);
    need(typeof j.deadlineMissed === "boolean", `jobs[${i}].deadlineMissed must be true or false.`);
  });
  const needJob = (id, where) => need(jobTask.has(id), `${where}: unknown jobId ${id}.`);

  let prevEnd = null;
  run.segments.forEach((s, i) => {
    if (!need(isInt(s.start) && isInt(s.end) && s.start >= 0 && s.start < s.end,
      `segments[${i}]: need integers with 0 <= start < end (got ${s.start}, ${s.end}).`)) return;
    need(s.end <= horizon, `segments[${i}]: end ${s.end} is past horizon ${horizon}.`);
    need(prevEnd === null || s.start >= prevEnd, `segments[${i}]: overlaps or is out of order (starts ${s.start}, previous ended ${prevEnd}).`);
    prevEnd = s.end;
    if (s.jobId === null) return; // Idle.
    needJob(s.jobId, `segments[${i}]`);
    for (const f of ["basePriority", "effectivePriority"])
      need(isPriority(s[f]), `segments[${i}].${f} must be an integer from 0 to 31.`);
  });

  // Snapshots declare the run's resources, so they are read before the events that refer to them.
  const resourceIds = new Set();
  const idList = (ids, where) => {
    if (!need(Array.isArray(ids), `${where} must be an array.`)) return;
    need(new Set(ids).size === ids.length, `${where}: contains a duplicate jobId.`);
    ids.forEach((id) => needJob(id, where));
  };
  const rows = (list, where, each) => {
    if (!need(Array.isArray(list), `${where} must be an array.`)) return;
    list.forEach((row, i) => { if (need(isObj(row), `${where}[${i}] must be an object.`)) each(row, `${where}[${i}]`); });
  };
  let prevTick = -1;
  run.snapshots.forEach((s, i) => {
    const at = `snapshots[${i}]`;
    if (need(inRun(s.tick), `${at}.tick must be an integer from 0 to ${horizon}.`)) {
      need(s.tick > prevTick, `${at}: ticks must strictly increase (${s.tick} after ${prevTick}).`);
      prevTick = s.tick;
    }
    const here = new Set(); // Resource IDs in this snapshot.
    const resource = (r, where) => {
      if (need(isInt(r.resourceId), `${where}.resourceId must be an integer.`))
        need(!here.has(r.resourceId), `${where}: duplicate resourceId ${r.resourceId}.`);
      here.add(r.resourceId);
      resourceIds.add(r.resourceId);
      idList(r.waiters, `${where}.waiters`);
    };
    rows(s.mutexes, `${at}.mutexes`, (m, where) => {
      resource(m, where);
      if (m.ownerJobId !== null) needJob(m.ownerJobId, `${where}.ownerJobId`);
    });
    rows(s.semaphores, `${at}.semaphores`, (m, where) => {
      resource(m, where);
      need(isInt(m.maxCount) && m.maxCount >= 1, `${where}.maxCount must be an integer of at least 1.`);
      need(isCount(m.count) && m.count <= m.maxCount, `${where}: count ${m.count} must be between 0 and maxCount ${m.maxCount}.`);
    });
    const seen = new Set();
    const running = [];
    rows(s.jobs, `${at}.jobs`, (j, where) => {
      if (needJob(j.jobId, where)) need(j.taskId === jobTask.get(j.jobId), `${where}: taskId ${j.taskId} does not match job ${j.jobId}.`);
      need(!seen.has(j.jobId), `${where}: duplicate jobId ${j.jobId}.`);
      seen.add(j.jobId);
      need(STATES.includes(j.state), `${where}.state ${JSON.stringify(j.state)} is not a known state.`);
      if (j.state === "RUNNING") running.push(j.jobId);
      for (const f of ["suspended", "deadlineMissed"]) need(typeof j[f] === "boolean", `${where}.${f} must be true or false.`);
      for (const f of ["basePriority", "effectivePriority"]) need(isPriority(j[f]), `${where}.${f} must be an integer from 0 to 31.`);
      for (const f of ["cpuBurstRemaining", "totalCpuUsed", "scriptIndex"]) need(isCount(j[f]), `${where}.${f} must be a nonnegative integer.`);
      need(BLOCK_REASONS.includes(j.blockReason), `${where}.blockReason ${JSON.stringify(j.blockReason)} is not a known reason.`);
      need(isIntOrNull(j.wakeTick), `${where}.wakeTick must be an integer or null.`);
      for (const f of ["blockedResourceId", "heldMutexId"])
        need(j[f] === null || here.has(j[f]), `${where}.${f}: unknown resourceId ${j[f]}.`);
    });
    need(running.length <= 1, `${at}: more than one RUNNING job (${running.join(", ")}).`);
    need((running.length ? running[0] : null) === s.runningJobId, `${at}: runningJobId ${s.runningJobId} does not match its job rows.`);
    idList(s.readyOrder, `${at}.readyOrder`);
  });
  if (c.policy !== "fixed_priority")
    need(resourceIds.size === 0, `config: unsupported combination: ${c.policy} with mutexes or semaphores.`);

  let prev = null;
  run.events.forEach((e, i) => {
    const at = `events[${i}]`;
    need(inRun(e.tick), `${at}.tick must be an integer from 0 to ${horizon}.`);
    need(isInt(e.phase) && e.phase >= 1 && e.phase <= 6, `${at}.phase must be an integer from 1 to 6.`);
    need(isInt(e.seq) && (!prev || e.seq > prev.seq), `${at}.seq must be an integer greater than the previous one.`);
    need(!prev || e.tick > prev.tick || (e.tick === prev.tick && e.phase >= prev.phase), `${at}: out of order; events must be in (tick, phase, seq) order.`);
    prev = e;
    need(EVENT_TYPES.includes(e.type), `${at}.type ${JSON.stringify(e.type)} is not a known event.`);
    need(typeof e.reason === "string", `${at}.reason must be text.`);
    need(isObj(e.data), `${at}.data must be an object.`);
    if (e.jobId !== null) needJob(e.jobId, at);
    if (e.taskId !== null) need(taskIds.has(e.taskId), `${at}: unknown taskId ${e.taskId}.`);
    // Without snapshots the file does not list its resources, so only the type can be checked.
    if (e.resourceId !== null)
      need(run.snapshots.length ? resourceIds.has(e.resourceId) : isInt(e.resourceId), `${at}: unknown resourceId ${e.resourceId}.`);
  });

  need(isObj(run.metrics.overall), "metrics.overall must be an object.");
  rows(run.metrics.perTask, "metrics.perTask", (m, where) => need(taskIds.has(m.taskId), `${where}: unknown taskId ${m.taskId}.`));
  return errs;
}

// Parses and checks one file's text. `data` is usable only when `errors` is empty.
function parseRun(text) {
  let data;
  try { data = JSON.parse(text); }
  catch (err) { return { data: null, errors: [`not valid JSON (${err.message}). Import a run file written by b6_sim.`] }; }
  return { data, errors: validateRun(data) };
}

// ---- Selected-tick view: plain lookups in the recorded run ----
// A tick without a snapshot shows nothing; it is never filled from a neighbour or the final state.
const snapshotAt = (run, tick) => run.snapshots.find((s) => s.tick === tick) ?? null;
const taskOf = (run, taskId) => run.tasks.find((t) => t.taskId === taskId);

function jobLabel(run, jobId) {
  const job = run.jobs.find((j) => j.jobId === jobId);
  return `${taskOf(run, job.taskId).name} · job ${jobId}`;
}

function resourceLabel(run, resourceId) {
  const first = run.snapshots[0];
  if (first?.mutexes.some((m) => m.resourceId === resourceId)) return `mutex ${resourceId}`;
  if (first?.semaphores.some((m) => m.resourceId === resourceId)) return `semaphore ${resourceId}`;
  return `resource ${resourceId}`;
}

const jobRowsAt = (run, tick) => snapshotAt(run, tick)?.jobs ?? [];
const readyAt = (run, tick) => (snapshotAt(run, tick)?.readyOrder ?? []).map((id) => jobLabel(run, id));
const sleepersAt = (run, tick) => jobRowsAt(run, tick).filter((r) => r.wakeTick !== null).map((r) => [jobLabel(run, r.jobId), r.wakeTick]);

function resourcesAt(run, tick) {
  const snap = snapshotAt(run, tick);
  if (!snap) return [];
  const waiters = (r) => r.waiters.map((id) => jobLabel(run, id)).join(", then ") || "none";
  return [
    ...snap.mutexes.map((m) => [`mutex ${m.resourceId}`, m.ownerJobId === null ? "free" : `owner ${jobLabel(run, m.ownerJobId)}`, waiters(m)]),
    ...snap.semaphores.map((m) => [`semaphore ${m.resourceId}`, `count ${m.count} of ${m.maxCount}`, waiters(m)]),
  ];
}

function waitText(run, row, tick) {
  if (row.blockReason === "none") return "nothing";
  if (row.blockReason === "timer") return `timer, wakes at tick ${row.wakeTick}`;
  if (row.blockReason === "semaphore") return `semaphore ${row.blockedResourceId}`;
  const owner = snapshotAt(run, tick).mutexes.find((m) => m.resourceId === row.blockedResourceId)?.ownerJobId ?? null;
  return `mutex ${row.blockedResourceId}, ${owner === null ? "not owned" : `owned by ${jobLabel(run, owner)}`}`;
}

// scriptIndex is the next step to take; a RUN that is under way has already been passed.
function nextStep(run, row) {
  if (row.state === "COMPLETED") return "finished";
  return String(taskOf(run, row.taskId).script ?? "").split("; ")[row.scriptIndex] || "not given";
}

function burstText(run, row) {
  return row.cpuBurstRemaining === 0 && nextStep(run, row).startsWith("RUN") ? "not started" : row.cpuBurstRemaining;
}

const priorityText = (row) => row.effectivePriority === row.basePriority ? String(row.basePriority)
  : `${row.effectivePriority} (boosted from ${row.basePriority})`;

// [label, value] pairs describing one job at one tick.
function inspectJob(run, jobId, tick) {
  const job = run.jobs.find((j) => j.jobId === jobId);
  const fixed = [["Job", jobLabel(run, jobId)], ["Released at", job.releaseTick], ["Deadline", job.absoluteDeadline], ["Script", taskOf(run, job.taskId).script]];
  const state = (text) => [...fixed, ["State", text]];
  if (!snapshotAt(run, tick)) return state(`no snapshot recorded at tick ${tick}`);
  const row = jobRowsAt(run, tick).find((r) => r.jobId === jobId);
  if (!row) return state(tick < job.releaseTick ? `not released yet (releases at ${job.releaseTick})` : "not in this snapshot");
  return [...state(row.state),
    ["Base priority", row.basePriority], ["Effective priority", priorityText(row)],
    ["Waiting for", waitText(run, row, tick)], ["Holds", row.heldMutexId === null ? "nothing" : `mutex ${row.heldMutexId}`],
    ["CPU left in this RUN", burstText(run, row)], ["CPU used so far", row.totalCpuUsed],
    ["Next step", nextStep(run, row)], ["Deadline missed", row.deadlineMissed ? "yes" : "no, not by this tick"]];
}

// All events at the tick plus a little context on both sides.
function eventsNear(run, tick) {
  const count = (test) => run.events.filter(test).length; // Events are sorted by tick.
  return run.events.slice(Math.max(0, count((e) => e.tick < tick) - 10), count((e) => e.tick <= tick) + 8);
}

function describeEvent(run, e) {
  const data = { ...e.data };
  const parts = [];
  if (e.type === "PRIORITY_CHANGE") {
    parts.push(`effective priority ${data.from} → ${data.to}`);
    delete data.from;
    delete data.to;
  }
  parts.push(e.reason.replaceAll("_", " "));
  if (e.resourceId !== null) parts.push(resourceLabel(run, e.resourceId));
  for (const [key, value] of Object.entries(data)) {
    const job = key.endsWith("JobId") && run.jobs.some((j) => j.jobId === value);
    parts.push(job ? `${key.slice(0, -5)} ${jobLabel(run, value)}` : `${key} ${value ?? "none"}`);
  }
  return (e.jobId === null ? "" : `${jobLabel(run, e.jobId)}: `) + parts.join(", ");
}

// The events that explain an outcome, over the whole run.
const keyEvents = (run) => run.events.filter((e) => ["PRIORITY_CHANGE", "DEADLINE_MISS", "ERROR"].includes(e.type));

// ---- Metrics and trust notes: exported values with units and explicit "not applicable" ----
const round2 = (v) => (typeof v === "number" ? Math.round(v * 100) / 100 : v);
const percent = (v) => (typeof v === "number" ? `${round2(v)}%` : "not given");

// [label, value] pairs; empty when the engine exported no metrics.
function overallMetrics(run) {
  const { busyTicks, observedTicks, busyPercent, completedJobs, unfinishedJobs, deadlineMisses,
    demandUtilization, rmBound, rmBoundApplicable, rmBoundNote, ...others } = run.metrics.overall;
  if (Object.keys(run.metrics.overall).length === 0) return [];
  const fraction = (v) => (typeof v === "number" ? percent(v * 100) : "not applicable");
  return [
    ["CPU busy", percent(busyPercent) + (observedTicks === undefined ? "" : ` (${busyTicks} of ${observedTicks} ticks)`)],
    ["Completed jobs", completedJobs], ["Unfinished jobs", unfinishedJobs], ["Deadline misses", deadlineMisses],
    ["Periodic demand", fraction(demandUtilization)],
    ["RM sufficient bound", (rmBoundApplicable ? fraction(rmBound) : "not applicable") + (rmBoundNote ? `: ${rmBoundNote}` : "")],
    ...Object.entries(others),
  ];
}

function taskMetrics(run) {
  return run.metrics.perTask.map((p) => [taskOf(run, p.taskId).name, p.completedSamples,
    ...[p.responseMin, p.responseMean, p.responseMax].map((v) => (v === null ? "no completed job" : round2(v))),
    p.jitter === null ? "insufficient samples" : p.jitter, p.misses]);
}

// Reasons not to take the run at face value; empty for a clean run.
function runNotes(run) {
  const v = run.validation;
  return [
    ...(v.status === "invalid" ? [`INVALID RUN, do not use as evidence: ${v.error ?? "no reason given"}`] : []),
    ...(v.limitsHit ?? []).map((l) => `Limit reached: ${l}`),
    ...(v.traceTruncated ? ["The trace is truncated: some records are missing."] : []),
    ...(v.warnings ?? []).map((w) => `Warning: ${w}`),
  ];
}

if (typeof module !== "undefined")
  module.exports = { validateRun, parseRun, snapshotAt, inspectJob, jobRowsAt, resourcesAt, sleepersAt, readyAt,
    eventsNear, describeEvent, keyEvents, overallMetrics, taskMetrics, runNotes };
