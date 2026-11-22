// Selected-tick checks: the inspector must show the recorded past, never the final state.
// Run: node dashboard/view.test.js
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const { snapshotAt, inspectJob, jobRowsAt, resourcesAt, sleepersAt, readyAt, eventsNear, describeEvent,
  keyEvents, overallMetrics, taskMetrics, runNotes } = require("./app.js");

const load = (file) => JSON.parse(fs.readFileSync(path.join(__dirname, file), "utf8"));
const runFile = (name) => load(`../runs/${name}.json`);
const inversion = runFile("priority_inversion-fixed_priority-heap");
const semaphore = runFile("semaphore_signal-fixed_priority-heap");
const suspend = runFile("suspend_resume-fixed_priority-heap");
const edf = runFile("rm_edf_compare-edf-heap");
const rr = runFile("rr_preemption-fixed_priority-heap");
const field = (run, jobId, tick, label) => {
  const pair = inspectJob(run, jobId, tick).find(([k]) => k === label);
  assert(pair, `inspector has no "${label}"`);
  return pair[1];
};

// No final-state leakage: H ends COMPLETED and missed, but neither was true at tick 4.
assert.strictEqual(inversion.jobs[1].finalState, "COMPLETED");
assert.strictEqual(inversion.jobs[1].deadlineMissed, true);
assert.strictEqual(field(inversion, 2, 4, "State"), "BLOCKED");
assert.strictEqual(field(inversion, 2, 5, "Deadline missed"), "no, not by this tick");
assert.strictEqual(field(inversion, 2, 6, "Deadline missed"), "yes");
assert.strictEqual(field(inversion, 2, 4, "CPU used so far"), 0);
assert.strictEqual(field(inversion, 1, 4, "State"), "READY");
assert.strictEqual(field(inversion, 1, 4, "CPU used so far"), 2);
assert.strictEqual(field(inversion, 1, 4, "CPU left in this RUN"), 2);
assert.strictEqual(field(inversion, 2, 0, "State"), "not released yet (releases at 1)");
assert.strictEqual(field(inversion, 2, 9, "State"), "COMPLETED");
assert.strictEqual(inspectJob(inversion, 2, 0).some(([k]) => k === "CPU used so far"), false);

// Blocking reason, owner and waiters.
assert.strictEqual(field(inversion, 2, 4, "Waiting for"), "mutex 0, owned by L · job 1");
assert.strictEqual(field(inversion, 1, 4, "Waiting for"), "nothing");
assert.strictEqual(field(inversion, 1, 4, "Holds"), "mutex 0");
assert.deepStrictEqual(resourcesAt(inversion, 4), [["mutex 0", "owner L · job 1", "H · job 2"]]);
assert.deepStrictEqual(resourcesAt(inversion, 9), [["mutex 0", "free", "none"]]);
assert.strictEqual(field(semaphore, 1, 1, "Waiting for"), "semaphore 0");
assert.deepStrictEqual(resourcesAt(semaphore, 1), [["semaphore 0", "count 0 of 1", "C · job 1"]]);
assert.deepStrictEqual(resourcesAt(semaphore, 2), [["semaphore 0", "count 0 of 1", "none"]]);

// A RUN that has not been loaded yet is not "0 ticks left".
assert.strictEqual(field(rr, 2, 0, "CPU left in this RUN"), "not started");
assert.strictEqual(field(rr, 2, 0, "Next step"), "RUN 3");
assert.strictEqual(field(rr, 1, 8, "Next step"), "finished");
assert.deepStrictEqual(readyAt(rr, 2), ["A · job 1", "B · job 2"]); // H runs, so it is not listed.

// Suspension keeps the reason for waiting until the timer fires at 5; resume is at 7.
assert.strictEqual(field(suspend, 1, 3, "State"), "SUSPENDED");
assert.strictEqual(field(suspend, 1, 3, "Waiting for"), "timer, wakes at tick 5");
assert.deepStrictEqual(sleepersAt(suspend, 3), [["S · job 1", 5]]);
assert.strictEqual(field(suspend, 1, 5, "State"), "SUSPENDED");
assert.strictEqual(field(suspend, 1, 5, "Waiting for"), "nothing");
assert.deepStrictEqual(sleepersAt(suspend, 5), []);
assert.strictEqual(field(suspend, 1, 7, "State"), "RUNNING");

// Idle and unfinished jobs.
assert.strictEqual(snapshotAt(suspend, 6).runningJobId, null);
const b1 = edf.jobs[edf.jobs.length - 1];
assert.strictEqual(b1.completionTick, null);
assert.notStrictEqual(field(edf, b1.jobId, 8, "State"), "COMPLETED");
assert.strictEqual(field(edf, b1.jobId, 8, "Deadline missed"), "no, not by this tick");

// A tick without a snapshot is reported, never filled from a neighbour.
const gap = load("../runs/priority_inversion-fixed_priority-heap.json");
gap.snapshots.splice(4, 1);
assert.strictEqual(snapshotAt(gap, 4), null);
assert.strictEqual(field(gap, 1, 4, "State"), "no snapshot recorded at tick 4");
assert.deepStrictEqual([jobRowsAt(gap, 4), resourcesAt(gap, 4), sleepersAt(gap, 4), readyAt(gap, 4)], [[], [], [], []]);
assert.strictEqual(snapshotAt(load("fixtures/rr_preemption.design.json"), 0), null); // Fixture without snapshots.

// Every real run: at each tick the snapshot's RUNNING job is the one the CPU chart shows.
for (const f of fs.readdirSync(path.join(__dirname, "../runs")).filter((n) => n.endsWith(".json"))) {
  const run = load(`../runs/${f}`);
  for (let t = 0; t < run.config.horizonTicks; ++t) {
    const segment = run.segments.find((s) => s.start <= t && t < s.end);
    const running = jobRowsAt(run, t).filter((r) => r.state === "RUNNING").map((r) => r.jobId);
    assert.deepStrictEqual(running, segment.jobId === null ? [] : [segment.jobId], `${f} tick ${t}`);
  }
}

// Events: everything at the tick, some context, in order, and readable text.
const near = eventsNear(inversion, 6);
assert.deepStrictEqual(near.filter((e) => e.tick === 6).map((e) => e.type), ["COMPLETE", "DISPATCH", "DEADLINE_MISS"]);
assert(near.length < 30 && near.every((e, i) => i === 0 || e.seq > near[i - 1].seq));
assert.strictEqual(eventsNear(inversion, 4).filter((e) => e.tick === 4).length, 0); // Nothing happens at 4.
assert(eventsNear(inversion, 4).some((e) => e.tick === 2) && eventsNear(inversion, 4).some((e) => e.tick === 6));
assert.deepStrictEqual(eventsNear({ events: [] }, 0), []);
const block = inversion.events.find((e) => e.type === "BLOCK");
assert.strictEqual(describeEvent(inversion, block), "H · job 2: mutex owned, mutex 0, owner L · job 1");
const miss = inversion.events.find((e) => e.type === "DEADLINE_MISS");
assert.strictEqual(describeEvent(inversion, miss), "H · job 2: unfinished at deadline, absoluteDeadline 6");
assert.strictEqual(describeEvent(inversion, { type: "PRIORITY_CHANGE", jobId: 1, resourceId: 0, reason: "inheritance", data: { from: 1, to: 3 } }),
  "L · job 1: effective priority 1 → 3, inheritance, mutex 0");
assert.strictEqual(describeEvent(inversion, { type: "ERROR", jobId: null, resourceId: null, reason: "engine_error", data: {} }), "engine error");

// Inversion ON: golden trace B from the plan (p.15), held as a labelled design fixture.
const on = load("fixtures/inversion_on.design.json");
assert.strictEqual(on.run.evidenceType, "design_fixture");
assert.deepStrictEqual(on.segments.map((s) => [s.start, s.end, s.jobId]), [[0, 1, 1], [1, 4, 1], [4, 5, 2], [5, 9, 3], [9, 10, null]]);
assert.deepStrictEqual(on.jobs.map((j) => [j.completionTick, j.deadlineMissed]), [[4, false], [5, false], [9, false]]);
assert.deepStrictEqual([0, 1, 3].map((t) => field(on, 1, t, "Effective priority")), ["1", "3 (boosted from 1)", "3 (boosted from 1)"]);
assert.strictEqual(field(on, 1, 4, "Effective priority"), "1");
assert.deepStrictEqual([1, 2, 3, 4].map((t) => field(on, 2, t, "State")), ["BLOCKED", "BLOCKED", "BLOCKED", "RUNNING"]);

// Key events explain the two outcomes: a miss when OFF, a donation when ON.
assert.deepStrictEqual(keyEvents(inversion).map((e) => [e.tick, e.type]), [[6, "DEADLINE_MISS"]]);
assert.deepStrictEqual(keyEvents(on).map((e) => [e.tick, e.type, e.data.from, e.data.to]), [[1, "PRIORITY_CHANGE", 1, 3], [4, "PRIORITY_CHANGE", 3, 1]]);

// RM-style and EDF runs share releases but not decisions (plan p.16).
const rmStyle = runFile("rm_edf_compare-fixed_priority-heap");
assert.deepStrictEqual(rmStyle.jobs.map((j) => j.releaseTick), edf.jobs.map((j) => j.releaseTick));
assert.deepStrictEqual(keyEvents(rmStyle).map((e) => [e.tick, e.type, e.jobId]), [[7, "DEADLINE_MISS", 2]]);
assert.deepStrictEqual(keyEvents(edf), []);

// Metrics are shown only as exported; nothing is filled in.
const noMetrics = structuredClone(inversion);
noMetrics.metrics = { overall: {}, perTask: [] };
noMetrics.validation.warnings = ["metrics were not computed for this run"];
assert.deepStrictEqual(overallMetrics(noMetrics), []);
assert.deepStrictEqual(taskMetrics(noMetrics), []);
// Engine-computed metrics of real runs (app/src/metrics.cpp).
assert.deepStrictEqual(overallMetrics(inversion), [["CPU busy", "90% (9 of 10 ticks)"], ["Completed jobs", 3], ["Unfinished jobs", 0], ["Deadline misses", 1],
  ["Periodic demand", "not applicable"], ["RM sufficient bound", "not applicable: one-shot tasks"]]);
assert.deepStrictEqual(taskMetrics(inversion), [["L", 1, 8, 8, 8, "insufficient samples", 0], ["H", 1, 8, 8, 8, "insufficient samples", 1],
  ["M", 1, 4, 4, 4, "insufficient samples", 0]]);
const rm = runFile("rm_edf_compare-rate_monotonic-heap");
assert.deepStrictEqual(overallMetrics(rm), [["CPU busy", "100% (8 of 8 ticks)"], ["Completed jobs", 3], ["Unfinished jobs", 1], ["Deadline misses", 1],
  ["Periodic demand", "97.14%"], ["RM sufficient bound", "82.84%: demand above the bound is inconclusive"]]);
assert.deepStrictEqual(taskMetrics(rm), [["A", 2, 2, 2, 2, 0, 0], ["B", 1, 8, 8, 8, "insufficient samples", 1]]);
assert.deepStrictEqual(overallMetrics(edf).slice(-1), [["RM sufficient bound", "not applicable: policy is edf, not rate_monotonic"]]);
assert.deepStrictEqual(overallMetrics(on), [["CPU busy", "90% (9 of 10 ticks)"], ["Completed jobs", 3], ["Unfinished jobs", 0], ["Deadline misses", 0],
  ["Periodic demand", "not applicable"], ["RM sufficient bound", "not applicable: one-shot tasks that share a mutex"]]);
const withBound = { metrics: { overall: { busyTicks: 8, observedTicks: 8, busyPercent: 100, completedJobs: 3, unfinishedJobs: 1, deadlineMisses: 1,
  demandUtilization: 34 / 35, rmBound: 0.8284271247, rmBoundApplicable: true, rmBoundNote: "demand above the bound is inconclusive" } } };
assert.deepStrictEqual(overallMetrics(withBound).slice(-2), [["Periodic demand", "97.14%"], ["RM sufficient bound", "82.84%: demand above the bound is inconclusive"]]);
assert.deepStrictEqual(overallMetrics({ metrics: { overall: { busyPercent: null, completedJobs: 2 } } })[0], ["CPU busy", "not given"]);
assert.deepStrictEqual(taskMetrics(on)[2], ["M", 1, 7, 7, 7, "insufficient samples", 0]);
const samples = structuredClone(on);
samples.metrics.perTask = [
  { taskId: 0, completedSamples: 0, responseMin: null, responseMean: null, responseMax: null, jitter: null, misses: 2 },
  { taskId: 1, completedSamples: 3, responseMin: 2, responseMean: 8 / 3, responseMax: 4, jitter: 2, misses: 0 },
];
assert.deepStrictEqual(taskMetrics(samples), [["L", 0, "no completed job", "no completed job", "no completed job", "insufficient samples", 2], ["H", 3, 2, 2.67, 4, 2, 0]]);

// Trust notes: a clean run has none; anything else is stated.
assert.deepStrictEqual(runNotes(on), []);
assert.deepStrictEqual(runNotes(inversion), []);
assert.deepStrictEqual(runNotes(noMetrics), ["Warning: metrics were not computed for this run"]);
const stopped = structuredClone(inversion);
stopped.validation = { status: "invalid", error: "event limit", warnings: [], limitsHit: ["maxEvents"], traceTruncated: true };
assert.deepStrictEqual(runNotes(stopped), ["INVALID RUN, do not use as evidence: event limit", "Limit reached: maxEvents", "The trace is truncated: some records are missing."]);

console.log("dashboard selected-tick checks passed");
