// Import checks: every malformed run file must be rejected with a useful message.
// Run: node dashboard/import.test.js
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const { validateRun, parseRun } = require("./app.js");

const read = (file) => fs.readFileSync(path.join(__dirname, file), "utf8");
const load = (file) => JSON.parse(read(file));
const FIXTURE = "fixtures/rr_preemption.design.json";
const INVERSION = "../runs/priority_inversion-fixed_priority-heap.json"; // Has mutex snapshots.
const SEMAPHORE = "../runs/semaphore_signal-fixed_priority-heap.json";
const EDF = "../runs/rm_edf_compare-edf-heap.json";

const rejectsFrom = (file) => (mutate, pattern) => {
  const run = load(file);
  mutate(run);
  const errs = validateRun(run);
  assert(errs.some((e) => pattern.test(e)), `expected ${pattern}, got ${JSON.stringify(errs)}`);
};

// Everything the team ships must be accepted.
for (const dir of ["fixtures", "../runs"])
  for (const f of fs.readdirSync(path.join(__dirname, dir)).filter((n) => n.endsWith(".json")))
    assert.deepStrictEqual(validateRun(load(`${dir}/${f}`)), [], `${dir}/${f}`);

// File text: not JSON, and the shape of the top level.
assert.match(parseRun("{ not json").errors[0], /not valid JSON/);
assert.match(parseRun("").errors[0], /not valid JSON/);
assert.deepStrictEqual(parseRun("[]").errors, ["Top level must be a JSON object."]);
assert.deepStrictEqual(parseRun("null").errors, ["Top level must be a JSON object."]);
assert.deepStrictEqual(parseRun(read(FIXTURE)).errors, []);

// Envelope, config and identity.
let rejects = rejectsFrom(FIXTURE);
rejects((r) => { r.schemaVersion = 2; }, /schemaVersion/);
rejects((r) => { r.schemaVersion = "1"; }, /schemaVersion/);
for (const k of ["tasks", "jobs", "segments", "events", "snapshots"])
  rejects((r) => { delete r[k]; }, new RegExp(`Missing array "${k}"`));
for (const k of ["run", "config", "metrics", "validation"])
  rejects((r) => { r[k] = []; }, new RegExp(`Missing object "${k}"`));
rejects((r) => { r.run.runId = 7; }, /run\.runId/);
rejects((r) => { r.run.evidenceType = "guess"; }, /evidenceType/);
rejects((r) => { r.config.policy = "lottery"; }, /config\.policy/);
rejects((r) => { r.config.timerBackend = "wheel"; }, /config\.timerBackend/);
rejects((r) => { r.config.inheritance = "on"; }, /config\.inheritance/);
rejects((r) => { r.config.quantumTicks = 0; }, /quantumTicks/);
rejects((r) => { r.config.horizonTicks = -1; }, /horizonTicks/);
rejects((r) => { r.config.horizonTicks = 1e9; }, /horizonTicks/);
rejects((r) => { r.config.policy = "edf"; r.config.inheritance = true; }, /unsupported combination/);
rejects((r) => { r.config.policy = "rate_monotonic"; r.config.inheritance = true; }, /unsupported combination/);
rejects((r) => { r.validation.status = "fine"; }, /validation\.status/);
rejects((r) => { r.validation.traceTruncated = true; }, /traceTruncated/);
rejects((r) => { r.metrics.perTask = {}; }, /metrics\.perTask/);
rejects((r) => { r.metrics.perTask[0].taskId = 77; }, /metrics\.perTask\[0\].*unknown taskId/);
rejects((r) => { r.metrics.perTask = [null]; }, /metrics\.perTask\[0\] must be an object/);
rejects((r) => { r.metrics.overall = null; }, /metrics\.overall/);
rejects((r) => { r.validation.warnings = "none"; }, /validation\.warnings/);
rejects((r) => { r.validation.limitsHit = "none"; }, /validation\.limitsHit/);

// A null row must be reported, not crash the importer.
for (const k of ["tasks", "jobs", "segments", "events", "snapshots"])
  rejects((r) => { r[k] = [null]; }, new RegExp(`${k}\\[0\\] must be an object`));

// Tasks, jobs, segments.
rejects((r) => { r.tasks[1].taskId = 1; }, /duplicate taskId/);
rejects((r) => { r.tasks[0].name = 5; }, /tasks\[0\]\.name/);
rejects((r) => { r.tasks[0].basePriority = 32; }, /basePriority/);
rejects((r) => { r.jobs[1].jobId = 1; }, /duplicate jobId/);
rejects((r) => { r.jobs[0].taskId = 99; }, /unknown taskId/);
rejects((r) => { r.jobs[0].releaseTick = -1; }, /releaseTick/);
rejects((r) => { r.jobs[0].completionTick = "6"; }, /integer or null/);
rejects((r) => { r.jobs[0].completionTick = 9; }, /completionTick 9 is outside/); // Horizon is 8.
rejects((r) => { r.jobs[2].firstStartTick = 1; }, /firstStartTick 1 is outside/); // Released at 2.
rejects((r) => { r.jobs[0].totalCpuUsed = -3; }, /totalCpuUsed/);
rejects((r) => { r.jobs[0].finalState = "DONE"; }, /finalState/);
rejects((r) => { r.jobs[0].deadlineMissed = "no"; }, /deadlineMissed/);
rejects((r) => { r.segments[0].jobId = 42; }, /unknown jobId/);
rejects((r) => { r.segments[1].start = 0; }, /overlaps/);
rejects((r) => { r.segments[0].end = 0; }, /start < end/);
rejects((r) => { r.segments[0].start = -1; }, /start < end/);
rejects((r) => { r.segments[0].end = 0.5; }, /start < end/);
rejects((r) => { r.segments[7].end = 9; }, /past horizon/);
rejects((r) => { r.segments[0].effectivePriority = null; }, /segments\[0\]\.effectivePriority/);

// Events.
rejects = rejectsFrom(INVERSION);
rejects((r) => { r.events[0].type = "TELEPORT"; }, /events\[0\]\.type/);
rejects((r) => { r.events[0].jobId = 9; }, /events\[0\]: unknown jobId 9/);
rejects((r) => { r.events[0].taskId = 9; }, /events\[0\]: unknown taskId 9/);
rejects((r) => { r.events[2].resourceId = 5; }, /events\[2\]: unknown resourceId 5/);
rejects((r) => { r.events[1].seq = 1; }, /events\[1\].*seq/);
rejects((r) => { r.events[0].tick = 99; }, /events\[0\]\.tick/);
rejects((r) => { r.events[0].phase = 7; }, /events\[0\]\.phase/);
rejects((r) => { r.events[3].tick = 0; }, /events\[3\].*order/); // Phase 2 after phase 3 of tick 0.
rejects((r) => { r.events[0].data = null; }, /events\[0\]\.data/);
rejects((r) => { r.events[0].reason = {}; }, /events\[0\]\.reason/);

// Snapshots.
rejects((r) => { r.snapshots[3].tick = 2; }, /snapshots\[3\].*increase/);
rejects((r) => { r.snapshots[10].tick = 11; }, /snapshots\[10\]\.tick/);
rejects((r) => { r.snapshots[2].jobs[0].state = "RUNNING"; }, /more than one RUNNING/);
rejects((r) => { r.snapshots[2].runningJobId = 1; }, /runningJobId 1 does not match/);
rejects((r) => { r.snapshots[2].runningJobId = null; }, /runningJobId null does not match/);
rejects((r) => { r.snapshots[2].jobs[0].jobId = 99; }, /snapshots\[2\]\.jobs\[0\]: unknown jobId 99/);
rejects((r) => { r.snapshots[2].jobs[0].jobId = 2; }, /duplicate jobId 2/);
rejects((r) => { r.snapshots[2].jobs[0].taskId = 1; }, /taskId 1 does not match job 1/);
rejects((r) => { r.snapshots[2].jobs[0].state = "NAPPING"; }, /state/);
rejects((r) => { r.snapshots[2].jobs[0].suspended = 0; }, /suspended/);
rejects((r) => { r.snapshots[2].jobs[1].blockReason = "vibes"; }, /blockReason/);
rejects((r) => { r.snapshots[2].jobs[1].blockedResourceId = 9; }, /unknown resourceId 9/);
rejects((r) => { r.snapshots[2].jobs[0].effectivePriority = 40; }, /effectivePriority/);
rejects((r) => { r.snapshots[2].jobs[0].cpuBurstRemaining = -1; }, /cpuBurstRemaining/);
rejects((r) => { r.snapshots[2].jobs[0].wakeTick = "soon"; }, /wakeTick/);
rejects((r) => { r.snapshots[2].jobs = null; }, /snapshots\[2\]\.jobs must be an array/);
rejects((r) => { r.snapshots[2].jobs = [null]; }, /snapshots\[2\]\.jobs\[0\] must be an object/);
rejects((r) => { r.snapshots[2].readyOrder = [1, 1]; }, /readyOrder.*duplicate/);
rejects((r) => { r.snapshots[2].readyOrder = [44]; }, /readyOrder.*unknown jobId 44/);
rejects((r) => { r.snapshots[2].mutexes[0].ownerJobId = 44; }, /ownerJobId.*unknown jobId 44/);
rejects((r) => { r.snapshots[2].mutexes[0].waiters = [2, 2]; }, /waiters.*duplicate/);
rejects((r) => { r.snapshots[2].mutexes = [null]; }, /mutexes\[0\] must be an object/);
rejects((r) => { r.snapshots[2].mutexes.push(r.snapshots[2].mutexes[0]); }, /duplicate resourceId/);
rejects((r) => { delete r.snapshots[2].semaphores; }, /semaphores must be an array/);

rejects = rejectsFrom(SEMAPHORE);
rejects((r) => { r.snapshots[0].semaphores[0].count = 2; }, /count 2 must be between 0 and maxCount 1/);
rejects((r) => { r.snapshots[0].semaphores[0].count = -1; }, /count -1 must be between/);
rejects((r) => { r.snapshots[0].semaphores[0].maxCount = 0; }, /maxCount/);
rejectsFrom(EDF)((r) => { r.snapshots[0].mutexes = [{ resourceId: 0, ownerJobId: null, waiters: [] }]; }, /unsupported combination/);

console.log("dashboard import checks passed");
