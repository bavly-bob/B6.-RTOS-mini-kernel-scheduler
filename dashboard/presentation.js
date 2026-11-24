"use strict";

// Presentation only: app.js remains the source of validation and recorded-state lookups.
(() => {
  const $ = (id) => document.getElementById(id);
  const runs = [];
  const reducedMotion = matchMedia("(prefers-reduced-motion: reduce)");
  const palette = ["#6adfd8", "#b4a0f6", "#edb67c", "#e69baf", "#a5c8ff", "#d5fc75"];
  const policies = { fixed_priority: "Fixed priority", rate_monotonic: "RM", edf: "EDF" };
  let current = null, tick = 0, selectedJob = null, position = 0, frame = null, lastTime = null;
  let moveCursor = () => {}, laneButtons = [];

  function el(tag, text, className) {
    const node = document.createElement(tag);
    if (text !== undefined) node.textContent = String(text);
    if (className) node.className = className;
    return node;
  }
  function svgEl(tag, attrs = {}, text) {
    const node = document.createElementNS("http://www.w3.org/2000/svg", tag);
    for (const [key, value] of Object.entries(attrs)) node.setAttribute(key, value);
    if (text !== undefined) node.textContent = String(text);
    return node;
  }
  function fillMeta(target, pairs) {
    target.replaceChildren(...pairs.map(([label, value]) => {
      const pair = el("div");
      pair.append(el("dt", label), el("dd", value ?? "not given"));
      return pair;
    }));
  }
  function titleFor(run) {
    return ({ rr_preemption: "Sharing the CPU", priority_inversion: "Priority inversion",
      semaphore_signal: "Producer & consumer", sleep_order: "Sleeping & waking",
      suspend_resume: "Suspending & resuming", rm_edf_compare: "RM & EDF scheduling",
      overload: "When demand exceeds capacity" })[run.run.scenarioId] || run.run.label || run.run.scenarioId;
  }
  function descriptionFor(run) {
    return ({
      rr_preemption: "Equal-priority jobs share the CPU until a higher-priority job arrives.",
      priority_inversion: run.config.inheritance
        ? "L inherits H’s priority so it can release the mutex sooner."
        : "H waits for L’s mutex while M delays the owner.",
      semaphore_signal: "The consumer waits until the producer signals an available permit.",
      sleep_order: "Sleeping jobs wake in time order, with a stable order when times match.",
      suspend_resume: "Suspension pauses a job independently of its timer wait.",
      rm_edf_compare: run.config.policy === "edf" ? "EDF chooses the ready job with the earliest absolute deadline."
        : run.config.policy === "rate_monotonic" ? "RM gives higher priority to tasks with shorter periods."
        : "Fixed-priority scheduling chooses the ready job with the highest priority.",
      overload: "CPU demand exceeds capacity, exposing deadline misses and unfinished jobs.",
    })[run.run.scenarioId] || "Explore the recorded CPU schedule and each job’s state, one tick at a time.";
  }
  function shortJob(id) {
    const job = current.jobs.find((j) => j.jobId === id);
    return job ? taskOf(current, job.taskId).name : "Unknown";
  }

  function drawGantt() {
    if (!current || $("view").hidden) return;
    const run = current, svg = $("gantt"), box = svg.parentElement;
    const horizon = run.config.horizonTicks, labelW = 94, top = 28, laneH = 54;
    const px = Math.max(2, (box.clientWidth - labelW - 20) / Math.max(horizon, 1));
    const width = labelW + horizon * px + 20;
    const lanes = [...run.jobs.map((job) => ({ id: job.jobId, job })), { id: null, job: null }];
    const axisY = top + lanes.length * laneH, height = axisY + 30;
    const x = (t) => labelW + t * px;
    const laneY = new Map(lanes.map((lane, i) => [lane.id, top + i * laneH]));
    const taskColors = new Map(run.tasks.map((task, i) => [task.taskId, palette[i % palette.length]]));
    const jobMap = new Map(run.jobs.map((job) => [job.jobId, job]));
    svg.replaceChildren();
    svg.setAttribute("width", width);
    svg.setAttribute("height", height);
    svg.setAttribute("viewBox", `0 0 ${width} ${height}`);
    svg.append(svgEl("title", {}, "CPU execution across the full recording. Side panels follow the selected tick."));
    const step = [1, 2, 5, 10, 20, 50, 100].find((n) => n * px >= 40) || 200;
    for (let t = 0; t <= horizon; t += step) {
      svg.append(svgEl("line", { x1: x(t), x2: x(t), y1: top - 8, y2: axisY, class: "gridline" }),
        svgEl("text", { x: x(t), y: 15, "text-anchor": "middle" }, t),
        svgEl("text", { x: x(t), y: axisY + 21, "text-anchor": "middle" }, t));
    }
    if (horizon % step) svg.append(svgEl("text", { x: x(horizon), y: axisY + 21, "text-anchor": "middle" }, horizon));
    for (const lane of lanes) svg.append(svgEl("line", { x1: labelW, x2: x(horizon), y1: laneY.get(lane.id) + laneH, y2: laneY.get(lane.id) + laneH, class: "axis" }));
    for (const segment of run.segments) {
      const job = jobMap.get(segment.jobId), boosted = job && segment.effectivePriority > segment.basePriority;
      const rect = svgEl("rect", { x: x(segment.start), y: laneY.get(segment.jobId) + 13,
        width: Math.max(.5, (segment.end - segment.start) * px - 2), height: 28, rx: 4,
        fill: job ? taskColors.get(job.taskId) : "#31424a", class: boosted ? "boosted" : job ? "cpu-segment" : "idle" });
      rect.append(svgEl("title", {}, `${job ? jobLabel(run, job.jobId) : "Idle"} · [${segment.start}, ${segment.end})${job ? ` · base ${segment.basePriority}, effective ${segment.effectivePriority}` : ""}`));
      if (job) rect.addEventListener("click", () => selectJob(job.jobId));
      svg.append(rect);
      if (boosted && (segment.end - segment.start) * px > 30) svg.append(svgEl("text", { x: x(segment.start) + 7, y: laneY.get(segment.jobId) + 31, class: "boost" }, `P${segment.effectivePriority}`));
    }
    for (const { job, id } of lanes) {
      if (!job) continue;
      const y = laneY.get(id);
      if (job.releaseTick <= horizon) svg.append(svgEl("line", { x1: x(job.releaseTick), x2: x(job.releaseTick), y1: y + 7, y2: y + 46, class: "release" }));
      if (job.absoluteDeadline <= horizon) {
        const marker = svgEl("line", { x1: x(job.absoluteDeadline), x2: x(job.absoluteDeadline), y1: y + 4, y2: y + 48, class: "deadline" });
        marker.append(svgEl("title", {}, `Deadline: tick ${job.absoluteDeadline}`));
        svg.append(marker);
      }
      if (job.deadlineMissTick !== null && job.deadlineMissTick <= horizon) {
        const marker = svgEl("text", { x: x(job.deadlineMissTick) + 4, y: y + 18, class: "miss" }, "×");
        marker.append(svgEl("title", {}, `Deadline miss recorded at tick ${job.deadlineMissTick}`));
        svg.append(marker);
      }
    }
    const cursor = svgEl("line", { y1: top - 4, y2: axisY + 4, class: "cursor" });
    const dot = svgEl("circle", { cy: top - 5, r: 4, class: "cursor-dot" });
    const labels = svgEl("g", { class: "labels" });
    labels.append(svgEl("rect", { x: 0, y: 0, width: labelW - 5, height, class: "labelBack" }));
    laneButtons = [];
    for (const { job, id } of lanes) {
      const y = laneY.get(id), name = job ? taskOf(run, job.taskId).name : "Idle";
      const group = svgEl("g", job ? { class: "lane-pick", tabindex: 0, role: "button", "aria-label": `Inspect ${jobLabel(run, id)}`, "data-job": id } : {});
      group.append(svgEl("rect", { x: 0, y: y + 2, width: labelW - 8, height: laneH - 4, rx: 5, class: "lane-hit" }),
        svgEl("text", { x: 7, y: y + 24, class: "lane-label" }, name.length > 10 ? `${name.slice(0, 9)}…` : name),
        svgEl("text", { x: 7, y: y + 41, class: "lane-sub" }, job ? `JOB ${id}` : "CPU FREE"));
      group.append(svgEl("title", {}, job ? jobLabel(run, id) : "No job executing"));
      if (job) {
        group.addEventListener("click", () => selectJob(id));
        group.addEventListener("keydown", (event) => { if (["Enter", " "].includes(event.key)) { event.preventDefault(); selectJob(id); } });
        laneButtons.push([group, id]);
      }
      labels.append(group);
    }
    svg.append(cursor, dot, labels);
    const pinLabels = () => labels.setAttribute("transform", `translate(${box.scrollLeft})`);
    box.onscroll = pinLabels;
    moveCursor = (t) => {
      cursor.setAttribute("x1", x(t)); cursor.setAttribute("x2", x(t)); dot.setAttribute("cx", x(t));
      if (x(t) < box.scrollLeft + labelW) box.scrollLeft = Math.max(0, x(t) - labelW);
      else if (x(t) > box.scrollLeft + box.clientWidth - 24) box.scrollLeft = x(t) - box.clientWidth + 24;
      pinLabels();
    };
    moveCursor(position);
    highlightJob();
  }
  function highlightJob() {
    for (const [node, id] of laneButtons) {
      node.classList.toggle("selected", id === selectedJob);
      node.setAttribute("aria-pressed", id === selectedJob);
    }
  }
  function queueGroup(title, entries, known) {
    const group = el("section", undefined, "queue-group");
    group.append(el("h3", title));
    if (!known || !entries.length) group.append(el("p", known ? "None" : "Unavailable", "empty"));
    else {
      const list = el("ol", undefined, "queue-list");
      for (const [id, suffix] of entries) {
        const item = el("li"), button = el("button", `${shortJob(id)} · J${id}`);
        button.title = jobLabel(current, id);
        button.append(el("span", suffix));
        button.addEventListener("click", () => selectJob(id));
        item.append(button); list.append(item);
      }
      group.append(list);
    }
    return group;
  }
  function renderInspector() {
    const row = jobRowsAt(current, tick).find((r) => r.jobId === selectedJob);
    const pairs = selectedJob === null ? [] : inspectJob(current, selectedJob, tick);
    const values = new Map(pairs);
    $("jobState").textContent = values.get("State") || "No jobs in this run";
    $("jobState").dataset.state = row?.state || "";
    const edf = current.config.policy === "edf";
    $("priorityLabel").textContent = edf ? "ABSOLUTE DEADLINE" : "EFFECTIVE PRIORITY";
    $("priorityValue").textContent = edf ? values.get("Deadline") ?? "—" : row?.effectivePriority ?? "—";
    $("priorityCard").classList.toggle("donated", Boolean(row && row.effectivePriority > row.basePriority));
    $("priorityNote").textContent = edf ? "EDF chooses the earliest deadline among ready jobs."
      : !row ? "No priority state recorded for this job at this tick."
      : row.effectivePriority > row.basePriority ? `Inherited from a waiter · base priority ${row.basePriority}`
      : `Base priority ${row.basePriority} · higher number runs first`;
    const kept = ["Released at", "Deadline", "Waiting for", "Holds", "Deadline missed"];
    fillMeta($("inspector"), pairs.filter(([key]) => kept.includes(key)));
    for (const pair of $("inspector").children) pair.classList.toggle("alert", pair.firstChild.textContent === "Deadline missed" && row?.deadlineMissed === true);
  }
  function renderTick() {
    const run = current, snap = snapshotAt(run, tick), rows = jobRowsAt(run, tick);
    $("slider").value = tick;
    $("slider").setAttribute("aria-valuetext", `Tick ${tick} of ${run.config.horizonTicks}`);
    $("tickLabel").textContent = `tick ${tick} of ${run.config.horizonTicks}`;
    document.querySelectorAll(".tickNow").forEach((node) => { node.textContent = `t = ${tick}`; });
    $("prev").disabled = $("reset").disabled = tick === 0;
    $("next").disabled = tick === run.config.horizonTicks;
    $("play").disabled = run.config.horizonTicks === 0;
    $("noSnapshot").hidden = snap !== null;
    $("running").textContent = !snap ? "Unavailable" : snap.runningJobId === null ? "Idle" : shortJob(snap.runningJobId);
    $("cpuStatus").textContent = !snap ? "No snapshot at this tick" : snap.runningJobId === null ? "No job on the processor" : `Job ${snap.runningJobId} · running`;
    $("queues").replaceChildren(
      queueGroup("READY · NEXT FIRST", (snap?.readyOrder || []).map((id, i) => [id, `#${i + 1}`]), Boolean(snap)),
      queueGroup("WAITING / SUSPENDED", rows.filter((row) => ["BLOCKED", "SUSPENDED"].includes(row.state)).map((row) => [row.jobId,
        row.state === "SUSPENDED" ? "suspended" : row.blockReason === "timer" ? `wake ${row.wakeTick}` : row.blockReason]), Boolean(snap)));
    const resources = resourcesAt(run, tick);
    $("resources").replaceChildren(...resources.map(([name, owner, waiters]) => {
      const card = el("div", undefined, "resource");
      card.append(el("strong", name), el("p", owner), el("p", `Waiting: ${waiters}`));
      return card;
    }));
    if (!resources.length) $("resources").append(el("p", snap ? "None in this run" : "Unavailable at this tick", "empty"));
    renderInspector();
    highlightJob();
    // Summarize only events at this tick; future outcomes stay out of live state.
    const events = run.events.filter((event) => event.tick === tick);
    const important = events.find((event) => event.type === "ERROR") || events.find((event) => event.type === "DEADLINE_MISS")
      || events.find((event) => event.type === "PRIORITY_CHANGE") || events.find((event) => ["SIGNAL", "UNLOCK", "BLOCK", "SUSPEND", "RESUME", "COMPLETE"].includes(event.type));
    $("tickStory").textContent = important ? eventStory(important)
      : !snap ? "This recording has no state snapshot for the selected tick."
      : tick === run.config.horizonTicks ? "End of recording. The side panels show the final recorded snapshot."
      : snap.runningJobId === null ? "The CPU is idle at this tick."
      : `${jobLabel(run, snap.runningJobId)} uses the CPU during [${tick}, ${tick + 1}).`;
  }
  function eventStory(event) {
    const job = event.jobId === null ? "The engine" : jobLabel(current, event.jobId);
    const resource = event.resourceId === null ? "a resource" : resourceLabel(current, event.resourceId);
    switch (event.type) {
      case "DEADLINE_MISS": return `${job} misses its deadline at tick ${event.tick}.`;
      case "PRIORITY_CHANGE": return Number.isInteger(event.data.from) && Number.isInteger(event.data.to)
        ? `${job} changes effective priority from ${event.data.from} to ${event.data.to}.`
        : `${job} changes effective priority.`;
      case "SIGNAL": return `${job} signals ${resource}.`;
      case "UNLOCK": return `${job} releases ${resource}.`;
      case "BLOCK": return `${job} waits for ${event.resourceId === null ? event.reason.replaceAll("_", " ") : resource}.`;
      case "SUSPEND": return `${job} is suspended.`;
      case "RESUME": return `${job} is resumed; any outstanding wait still applies.`;
      case "COMPLETE": return `${job} finishes at tick ${event.tick}.`;
      default: return `Engine error: ${event.reason.replaceAll("_", " ")}.`;
    }
  }
  function selectJob(id) { selectedJob = id; $("jobSelect").value = id; renderInspector(); highlightJob(); }
  function goTo(value) {
    position = tick = Math.max(0, Math.min(current.config.horizonTicks, Math.floor(value)));
    renderTick(); moveCursor(position);
  }
  function pause() {
    if (frame !== null) cancelAnimationFrame(frame);
    frame = null; lastTime = null;
    $("play").textContent = "Play";
    $("play").setAttribute("aria-label", "Play recording");
  }
  function advance(now) {
    if (lastTime !== null) {
      const tickMs = Math.max(80, Math.min(700, 20000 / Math.max(current.config.horizonTicks, 1)));
      position = Math.min(current.config.horizonTicks, position + (now - lastTime) / tickMs * Number($("speed").value));
    }
    lastTime = now;
    // Interpolate the cursor only. The imported snapshots always remain discrete.
    const nextTick = Math.floor(position);
    if (nextTick !== tick) { tick = nextTick; renderTick(); }
    moveCursor(reducedMotion.matches ? tick : position);
    if (position >= current.config.horizonTicks) pause();
    else frame = requestAnimationFrame(advance);
  }
  function play() {
    if (position >= current.config.horizonTicks) goTo(0);
    $("play").textContent = "Pause";
    $("play").setAttribute("aria-label", "Pause recording");
    lastTime = null; frame = requestAnimationFrame(advance);
  }
  function showViewer() {
    $("welcome").hidden = true; $("view").hidden = false; $("runTools").hidden = false;
    $("returnToRun").hidden = false; document.body.classList.add("in-run");
  }
  function render(run) {
    pause();
    const sameScenario = current?.run.scenarioId === run.run.scenarioId;
    tick = sameScenario ? Math.min(tick, run.config.horizonTicks) : 0;
    position = tick;
    if (!sameScenario || !run.jobs.some((job) => job.jobId === selectedJob)) selectedJob = run.jobs[0]?.jobId ?? null;
    current = run;
    showViewer();
    $("runTitle").textContent = titleFor(run);
    $("runTitle").title = run.run.runId;
    $("runDescription").textContent = descriptionFor(run);
    fillMeta($("runMeta"), [["Policy", policies[run.config.policy]], ["Timer", run.config.timerBackend === "heap" ? "Min-heap" : "Sorted list"], ["Inheritance", run.config.inheritance ? "ON" : "OFF"]]);
    $("runMeta").lastChild.classList.toggle("enabled", run.config.inheritance);
    $("fixtureBanner").hidden = run.run.evidenceType !== "design_fixture";
    const notes = runNotes(run);
    $("runNotes").hidden = notes.length === 0;
    $("runNotes").textContent = notes.join("\n");
    $("runNotes").className = run.validation.status === "invalid" ? "notes invalid" : "notes";
    $("slider").max = run.config.horizonTicks;
    $("jobSelect").replaceChildren(...run.jobs.map((job) => { const option = el("option", jobLabel(run, job.jobId)); option.value = job.jobId; return option; }));
    $("jobSelect").disabled = !run.jobs.length;
    if (selectedJob !== null) $("jobSelect").value = selectedJob;
    drawGantt(); renderTick();
  }
  $("play").addEventListener("click", () => frame === null ? play() : pause());
  $("prev").addEventListener("click", () => { pause(); goTo(tick - 1); });
  $("next").addEventListener("click", () => { pause(); goTo(tick + 1); });
  $("reset").addEventListener("click", () => { pause(); goTo(0); });
  $("slider").addEventListener("input", (event) => { pause(); goTo(Number(event.target.value)); });
  $("jobSelect").addEventListener("change", (event) => selectJob(Number(event.target.value)));
  $("runSelect").addEventListener("change", (event) => render(runs[Number(event.target.value)].data));
  $("focusChart").addEventListener("click", () => {
    const focused = document.body.classList.toggle("focus-chart");
    $("focusChart").setAttribute("aria-pressed", focused);
    $("focusChart").textContent = focused ? "Show side panels" : "Focus chart";
    drawGantt();
  });
  $("home").addEventListener("click", () => {
    pause(); $("view").hidden = true; $("welcome").hidden = false; $("runTools").hidden = true;
    document.body.classList.remove("in-run"); $("importStart").focus();
  });
  $("returnToRun").addEventListener("click", () => { showViewer(); drawGantt(); $("runTitle").focus(); });
  for (const id of ["importStart", "importMore"]) $(id).addEventListener("click", () => { pause(); $("file").click(); });
  document.addEventListener("visibilitychange", () => { if (document.hidden) pause(); });
  new ResizeObserver(() => drawGantt()).observe($("gantt").parentElement);
  $("file").addEventListener("change", async (event) => {
    const problems = [], files = Array.from(event.target.files);
    let lastAdded = -1;
    for (const id of ["importStart", "importMore"]) $(id).disabled = true;
    try {
      for (const file of files) {
        if (file.size > MAX_FILE_BYTES) { problems.push(`${file.name}: ${file.size} bytes exceeds the 32 MiB limit.`); continue; }
        try {
          const { data, errors } = parseRun(await file.text());
          if (errors.length) { problems.push(`${file.name} rejected:\n  • ${errors.slice(0, 10).join("\n  • ")}${errors.length > 10 ? `\n  • …and ${errors.length - 10} more` : ""}`); continue; }
          runs.push({ name: file.name, data }); lastAdded = runs.length - 1;
        } catch (error) { problems.push(`${file.name}: could not read this file (${error.message}).`); }
      }
      $("errors").textContent = problems.join("\n\n");
      if (lastAdded >= 0) {
        $("runSelect").replaceChildren(...runs.map(({ name, data }, index) => {
          const option = el("option", `${titleFor(data)} · ${policies[data.config.policy]} · ${data.config.timerBackend === "heap" ? "Heap" : "List"} · PI ${data.config.inheritance ? "ON" : "OFF"}${data.run.evidenceType === "design_fixture" ? " (sample)" : ""}`);
          option.value = index; option.title = `${data.run.runId} · ${name}`; return option;
        }));
        $("runSelect").disabled = false; $("runSelect").value = lastAdded;
        render(runs[lastAdded].data); $("runTitle").focus();
      }
    } finally {
      event.target.value = ""; // Permit re-importing the same file after an edit.
      for (const id of ["importStart", "importMore"]) $(id).disabled = false;
    }
  });
})();
