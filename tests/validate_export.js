// Checks files written by test_run_export with the dashboard's own validator.
// Run: node tests/validate_export.js <directory written by test_run_export>
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const { validateRun } = require("../dashboard/app.js");

const dir = process.argv[2];
assert(dir, "usage: node tests/validate_export.js <export directory>");
const load = (name) => JSON.parse(fs.readFileSync(path.join(dir, name), "utf8"));

// Golden trace A, written through RunTrace + exporter.
const rr = load("rr_preemption.export.json");
assert.deepStrictEqual(validateRun(rr), []);
const intervals = rr.segments.map((s) => `${s.jobId ?? "idle"}[${s.start},${s.end})`).join(" ");
assert.strictEqual(intervals, "1[0,1) 2[1,2) 3[2,3) 1[3,4) 2[4,5) 1[5,6) 2[6,7) idle[7,8)");
assert.deepStrictEqual(rr.jobs.map((j) => j.completionTick), [6, 7, 3]);
assert.strictEqual(rr.snapshots.length, 9);              // Boundaries 0..8.
assert.strictEqual(rr.snapshots[2].runningJobId, 3);     // H preempts at t=2.
assert.strictEqual(rr.snapshots[1].jobs.length, 2);      // H not yet released.
assert.strictEqual(rr.validation.status, "ok");

// Hostile text survives escaping and parses back to the original.
const odd = load("escaping.export.json");
assert.deepStrictEqual(validateRun(odd), []);
assert.strictEqual(odd.tasks[0].name, 'Quote" back\\slash\nline\ttab <script>&\u0001 é');
assert.strictEqual(odd.jobs[0].completionTick, null);    // Unfinished: null, never the horizon.
assert.strictEqual(odd.validation.status, "invalid");    // Event limit was hit.
assert.deepStrictEqual(odd.validation.limitsHit, ["maxEvents"]);
assert.strictEqual(odd.validation.traceTruncated, true);
console.log("exported run files pass the dashboard validator");
