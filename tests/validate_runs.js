// Checks every real engine run written by test_scheduler with the dashboard's
// own import validator, so the C++ output and the GUI agree on the contract.
// Run: node tests/validate_runs.js <directory written by test_scheduler>
"use strict";
const assert = require("assert");
const fs = require("fs");
const path = require("path");
const { validateRun } = require("../dashboard/app.js");

const dir = process.argv[2];
assert(dir, "usage: node tests/validate_runs.js <run directory>");
const files = fs.readdirSync(dir).filter((f) => f.endsWith(".json"));
assert(files.length > 0, `no run files in ${dir}`);

for (const file of files) {
  const run = JSON.parse(fs.readFileSync(path.join(dir, file), "utf8"));
  assert.deepStrictEqual(validateRun(run), [], `${file} fails the dashboard validator`);
  assert.strictEqual(run.run.evidenceType, "engine_run", `${file} must be an engine_run`);
  assert.strictEqual(run.validation.status, "ok", `${file} is not a valid run`);
  assert.strictEqual(run.validation.traceTruncated, false, `${file} is truncated`);
  assert.strictEqual(run.snapshots.length, run.config.horizonTicks + 1, `${file} snapshot count`);
  const busy = run.segments.reduce((n, s) => n + (s.end - s.start), 0);
  assert.strictEqual(busy, run.config.horizonTicks, `${file} segments must cover the horizon`);
}
console.log(`${files.length} engine runs pass the dashboard validator`);
