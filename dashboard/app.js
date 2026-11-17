"use strict";
// B6 Scheduler Dashboard: imports schemaVersion 1 run files and displays them.
// It never makes or recomputes scheduling decisions; every number shown comes from the file.
// All file text is inserted with textContent, never as HTML.

const MAX_FILE_BYTES = 32 * 1024 * 1024;
const REQUIRED_ARRAYS = ["tasks", "jobs", "segments", "events", "snapshots"];
const REQUIRED_OBJECTS = ["run", "config", "metrics", "validation"];

const isInt = (v) => Number.isInteger(v);
const isIntOrNull = (v) => v === null || Number.isInteger(v);

// Returns a list of human-readable problems; an empty list means the run is accepted.
function validateRun(run) {
  const errs = [];
  if (run === null || typeof run !== "object" || Array.isArray(run)) return ["Top level must be a JSON object."];
  if (run.schemaVersion !== 1) return [`schemaVersion is ${JSON.stringify(run.schemaVersion)}; this dashboard supports only 1.`];
  for (const k of REQUIRED_OBJECTS) if (!run[k] || typeof run[k] !== "object" || Array.isArray(run[k])) errs.push(`Missing object "${k}".`);
  for (const k of REQUIRED_ARRAYS) if (!Array.isArray(run[k])) errs.push(`Missing array "${k}".`);
  if (errs.length) return errs;

  const ev = run.run.evidenceType;
  if (ev !== "engine_run" && ev !== "design_fixture") errs.push(`run.evidenceType must be "engine_run" or "design_fixture", got ${JSON.stringify(ev)}.`);
  const horizon = run.config.horizonTicks;
  if (!isInt(horizon) || horizon < 0) errs.push("config.horizonTicks must be a nonnegative integer.");

  const taskIds = new Set();
  run.tasks.forEach((t, i) => {
    if (!isInt(t.taskId)) errs.push(`tasks[${i}].taskId must be an integer.`);
    else if (taskIds.has(t.taskId)) errs.push(`tasks[${i}]: duplicate taskId ${t.taskId}.`);
    taskIds.add(t.taskId);
  });

  const jobIds = new Set();
  run.jobs.forEach((j, i) => {
    if (!isInt(j.jobId)) errs.push(`jobs[${i}].jobId must be an integer.`);
    else if (jobIds.has(j.jobId)) errs.push(`jobs[${i}]: duplicate jobId ${j.jobId}.`);
    jobIds.add(j.jobId);
    if (!taskIds.has(j.taskId)) errs.push(`jobs[${i}]: unknown taskId ${j.taskId}.`);
    for (const f of ["releaseTick", "absoluteDeadline"]) if (!isInt(j[f])) errs.push(`jobs[${i}].${f} must be an integer.`);
    for (const f of ["firstStartTick", "completionTick", "deadlineMissTick"]) if (!isIntOrNull(j[f])) errs.push(`jobs[${i}].${f} must be an integer or null.`);
  });

  let prevEnd = null;
  run.segments.forEach((s, i) => {
    if (!isInt(s.start) || !isInt(s.end) || s.start < 0 || s.start >= s.end) {
      errs.push(`segments[${i}]: need integers with 0 <= start < end (got ${s.start}, ${s.end}).`);
      return;
    }
    if (isInt(horizon) && s.end > horizon) errs.push(`segments[${i}]: end ${s.end} is past horizon ${horizon}.`);
    if (prevEnd !== null && s.start < prevEnd) errs.push(`segments[${i}]: overlaps or is out of order (starts ${s.start}, previous ended ${prevEnd}).`);
    prevEnd = s.end;
    if (s.jobId !== null && !jobIds.has(s.jobId)) errs.push(`segments[${i}]: unknown jobId ${s.jobId}.`);
  });
  return errs;
}

if (typeof module !== "undefined") module.exports = { validateRun };

if (typeof document !== "undefined") {
  const $ = (id) => document.getElementById(id);
  const runs = []; // { name, data }

  const el = (tag, text, cls) => {
    const e = document.createElement(tag);
    if (text !== undefined) e.textContent = String(text);
    if (cls) e.className = cls;
    return e;
  };
  const show = (v, none = "—") => (v === null || v === undefined ? none : v);

  function fillMeta(dl, pairs) {
    dl.replaceChildren();
    for (const [k, v] of pairs) {
      const pair = el("div");
      pair.append(el("dt", k), el("dd", show(v, "not given")));
      dl.append(pair);
    }
  }

  function fillTable(table, headers, rows) {
    table.replaceChildren();
    const head = el("tr");
    headers.forEach((h) => head.append(el("th", h)));
    table.append(el("thead"));
    table.tHead.append(head);
    const body = el("tbody");
    for (const row of rows) {
      const tr = el("tr");
      for (const cell of row) {
        const missing = cell === null || cell === undefined;
        tr.append(el("td", missing ? cell === null ? "n/a" : "—" : cell, missing ? "na" : undefined));
      }
      body.append(tr);
    }
    table.append(body);
  }

  const NS = "http://www.w3.org/2000/svg";
  function svgEl(tag, attrs, text) {
    const e = document.createElementNS(NS, tag);
    for (const [k, v] of Object.entries(attrs)) e.setAttribute(k, v);
    if (text !== undefined) e.textContent = String(text);
    return e;
  }

  function taskColor(index, count) {
    return `hsl(${Math.round((index * 360) / Math.max(count, 1))} 60% 50%)`;
  }

  function drawGantt(run) {
    const svg = $("gantt");
    svg.replaceChildren();
    const horizon = run.config.horizonTicks;
    const taskIndex = new Map(run.tasks.map((t, i) => [t.taskId, i]));
    const taskName = new Map(run.tasks.map((t) => [t.taskId, t.name]));
    const lanes = run.jobs.map((j) => ({ key: j.jobId, label: `${taskName.get(j.taskId)} · job ${j.jobId}`, job: j }));
    lanes.push({ key: "idle", label: "idle", job: null });

    const labelW = 120, laneH = 26, top = 4, axisH = 22;
    const px = Math.max(8, Math.min(48, Math.floor(800 / Math.max(horizon, 1))));
    const width = labelW + horizon * px + 10;
    const height = top + lanes.length * laneH + axisH;
    svg.setAttribute("width", width);
    svg.setAttribute("height", height);
    svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    const x = (t) => labelW + t * px;
    const laneY = new Map(lanes.map((l, i) => [l.key, top + i * laneH]));

    lanes.forEach((l) => {
      const y = laneY.get(l.key);
      svg.append(svgEl("text", { x: 4, y: y + laneH / 2 + 4 }, l.label));
      svg.append(svgEl("line", { x1: labelW, x2: x(horizon), y1: y + laneH, y2: y + laneH, class: "axis" }));
    });

    for (const s of run.segments) {
      const key = s.jobId === null ? "idle" : s.jobId;
      const job = s.jobId === null ? null : run.jobs.find((j) => j.jobId === s.jobId);
      const y = laneY.get(key) + 4;
      const rect = svgEl("rect", { x: x(s.start), y, width: (s.end - s.start) * px - 1, height: laneH - 8, rx: 2 });
      if (job) rect.setAttribute("fill", taskColor(taskIndex.get(job.taskId), run.tasks.length));
      else rect.setAttribute("class", "idle");
      const pri = job ? `, priority base ${show(s.basePriority)} / effective ${show(s.effectivePriority)}` : "";
      rect.append(svgEl("title", {}, `[${s.start}, ${s.end}) ${key === "idle" ? "idle" : "job " + key}${pri}`));
      svg.append(rect);
    }

    for (const l of lanes) {
      if (!l.job) continue;
      const y = laneY.get(l.key);
      if (l.job.releaseTick <= horizon) svg.append(svgEl("line", { x1: x(l.job.releaseTick), x2: x(l.job.releaseTick), y1: y + 2, y2: y + laneH - 2, class: "release" }));
      if (l.job.absoluteDeadline <= horizon) {
        const d = svgEl("line", { x1: x(l.job.absoluteDeadline), x2: x(l.job.absoluteDeadline), y1: y, y2: y + laneH, class: "deadline" });
        d.append(svgEl("title", {}, `deadline ${l.job.absoluteDeadline}`));
        svg.append(d);
      }
    }

    const axisY = top + lanes.length * laneH;
    const step = Math.max(1, Math.ceil(30 / px));
    for (let t = 0; t <= horizon; t += step) svg.append(svgEl("text", { x: x(t) - 3, y: axisY + 15 }, t));
  }

  function render(run) {
    $("view").hidden = false;
    $("fixtureBanner").hidden = run.run.evidenceType !== "design_fixture";
    const c = run.config, r = run.run;
    fillMeta($("runMeta"), [
      ["Run ID", r.runId], ["Scenario", r.scenarioId], ["Label", r.label], ["Evidence", r.evidenceType],
      ["Engine revision", r.engineRevision], ["Policy", c.policy], ["Timer backend", c.timerBackend],
      ["Inheritance", c.inheritance === undefined ? undefined : c.inheritance ? "on" : "off"],
      ["Quantum (ticks)", c.quantumTicks], ["Horizon (ticks)", c.horizonTicks], ["Tick (ms)", c.tickMs],
      ["Switch cost (ticks)", c.contextSwitchCostTicks], ["Run status", run.validation.status],
      ["Trace truncated", run.validation.traceTruncated === undefined ? undefined : String(run.validation.traceTruncated)],
    ]);
    drawGantt(run);

    fillTable($("tasks"), ["ID", "Name", "Base priority", "First release", "Period", "Rel. deadline", "Script"],
      run.tasks.map((t) => [t.taskId, t.name, t.basePriority, t.firstRelease, t.periodTicks === 0 ? "one-shot" : t.periodTicks, t.relativeDeadline, t.script]));

    const taskName = new Map(run.tasks.map((t) => [t.taskId, t.name]));
    fillTable($("jobs"), ["Job", "Task", "Release", "Deadline", "First start", "Completion", "Response", "CPU used", "Final state", "Missed"],
      run.jobs.map((j) => [j.jobId, taskName.get(j.taskId), j.releaseTick, j.absoluteDeadline,
        show(j.firstStartTick, "not started"), show(j.completionTick, "not completed"),
        // Display-only subtraction of two exported ticks; null when unfinished, never the horizon.
        j.completionTick === null ? "not completed" : j.completionTick - j.releaseTick,
        j.totalCpuUsed, j.finalState, j.deadlineMissed ? `yes @ ${show(j.deadlineMissTick)}` : "no"]));

    const m = run.metrics;
    fillMeta($("overall"), Object.entries(m.overall || {}));
    fillTable($("perTask"), ["Task", "Completed samples", "Response min", "mean", "max", "Jitter (max−min)", "Misses"],
      (m.perTask || []).map((p) => [taskName.get(p.taskId) ?? p.taskId, p.completedSamples, p.responseMin, p.responseMean, p.responseMax,
        p.jitter === null && p.completedSamples < 2 ? "insufficient samples" : p.jitter, p.misses]));
  }

  function refreshSelect(selectIndex) {
    const sel = $("runSelect");
    sel.replaceChildren(...runs.map((r, i) => {
      const o = el("option", `${r.data.run.runId ?? r.name}${r.data.run.evidenceType === "design_fixture" ? " (sample)" : ""}`);
      o.value = i;
      return o;
    }));
    sel.disabled = runs.length === 0;
    sel.value = selectIndex;
    render(runs[selectIndex].data);
  }

  $("runSelect").addEventListener("change", (e) => render(runs[Number(e.target.value)].data));

  $("file").addEventListener("change", async (e) => {
    const problems = [];
    let lastAdded = -1;
    for (const file of e.target.files) {
      if (file.size > MAX_FILE_BYTES) { problems.push(`${file.name}: ${file.size} bytes exceeds the 32 MiB limit.`); continue; }
      let data;
      try { data = JSON.parse(await file.text()); }
      catch (err) { problems.push(`${file.name}: not valid JSON (${err.message}).`); continue; }
      const errs = validateRun(data);
      if (errs.length) { problems.push(`${file.name} rejected:\n  • ${errs.slice(0, 10).join("\n  • ")}${errs.length > 10 ? `\n  • …and ${errs.length - 10} more` : ""}`); continue; }
      runs.push({ name: file.name, data });
      lastAdded = runs.length - 1;
    }
    $("errors").textContent = problems.join("\n\n");
    if (lastAdded >= 0) refreshSelect(lastAdded);
    e.target.value = ""; // allow re-importing the same file
  });
}
