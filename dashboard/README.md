# B6 Scheduler Dashboard

Open `dashboard/index.html` directly in a browser. No server, internet connection or installation is needed.
On the welcome screen, choose **Choose / import runs** and select one or more JSON files from `runs/`.
The menu at the top switches between imported recordings; **Import runs** adds more files.

The dashboard plays back exported C++ results. It never runs the scheduler, changes settings in a recording,
or recomputes engine metrics. To change the policy, timer backend or inheritance setting, generate a new
run with the CLI and import it.

## Presenting a run

- The heading names the scenario and explains its purpose in one sentence. The three badges show its
  policy, timer backend and inheritance setting. Hover over the heading to see the original run ID.
- The Gantt chart shows the entire recorded CPU timeline, with one lane per job. A bar is an execution
  interval; idle time has its own lane. Release lines, deadline lines and missed-deadline crosses refer
  to the full recording. A lime outline marks an inherited priority; `P` is the effective priority.
- **Play / Pause**, **Prev**, **Next**, **Reset** and the slider select recorded time. The speed menu changes
  playback speed only. The cursor moves smoothly between ticks, while side panels always use a whole-tick
  snapshot. The tick label identifies that snapshot. Reduced-motion mode moves the cursor in whole ticks.
- The left panel shows the running job, ready order, blocked or suspended jobs, and resource owners,
  permits and waiters. `J` identifies a job, so repeated releases of the same task can be distinguished.
- Select a chart lane, a queue entry or a job from **Inside the job** to inspect its state. The priority
  card highlights donation and restoration. For EDF it shows the absolute deadline instead, because
  EDF uses deadlines to choose between ready jobs.
- **Focus chart** hides the side panels and widens the timeline. Long recordings scroll horizontally;
  lane names stay pinned. Many-job recordings also scroll vertically inside the chart.
- Switching between recordings with the same scenario ID preserves the selected tick and job where
  possible, making OFF/ON and RM/EDF comparisons easier.
- The B6 button returns to the welcome screen. **Return to imported run** restores the view. Imported
  files stay in memory until the page is reloaded. Playback pauses when the page is hidden.

The old job-state, task, final-outcome, event-log and metrics tables are intentionally absent from this
presentation view. A short sentence beneath the controls explains one significant event at the selected
tick. Detailed evidence and metrics remain in the exported JSON.

## Accuracy and evidence

The snapshot at tick `t` records settled decisions at `t`, before the interval `[t, t+1)` runs. A missing
snapshot is reported explicitly: the UI never substitutes a neighboring snapshot or a job's final state.
The full chart may show a future deadline miss, while the inspector correctly says it has not happened
yet at the selected tick.

Real inheritance examples are available in `runs/priority_inversion-fixed_priority-heap-inheritance.json`
and its `sorted_list` counterpart. The files in `dashboard/fixtures/` are hand-written test samples and
always display a **SAMPLE DATA** banner. Invalid or truncated recordings and exported warnings remain
visible above the chart; simplifying the layout does not hide them.

`app.js` retains the existing import validator and pure recorded-state helpers. `presentation.js` owns
the UI, navigation, chart rendering and animation. Both are ordinary scripts so `file://` opening works.
The local Oxanium and Space Mono font assets include their licenses; no external assets are requested.

## Checks

Run from the repository root:

```sh
node dashboard/validate.test.js
node dashboard/view.test.js
node dashboard/browser.check.js
```

The browser checks require Node 22+ and Chrome or Edge. Set `BROWSER` to its executable if it is not
detected. They open the page from disk with networking disabled, exercise real imports and playback,
check OFF/ON history, warnings, unsafe text, long and empty runs, keyboard lane selection, narrow screens,
reduced motion and laptop playback controls. To save screenshots outside the repository:

```sh
node dashboard/browser.check.js ../output/dashboard-review
```

## Unchanged import contract

Schema version 1 is required. The existing validator checks object and array shapes, allowed policies,
backends, states, block reasons and event types; unique IDs and valid references; ordered non-overlapping
intervals; ordered events and snapshot ticks; a single consistent running job; resource and semaphore
bounds; and the 2,000-tick horizon limit. Files larger than 32 MiB are refused. EDF/RM recordings combined
with resource synchronization or inheritance are refused under the current engine's supported modes.

A structurally valid recording with `validation.status: "invalid"` can still be inspected with its
warning banner. The C++ engine, exporter, run schema and committed run files are unchanged by this UI.
