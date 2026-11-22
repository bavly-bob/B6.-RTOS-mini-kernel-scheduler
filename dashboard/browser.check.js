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
  const inspector = async (label) => js(`[...document.querySelectorAll("#inspector > div")].find((d) => d.children[0].textContent === ${JSON.stringify(label)})?.children[1].textContent ?? null`);
  const expect = async (label, value) => assert.strictEqual(await inspector(label), value, `inspector ${label}`);

  // Wrong files: each is refused with its name and reason, and nothing is shown.
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
    assert(errors.includes(part), `error message lacks "${part}":\n${errors}`);
  assert.strictEqual(await js(`document.getElementById("view").hidden`), true);
  await shot("rejected-files");

  // A real inversion run next to a rejected file: the good one is still imported.
  const inversion = runPath("priority_inversion-fixed_priority-heap");
  await importFiles([inversion, tempFile("notes.json", "{")]);
  assert((await text("errors")).includes("notes.json"));
  await importFiles([inversion]);
  assert.strictEqual(await text("errors"), "");
  assert.strictEqual(await js(`document.getElementById("fixtureBanner").hidden`), true);
  assert.match(await text("runMeta"), /engine_run/);

  // Slider: historical state, not the final state (H ends COMPLETED with a miss).
  await pickJob(2);
  await expect("State", "not released yet (releases at 1)");
  await goTo(4);
  await expect("State", "BLOCKED");
  await expect("Waiting for", "mutex 0, owned by L · job 1");
  await expect("Deadline missed", "no, not by this tick");
  assert.match(await text("resources"), /mutex 0\s+owner L · job 1\s+H · job 2/);
  await goTo(6);
  await expect("Deadline missed", "yes");
  assert.strictEqual(await js(`(() => { const list = document.getElementById("events").getBoundingClientRect();
    return [...document.querySelectorAll("#events .now")].every((b) => b.getBoundingClientRect().bottom <= list.bottom + 1); })()`), true, "this tick's events are visible");

  // Buttons.
  await click("next");
  assert.strictEqual(await text("tickLabel"), "tick 7 of 10");
  await click("prev");
  await click("prev");
  assert.strictEqual(await text("tickLabel"), "tick 5 of 10");
  await click("reset");
  assert.strictEqual(await text("tickLabel"), "tick 0 of 10");
  assert.strictEqual(await js(`document.getElementById("prev").disabled`), true);
  await click("play");
  await until(`document.getElementById("slider").value >= 2`, "playback to advance");
  await click("play");
  const stopped = await text("tickLabel");
  await pause(900);
  assert.strictEqual(await text("tickLabel"), stopped, "pause must stop playback");
  await goTo(9);
  await click("play");
  await until(`document.getElementById("slider").value === "10" && document.getElementById("play").textContent === "Play"`, "playback to stop at the horizon");
  assert.strictEqual(await js(`document.getElementById("next").disabled`), true);

  // Selecting an event jumps to its tick and job.
  await js(`[...document.querySelectorAll("#events .event")].find((b) => b.innerText.includes("UNLOCK")).click()`);
  assert.match(await text("tickLabel"), /^tick 8 of 10/);

  // Idle time, suspension and an unfinished job.
  await importFiles([runPath("suspend_resume-fixed_priority-heap"), runPath("rm_edf_compare-edf-heap")]);
  assert.match(await text("runMeta"), /edf/);
  await goTo(8);
  await pickJob(4);
  await expect("State", "READY");
  assert.match(await text("jobs"), /not completed\s+not completed/);
  const count = await js(`document.getElementById("runSelect").options.length`);
  await pickRun(count - 2);
  await goTo(5);
  await pickJob(1);
  await expect("State", "SUSPENDED");
  await expect("Waiting for", "nothing");
  await goTo(6);
  assert.match(await text("queues"), /Running\s+idle/);

  // Inversion OFF and ON in the same viewer: switching keeps the tick and job.
  await importFiles([inversion]);
  await goTo(3);
  await pickJob(1);
  await expect("Effective priority", "1");
  assert.match(await text("keyEvents"), /t=6\s+DEADLINE_MISS\s+H · job 2/);
  assert.strictEqual(await js("document.querySelectorAll('#gantt .miss').length"), 1);
  assert.strictEqual(await js("document.querySelectorAll('#gantt rect.boosted').length"), 0);
  assert.strictEqual(await js(`document.getElementById("noMetrics").hidden`), true);
  assert.match(await text("overall"), /90% \(9 of 10 ticks\)/);
  assert.match(await text("overall"), /not applicable: one-shot tasks/);
  // A run without metrics says so instead of showing empty tables.
  await importFiles([broken("no_metrics.json", (r) => {
    r.metrics = { overall: {}, perTask: [] };
    r.validation.warnings = ["metrics were not computed for this run"];
  })]);
  assert.strictEqual(await js(`document.getElementById("noMetrics").hidden`), false);
  assert.match(await text("runNotes"), /metrics were not computed/);
  await importFiles([inversion]);
  await importFiles([path.join(__dirname, "fixtures", "inversion_on.design.json")]);
  assert.strictEqual(await text("tickLabel"), "tick 3 of 10");
  await expect("Job", "L · job 1");
  await expect("Effective priority", "3 (boosted from 1)");
  assert.strictEqual(await js("document.querySelectorAll('#gantt rect.boosted').length"), 1);
  assert.strictEqual(await js("document.querySelectorAll('#gantt .miss').length"), 0);
  assert.strictEqual(await js(`document.getElementById("runNotes").hidden`), true);
  assert.match(await text("overall"), /90% \(9 of 10 ticks\)/);
  assert.match(await text("fixtureBanner"), /SAMPLE DATA/);
  await js(`[...document.querySelectorAll("#keyEvents .event")][1].click()`);
  assert.strictEqual(await text("tickLabel"), "tick 4 of 10");

  // Key events fall back to a sentence, and an invalid run is marked as such.
  await importFiles([runPath("rm_edf_compare-edf-heap")]);
  assert.match(await text("keyEvents"), /No priority change, deadline miss or error/);
  await importFiles([broken("invalid.json", (r) => { r.validation.status = "invalid"; r.validation.error = "stopped early"; })]);
  assert.match(await text("runNotes"), /INVALID RUN, do not use as evidence: stopped early/);
  assert.strictEqual(await js(`document.getElementById("runNotes").className`), "notes invalid");

  // Hostile names are shown as text and never run.
  const hostile = broken("hostile.json", (r) => { r.run.label = r.tasks[0].name = '<img src=x onerror="window.__hacked=1"><script>window.__hacked=1</script>'; });
  await importFiles([hostile]);
  assert((await text("tasks")).includes("<img src=x"));
  assert.strictEqual(await js("document.querySelectorAll('main img, main script').length"), 0);
  assert.strictEqual(await js("window.__hacked ?? null"), null);

  // The hand-written sample is labelled, and a missing snapshot is stated.
  await importFiles([path.join(__dirname, "fixtures", "rr_preemption.design.json")]);
  assert.match(await text("fixtureBanner"), /SAMPLE DATA/);
  assert.strictEqual(await js(`document.getElementById("noSnapshot").hidden`), false);

  // A 140-tick run fits its card; a 2,000-tick one scrolls with its lane labels kept in view.
  const chart = "document.getElementById('gantt')";
  await importFiles([runPath("sleep_order-fixed_priority-heap")]);
  assert.strictEqual(await js(`${chart}.parentElement.scrollWidth <= ${chart}.parentElement.clientWidth`), true);
  assert.match(await text("resources"), /none/);
  const long = readRun(runPath("sleep_order-fixed_priority-heap"));
  long.config.horizonTicks = 2000;
  await importFiles([tempFile("long.json", JSON.stringify(long))]);
  await goTo(1900);
  await until(`${chart}.parentElement.scrollLeft > 0`, "the chart to follow the cursor");
  await until(`Math.abs(${chart}.querySelector("g.labels text").getBoundingClientRect().left - ${chart}.parentElement.getBoundingClientRect().left) < 20`, "lane labels to stay in view");

  // Imported settings are immutable: the first inversion run still shows its file's values.
  await pickRun(0);
  for (const pair of [["Policy", bad.config.policy], ["Timer backend", bad.config.timerBackend], ["Inheritance", "off"],
    ["Quantum (ticks)", bad.config.quantumTicks], ["Horizon (ticks)", bad.config.horizonTicks]])
    assert((await text("runMeta")).replace(/\s+/g, " ").includes(pair.join(" ")), `run settings lack ${pair}`);

  // Screenshots of real engine runs only, named <runId>-t<tick>.
  if (shots) {
    for (const [name, t, job] of [["rr_preemption-fixed_priority-heap", 2, 3], ["sleep_order-fixed_priority-heap", 115, 1],
      ["suspend_resume-fixed_priority-heap", 5, 1], ["semaphore_signal-fixed_priority-heap", 1, 1],
      ["priority_inversion-fixed_priority-heap", 6, 2], ["rm_edf_compare-fixed_priority-heap", 7, 2],
      ["rm_edf_compare-edf-heap", 7, 2], ["overload-fixed_priority-heap", 10, 2]]) {
      await importFiles([runPath(name)]);
      await goTo(t);
      await pickJob(job);
      await shot(`${readRun(runPath(name)).run.runId}-t${t}`);
    }
  }

  assert.deepStrictEqual(problems, [], "page errors");
  assert(requests.length > 0 && requests.every((u) => u.startsWith("file:")), `non-local request: ${requests}`);
  console.log(`dashboard browser checks passed (offline, ${path.basename(browser)})`);
}

main().catch((error) => { console.error(error); process.exitCode = 1; }).finally(() => {
  child.once("exit", () => fs.rmSync(temp, { recursive: true, force: true, maxRetries: 10, retryDelay: 200 }));
  child.kill();
});
