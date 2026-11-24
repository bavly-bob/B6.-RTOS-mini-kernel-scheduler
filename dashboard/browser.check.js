// Drives dashboard/index.html in a real headless Chrome/Edge, opened from disk with the network off.
// Run: node dashboard/browser.check.js [screenshot directory]      (Node 22+)
// Set BROWSER to the browser executable if it is not found automatically.
"use strict";
const assert = require("assert");
const { spawn } = require("child_process");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { pathToFileURL } = require("url");

const shots = process.argv[2];
const repo = path.join(__dirname, "..");
const browser = [process.env.BROWSER,
  "C:/Program Files/Google/Chrome/Application/chrome.exe",
  "C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe",
  "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
  "/usr/bin/google-chrome", "/usr/bin/chromium", "/usr/bin/chromium-browser"].find((p) => p && fs.existsSync(p));
assert(browser, "no Chrome/Edge found; set BROWSER to its executable");

const temp = fs.mkdtempSync(path.join(os.tmpdir(), "b6-dashboard-"));
const tempFile = (name, content) => {
  const p = path.join(temp, name);
  fs.writeFileSync(p, content);
  return p;
};
const runPath = (name) => path.join(repo, "runs", `${name}.json`);
const readRun = (p) => JSON.parse(fs.readFileSync(p, "utf8"));

const child = spawn(browser, ["--headless=new", "--disable-gpu", "--no-first-run", "--no-default-browser-check",
  "--remote-debugging-port=0", `--user-data-dir=${path.join(temp, "profile")}`, "about:blank"], { stdio: ["ignore", "ignore", "pipe"] });

async function connect() {
  const endpoint = await new Promise((resolve, reject) => {
    let log = "";
    child.stderr.on("data", (d) => {
      log += d;
      const m = log.match(/DevTools listening on ws:\/\/([^/]+)\//);
      if (m) resolve(m[1]);
    });
    child.on("exit", () => reject(new Error(`browser exited early: ${log}`)));
  });
  const page = (await (await fetch(`http://${endpoint}/json`)).json()).find((t) => t.type === "page");
  const ws = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve) => ws.addEventListener("open", resolve));
  const waiting = new Map(), problems = [], requests = [];
  let id = 0;
  ws.addEventListener("message", ({ data }) => {
    const m = JSON.parse(data);
    if (m.id) { waiting.get(m.id)(m); waiting.delete(m.id); }
    if (m.method === "Runtime.exceptionThrown") problems.push(m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text);
    if (m.method === "Network.requestWillBeSent") requests.push(m.params.request.url);
  });
  const send = async (method, params = {}) => {
    const reply = await new Promise((resolve) => { waiting.set(++id, resolve); ws.send(JSON.stringify({ id, method, params })); });
    if (reply.error) throw new Error(`${method}: ${reply.error.message}`);
    return reply.result;
  };
  const js = async (expression) => {
    const r = await send("Runtime.evaluate", { expression, returnByValue: true, awaitPromise: true });
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? expression);
    return r.result.value;
  };
  return { send, js, problems, requests };
}

async function main() {
  const { send, js, problems, requests } = await connect();
  const pause = (ms) => new Promise((r) => setTimeout(r, ms));
  const until = async (expression, what) => {
    for (let i = 0; i < 100; ++i) { if (await js(expression)) return; await pause(50); }
    throw new Error(`timed out waiting for ${what}`);
  };
  for (const d of ["Page", "Runtime", "Network", "DOM"]) await send(`${d}.enable`);
  await send("Network.emulateNetworkConditions", { offline: true, latency: 0, downloadThroughput: 0, uploadThroughput: 0 });
  await send("Emulation.setDeviceMetricsOverride", { width: 1360, height: 1000, deviceScaleFactor: 1, mobile: false });
  await send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-color-scheme", value: "light" }] });
  await send("Page.navigate", { url: pathToFileURL(path.join(__dirname, "index.html")).href });
  await until("document.readyState === 'complete' && location.protocol === 'file:'", "page load");

  const text = (id) => js(`document.getElementById("${id}").innerText`);
  const click = (id) => js(`document.getElementById("${id}").click()`);
  const importFiles = async (files) => {
    const { root } = await send("DOM.getDocument");
    const { nodeId } = await send("DOM.querySelector", { nodeId: root.nodeId, selector: "#file" });
    await js(`window.__done = false; document.getElementById("file").addEventListener("change", () => { const wait = setInterval(() => { if (document.getElementById("file").value === "") { clearInterval(wait); window.__done = true; } }, 10); }, { once: true })`);
    await send("DOM.setFileInputFiles", { nodeId, files });
    await until("window.__done", `import of ${files.join(", ")}`);
  };
  const goTo = (t) => js(`(() => { const s = document.getElementById("slider"); s.value = ${t}; s.dispatchEvent(new Event("input")); })()`);
  const pickJob = (id) => js(`(() => { const s = document.getElementById("jobSelect"); s.value = ${id}; s.dispatchEvent(new Event("change")); })()`);
  const pickRun = (i) => js(`(() => { const s = document.getElementById("runSelect"); s.value = ${i}; s.dispatchEvent(new Event("change")); })()`);
  const shot = async (name) => {
    if (!shots) return;
    fs.mkdirSync(shots, { recursive: true });
    const { cssContentSize } = await send("Page.getLayoutMetrics");
    const { data } = await send("Page.captureScreenshot", { format: "png", captureBeyondViewport: true,
      clip: { x: 0, y: 0, width: cssContentSize.width, height: cssContentSize.height, scale: 1 } });
    fs.writeFileSync(path.join(shots, `${name}.png`), Buffer.from(data, "base64"));
  };
  const inspector = async (label) => js("[...document.querySelectorAll('#inspector > div')].find((d) => d.children[0].textContent === " + JSON.stringify(label) + ")?.children[1].textContent ?? null");
  const expect = async (label, value) => assert.strictEqual(await inspector(label), value, "inspector " + label);
  const state = async (value) => assert.strictEqual(await text("jobState"), value);
  const noOverflow = async () => assert(await js("document.documentElement.scrollWidth <= innerWidth"), "page must fit viewport");
  const cursorX = () => js("Number(document.querySelector('#gantt .cursor').getAttribute('x1'))");
  await js("document.fonts.ready");
  await pause(1100);
  assert.strictEqual(await js("document.getElementById('welcome').hidden"), false);
  assert.strictEqual(await js("document.getElementById('view').hidden"), true);
  for (const name of ["Moataz", "Youssef Gohar", "Marc", "Sarofem", "Bavly"])
    assert((await text("welcome")).includes(name));
  await noOverflow();
  await shot("welcome-desktop");

  // Rejected files do not replace the welcome screen or an existing valid recording.
  const bad = readRun(runPath("priority_inversion-fixed_priority-heap"));
  const broken = (name, change) => {
    const run = structuredClone(bad);
    change(run);
    return tempFile(name, JSON.stringify(run));
  };
  await importFiles([
    tempFile("notes.json", "this is not a run"),
    broken("future.json", (r) => { r.schemaVersion = 2; }),
    broken("overlap.json", (r) => { r.segments[1].start = 0; }),
    broken("two-running.json", (r) => { r.snapshots[2].jobs[0].state = "RUNNING"; }),
    broken("null-row.json", (r) => { r.jobs[0] = null; }),
    tempFile("huge.json", Buffer.alloc(32 * 1024 * 1024 + 1, 32)),
  ]);
  const errors = await text("errors");
  for (const part of ["notes.json", "not valid JSON", "future.json", "schemaVersion is 2", "overlap.json", "overlaps",
    "two-running.json", "more than one RUNNING", "null-row.json", "jobs[0] must be an object", "huge.json", "32 MiB"])
    assert(errors.includes(part), "error message lacks " + part);
  assert.strictEqual(await js("document.getElementById('view').hidden"), true);
  await shot("rejected-files");

  const inversion = runPath("priority_inversion-fixed_priority-heap");
  await importFiles([inversion, tempFile("notes.json", "{")]);
  assert((await text("errors")).includes("notes.json"));
  await importFiles([inversion]);
  assert.strictEqual(await text("errors"), "");
  assert.strictEqual(await js("document.getElementById('fixtureBanner').hidden"), true);
  assert.strictEqual(await js("document.getElementById('welcome').hidden"), true);
  assert.match(await text("runMeta"), /Fixed priority/);
  assert.match(await text("runTitle"), /Priority inversion/);
  assert.match(await text("runDescription"), /M delays/);
  assert.strictEqual(await js("document.querySelectorAll('#states,#tasks,#jobs,#overall,#perTask,#events,#keyEvents').length"), 0);

  // Historical job and resource state must not leak the final outcome.
  await pickJob(2);
  await state("not released yet (releases at 1)");
  assert.strictEqual(await text("priorityValue"), "—");
  await goTo(4);
  await state("BLOCKED");
  await expect("Waiting for", "mutex 0, owned by L · job 1");
  await expect("Deadline missed", "no, not by this tick");
  assert.match(await text("resources"), /mutex 0\s+owner L · job 1\s+Waiting: H · job 2/);
  await goTo(6);
  await expect("Deadline missed", "yes");
  assert.match(await text("tickStory"), /misses its deadline/);

  await click("next");
  assert.strictEqual(await text("tickLabel"), "tick 7 of 10");
  await click("prev"); await click("prev");
  assert.strictEqual(await text("tickLabel"), "tick 5 of 10");
  await click("reset");
  assert.strictEqual(await js("document.getElementById('prev').disabled"), true);

  // A cursor moves between ticks without inventing intermediate state.
  await js("document.getElementById('speed').value = '0.5'");
  const initialX = await cursorX();
  await click("play"); await pause(180);
  const firstX = await cursorX();
  await pause(180);
  const secondX = await cursorX();
  assert(firstX > initialX && secondX > firstX, "cursor must move smoothly inside one tick");
  assert.strictEqual(await text("tickLabel"), "tick 0 of 10");
  await state("not released yet (releases at 1)");
  await click("play");
  const stoppedX = await cursorX(), stoppedTick = await text("tickLabel");
  await pause(350);
  assert.strictEqual(await cursorX(), stoppedX);
  assert.strictEqual(await text("tickLabel"), stoppedTick);
  await js("document.getElementById('speed').value = '4'");
  await goTo(9); await click("play");
  await until("document.getElementById('slider').value === '10' && document.getElementById('play').textContent === 'Play'", "end of recording");
  assert.strictEqual(await js("document.getElementById('next').disabled"), true);
  await click("play");
  await until("Number(document.getElementById('slider').value) < 10", "replay from start");
  await click("play");

  // Home preserves imported runs, chosen job and tick.
  await goTo(4);
  await click("home");
  assert.strictEqual(await js("document.getElementById('view').hidden"), true);
  await click("returnToRun");
  assert.strictEqual(await text("tickLabel"), "tick 4 of 10");
  await state("BLOCKED");
  await click("focusChart");
  assert.strictEqual(await js("getComputedStyle(document.querySelector('.live-panel')).display"), "none");
  await click("focusChart");
  assert.notStrictEqual(await js("getComputedStyle(document.querySelector('.live-panel')).display"), "none");
  await js("document.querySelector('#gantt .lane-pick[data-job=\"1\"]').dispatchEvent(new KeyboardEvent('keydown', {key:'Enter',bubbles:true}))");
  assert.strictEqual(await js("document.getElementById('jobSelect').value"), "1");

  // Suspension and EDF keep their exact recorded state and display the correct urgency.
  await importFiles([runPath("suspend_resume-fixed_priority-heap"), runPath("rm_edf_compare-edf-heap")]);
  assert.match(await text("runMeta"), /EDF/);
  await goTo(8); await pickJob(4); await state("READY");
  assert.strictEqual(await text("priorityLabel"), "ABSOLUTE DEADLINE");
  const count = await js("document.getElementById('runSelect').options.length");
  await pickRun(count - 2); await goTo(5); await pickJob(1);
  await state("SUSPENDED"); await expect("Waiting for", "nothing");
  await goTo(6);
  assert.strictEqual(await text("running"), "Idle");

  // Real engine OFF/ON runs keep the tick and selection for a direct comparison.
  await importFiles([inversion]); await goTo(3); await pickJob(1);
  assert.strictEqual(await text("priorityValue"), "1");
  assert.strictEqual(await js("document.querySelectorAll('#gantt .miss').length"), 1);
  assert.strictEqual(await js("document.querySelectorAll('#gantt rect.boosted').length"), 0);
  await importFiles([runPath("priority_inversion-fixed_priority-heap-inheritance")]);
  assert.strictEqual(await text("tickLabel"), "tick 3 of 10");
  assert.strictEqual(await js("document.getElementById('jobSelect').value"), "1");
  assert.strictEqual(await text("priorityValue"), "3");
  assert.match(await text("priorityNote"), /Inherited from a waiter/);
  assert.strictEqual(await js("document.querySelectorAll('#gantt rect.boosted').length"), 1);
  assert.strictEqual(await js("document.querySelectorAll('#gantt .miss').length"), 0);
  assert.strictEqual(await js("document.getElementById('fixtureBanner').hidden"), true);
  await shot("inheritance-on-t3");
  await goTo(4);
  assert.strictEqual(await text("priorityValue"), "1");
  await pickJob(2); await goTo(5); await state("COMPLETED");
  await importFiles([inversion]);
  await state("BLOCKED");
  await goTo(9); await state("COMPLETED");

  // Missing metrics and invalid recordings retain their exported warnings.
  await importFiles([broken("no_metrics.json", (r) => {
    r.metrics = { overall: {}, perTask: [] };
    r.validation.warnings = ["metrics were not computed for this run"];
  })]);
  assert.match(await text("runNotes"), /metrics were not computed/);
  await importFiles([broken("invalid.json", (r) => { r.validation.status = "invalid"; r.validation.error = "stopped early"; })]);
  assert.match(await text("runNotes"), /INVALID RUN, do not use as evidence: stopped early/);
  assert.strictEqual(await js("document.getElementById('runNotes').className"), "notes invalid");

  // Names remain plain text in the title, selector, cards and SVG.
  await importFiles([broken("hostile.json", (r) => {
    r.run.scenarioId = "custom";
    r.run.label = r.tasks[0].name = '<img src=x onerror="window.__hacked=1"><script>window.__hacked=1</script>';
  })]);
  assert((await text("runTitle")).includes("<img src=x"));
  assert.strictEqual(await js("document.querySelectorAll('main img, main script').length"), 0);
  assert.strictEqual(await js("window.__hacked ?? null"), null);

  await importFiles([path.join(__dirname, "fixtures", "inversion_on.design.json")]);
  assert.match(await text("fixtureBanner"), /SAMPLE DATA/);
  await importFiles([path.join(__dirname, "fixtures", "rr_preemption.design.json")]);
  assert.strictEqual(await js("document.getElementById('noSnapshot').hidden"), false);
  assert.strictEqual(await text("running"), "Unavailable");

  // Long recordings scroll horizontally, with lane labels pinned beside the cursor.
  const chart = "document.getElementById('gantt')";
  await importFiles([runPath("sleep_order-fixed_priority-heap")]);
  assert.strictEqual(await js(chart + ".parentElement.scrollWidth <= " + chart + ".parentElement.clientWidth"), true);
  assert.match(await text("resources"), /None/);
  const long = readRun(runPath("sleep_order-fixed_priority-heap"));
  long.config.horizonTicks = 2000;
  await importFiles([tempFile("long.json", JSON.stringify(long))]); await goTo(1900);
  await until(chart + ".parentElement.scrollLeft > 0", "cursor following");
  await until("Math.abs(" + chart + ".querySelector('g.labels text').getBoundingClientRect().left - " + chart + ".parentElement.getBoundingClientRect().left) < 20", "pinned lane labels");

  // A zero-length, empty recording remains usable and cannot start playback.
  await importFiles([broken("empty.json", (r) => {
    for (const key of ["tasks", "jobs", "segments", "events", "snapshots"]) r[key] = [];
    r.config.horizonTicks = 0; r.metrics = { overall: {}, perTask: [] };
  })]);
  assert.strictEqual(await js("document.getElementById('play').disabled"), true);
  assert.strictEqual(await text("jobState"), "No jobs in this run");

  // Preserve settings and test actual engine examples, including both timer backends.
  await pickRun(0);
  assert.match(await text("runMeta"), /Fixed priority\s+Timer\s+Min-heap\s+Inheritance\s+OFF/i);
  for (const [name, t, job] of [["rr_preemption-fixed_priority-heap", 2, 3], ["sleep_order-fixed_priority-heap", 115, 1],
    ["suspend_resume-fixed_priority-heap", 5, 1], ["semaphore_signal-fixed_priority-heap", 1, 1],
    ["priority_inversion-fixed_priority-heap", 6, 2], ["rm_edf_compare-fixed_priority-heap", 7, 2],
    ["rm_edf_compare-edf-heap", 7, 2], ["overload-fixed_priority-heap", 10, 2],
    ["priority_inversion-fixed_priority-sorted_list-inheritance", 3, 1]]) {
    await importFiles([runPath(name)]); await goTo(t); await pickJob(job);
    await noOverflow(); await shot(name + "-t" + t);
  }

  // Playback controls stay visible on a common presentation laptop viewport.
  await send("Emulation.setDeviceMetricsOverride", { width: 1366, height: 768, deviceScaleFactor: 1, mobile: false });
  await pause(150);
  assert(await js("document.getElementById('play').getBoundingClientRect().bottom <= innerHeight"), "play button should be visible on a laptop");
  await noOverflow(); await shot("viewer-laptop");

  // Narrow screens and reduced-motion preferences remain usable.
  await send("Emulation.setDeviceMetricsOverride", { width: 390, height: 844, deviceScaleFactor: 1, mobile: false });
  await send("Emulation.setEmulatedMedia", { features: [{ name: "prefers-reduced-motion", value: "reduce" }] });
  await pause(150); await noOverflow(); await shot("viewer-mobile");
  await goTo(0);
  await js("document.getElementById('speed').value = '0.5'");
  const reducedX = await cursorX();
  await click("play"); await pause(200);
  assert.strictEqual(await cursorX(), reducedX, "reduced motion keeps cursor on whole ticks");
  await click("play"); await click("home");
  await noOverflow(); await shot("welcome-mobile");
  assert.strictEqual(await js("getComputedStyle(document.querySelector('.orbit')).animationName"), "none");


  assert.deepStrictEqual(problems, [], "page errors");
  assert(requests.length > 0 && requests.every((u) => u.startsWith("file:") || u.startsWith("data:image/svg+xml,")), `non-local request: ${requests}`);
  console.log(`dashboard browser checks passed (offline, ${path.basename(browser)})`);
}

main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => {
  child.once("exit", () => fs.rmSync(temp, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 }));
  child.kill();
});
