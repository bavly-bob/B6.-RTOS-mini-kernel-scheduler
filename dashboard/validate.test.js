// Run: node dashboard/validate.test.js
const assert = require("assert");
const path = require("path");
const { validateRun } = require("./app.js");

const fixture = () => JSON.parse(require("fs").readFileSync(path.join(__dirname, "fixtures/rr_preemption.design.json"), "utf8"));
const rejects = (mutate, pattern) => {
  const run = fixture();
  mutate(run);
  const errs = validateRun(run);
  assert(errs.some((e) => pattern.test(e)), `expected ${pattern}, got ${JSON.stringify(errs)}`);
};

assert.deepStrictEqual(validateRun(fixture()), []);
rejects((r) => { r.schemaVersion = 2; }, /schemaVersion/);
rejects((r) => { delete r.segments; }, /Missing array "segments"/);
rejects((r) => { r.jobs[1].jobId = 1; }, /duplicate jobId/);
rejects((r) => { r.jobs[0].taskId = 99; }, /unknown taskId/);
rejects((r) => { r.segments[0].jobId = 42; }, /unknown jobId/);
rejects((r) => { r.segments[1].start = 0; }, /overlaps/);
rejects((r) => { r.segments[0].end = 0; }, /start < end/);
rejects((r) => { r.segments[7].end = 9; }, /past horizon/);
rejects((r) => { r.jobs[0].completionTick = "6"; }, /integer or null/);
rejects((r) => { r.run.evidenceType = "guess"; }, /evidenceType/);
assert.deepStrictEqual(validateRun([]), ["Top level must be a JSON object."]);
console.log("dashboard validation checks passed");
