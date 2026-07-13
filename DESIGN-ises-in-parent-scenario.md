# Design study — promoting Sequence ISes into the parent scenario

**Status:** exploratory design brief, written overnight for review. Grounded in the
score **and** ossia runtime source (see Appendix A for file:line evidence). Author:
Claude (Opus 4.8), session 019ya5qsdZbjVtA697QUhJFz.

**Reframing worth stating up front:** the Sequence *already* builds a **real nested
`ossia::scenario`** with real internal `time_sync`s between sections
(`SequenceExecution.cpp:44-100, 210-253`). So the ISes are already genuine timesyncs
with full trigger/AND semantics — but they live in a **child** scenario, invisible to
the **parent**. "Promoting the ISes" therefore means **flattening that child structure
into the parent scenario** so parent elements can sync/trigger against it. The execution
machinery this depends on is already exercised today; the open work is model-identity
and UX, not runtime feasibility.

---

## 0. The ask (restated)

Today the Sequence process **encapsulates** its own timing structure: sections are
internal intervals, and the "IS" boundaries between them are internal
timesyncs/events/states *hidden inside one parent interval*. Because they're
internal, the ISes **cannot**:

- be **synchronized** with other states/timesyncs in the parent scenario, and
- **receive trigger points** (conditions that hold/launch them, GUI/OSC triggers).

Goal: make each IS a **first-class element of the parent scenario** so it can be
triggered and synced like any other timesync.

The obstacle the user identified: the sequence lives in an interval **alongside other
parallel processes** (e.g. audio). Naively "promoting" the ISes by splitting that
interval at each IS would also **cut the parallel processes** — and continuous
processes (audio) can't survive being cut.

The user's proposed way out — the thing to evaluate here:

> As soon as a sequence is created (from a state, or from an existing interval), have
> **both in parallel**: the other processes stay in *their* interval (extended on every
> blue-`+`), and for the sequence, **proper states are inserted between the start and
> end states of that parallel interval**.

---

## 1. TL;DR of the findings

1. **The structure is representable in score's model today.** score expresses
   parallelism at the *timesync* level: several intervals can share a start timesync
   and an end timesync while each keeps its own state. So a "ladder/diamond" —
   one continuous interval **A** in parallel with a chain of section intervals
   **B₁→IS₁→B₂→…→Bₙ**, all spanning the same `TS_start … TS_end` — is a legal
   scenario. (StateModel.hpp:147-148, TimeSyncModel.hpp:125, EventModel.hpp:100.)

2. **It delivers the goal directly and for free.** Each IS becomes a real
   `TimeSyncModel`, which already carries `expression`/`active`/`autotrigger`
   (TimeSyncModel.hpp:65-72). No new "sync/trigger" mechanism is needed — that's the
   whole point.

3. **Latent upsides — but read the correction.**
   - **Boundary value sync is *already* native** (the sequence is a `ScenarioInterface`;
     adjacent sections share one IS `StateModel`, so `ProcessPolicy` already keeps their
     values equal). Promotion does **not** newly gain this — so it does **not**
     automatically eliminate the *value* side of the twin-sync. What this session's bug
     actually was is a **presentation** defect in the *custom* `SequencePresenter`
     (point-object churn from its per-drag rack mirroring). Rendering the sections as
     **ordinary parent-scenario intervals** (normal curve presenters, no bespoke
     mirroring) is *likely* to avoid that whole class of custom-UI churn bugs — a real
     but not guaranteed benefit.
   - **Native trigger/sync on ISes** — this is the actual, certain win (see item 2).
   - **Flatter execution.** Today the sequence executes as a *nested* `ossia::scenario`
     inside a `sequence_node` (SequenceExecution.cpp:44-100). Promotion lets the sections
     execute directly in the parent scenario — flatter, one fewer scenario level — though
     the nested approach already works, so this is simplification, not unblocking.

4. **The genuinely hard part is not feasibility — it's *identity & editing*.** Once the
   ISes/sections are native parent elements, "what is a Sequence?" becomes a
   grouping/pattern over native elements rather than one owned `ProcessModel`. Keeping
   the nice sequence UX (extend, drag-IS, ripple, insert-IS, per-parameter lanes,
   ports) and its invariants — while the elements are also editable by ordinary
   scenario tools — is the crux that has stalled this for years.

5. **There is one real, unavoidable semantic tension** (the reason this is thorny):
   a *continuous* parallel interval **A** and a *triggerable* chain **B** cannot both
   be true at a waiting trigger. If IS₂ holds, B stretches but A (one continuous
   interval) does not — so A and B **desync at triggers**. This is acceptable (arguably
   desirable), but it must be a *chosen, documented* semantic, not an accident.

---

## 2. Current architecture (for contrast)

`SequenceModel` **derives from both `Process::ProcessModel` and
`Scenario::ScenarioInterface`** (SequenceModel.hpp:42-44) and owns the same four
`score::EntityMap`s as a real scenario — `intervals / events / timeSyncs / states`
(SequenceModel.hpp:53-56) — plus boundary ids `m_start/​endTimeSyncId`,
`m_start/​endEventId`. **So it already *is* a mini-scenario**, and generic `Scenario::`
algorithms (Accessors, `ProcessPolicy`, SetNext/PreviousInterval) already run on it.

- A **section = one interval**; an **IS = exactly one timeSync + one event + one state**
  (`createIS`, SequenceModel.cpp:396-417). Consecutive sections are joined by
  `SetNextInterval`/`SetPreviousInterval` **sharing that one IS state** — which means the
  **boundary-value twin-sync already runs today via the normal `ProcessPolicy` shared-
  state mechanism.** (The bug we fixed this session was therefore in the *custom
  presenter's* disc rendering, **not** the value sync — see the corrected Insight below.)
- Per-parameter lanes = one Automation (or Gradient, for colour) per address per section;
  vec params expand to one lane per component. JM's per-parameter **ports** = one
  `ValueOutlet` per namespace parameter on the process, with per-section instance ports
  hidden (`rebuildParamPorts`, SequenceModel.cpp:588-646).
- **Extend (blue `+`) already moves the parent interval.** `ExtendSequence` =
  `AppendSequenceSection` **+ `Scenario::MoveEventMeta`** on the *host interval's end
  event* (ExtendSequence.cpp:39-71). So **the host interval already behaves exactly like
  the proposal's "lane A"**: it stretches on every `+` and it already holds any parallel
  processes the user dropped alongside the sequence.
- It renders as nested `TemporalIntervalPresenter`s in a custom `SequencePresenter`
  (SequencePresenter.cpp:181-263) and executes as a **real nested `ossia::scenario`**
  (§1 reframing). Serialized as one process (version 2, four entity maps).

**So the delta the proposal really asks for** is not "add a scenario structure" (the
sequence already is one) but: **relocate that structure's entity maps from the process
into the parent scenario's maps**, keeping the host interval as lane A. Because
everything is currently *inside* the process, the ISes are unreachable from the parent —
hence the limitation, and hence this study.

*(Caveat from exploration: in the tree read, `Scenario/Sequence/` appeared untracked and
its factory wiring wasn't found in `CMakeLists.txt`/`score_plugin_scenario.cpp` — yet the
plugin builds and runs in our test instance, so registration is happening (globbed
sources / elsewhere). Not load-bearing for this design; flagged for tidiness only.)*

---

## 3. The target structure (native ladder)

Decompose the proposal into two independent pieces — this framing matters:

### 3a. Core: a sequence **is** a constrained native chain
A sequence's own content (the per-parameter automations that ramp across sections) maps
to a **linear chain of intervals in the parent scenario**:

```
TS_start ── B1 ── (IS1) ── B2 ── (IS2) ── … ── Bn ── TS_end
              \_ automations _/    (each Bk holds one automation per parameter)
```

- Each **section Bk** is a normal interval holding one automation (or gradient) per
  parameter, in slots — exactly one per address.
- Each **IS** is a single **shared `StateModel`** (1 previous-interval = Bk, 1
  next-interval = Bk+1) sitting on its own **`EventModel`/`TimeSyncModel`**.
  - The shared state **natively syncs the boundary values** of Bk's and Bk+1's
    automations for each address (ProcessPolicy) → free twin-sync.
  - The IS `TimeSyncModel` can carry a **trigger/expression** and be **merged/synced**
    with any other timesync → the goal, for free.

**If a sequence has only automations, this is all you need. No parallel lane, no
continuity problem.** The ISes are already first-class. This is the simplest and
strongest form.

### 3b. Optional: a parallel **continuous lane A** for spanning processes
When the sequence must run *under* a continuous process (an audio bed) that should
**not** be cut at ISes, add a second interval **A** in parallel between the *same*
`TS_start` and `TS_end`:

```
TS_start ──────────── A (audio bed, one continuous interval) ──────────── TS_end
   │                                                                          │
   └──── B1 ─ IS1 ─ B2 ─ IS2 ─ … ─ Bn ────────────────────────────────────────┘
```

Model-wise (confirmed representable; parallelism is done at the timesync level because a
`StateModel` is strictly 1-in/1-out, StateModel.hpp:147-148):
- `TS_start`'s event carries **two states** — one feeds A, one feeds B1.
- `TS_end`'s event carries two states (one from A, one from Bn) and fires on
  **AND-convergence** — confirmed: `is_timesync_ready` returns true only when *every*
  active event is pending, and an event pends only once *all* its incoming intervals
  passed their min duration (scenario_sync_musical_execution.cpp:71-155).
- The intermediate IS timesyncs belong to **lane B only**; A doesn't pass through them,
  so A is never cut → **audio continuity preserved** (audio hard-stops at any boundary:
  time_interval.cpp:316-342 + discontinuity flags, continuity.hpp:9-45).

This is exactly the user's picture: "other processes stay in their interval; proper
states inserted between the start and end states of that parallel interval."

**Key clarification:** A is *optional and only for whole-sequence-spanning continuous
processes.* Per-section processes (including per-section audio) can still live inside a
Bk interval — they'll be cut at that section's IS boundaries, like any scenario audio.
So the model is actually *more* expressive than today (both "spanning bed" and
"per-section" processes are possible), at the cost of the user having to place a process
in A vs a Bk.

---

## 4. Why the model already allows it (grounded)

- **Parallelism lives on timesyncs, not states.** `StateModel` is strictly 1-in/1-out
  (`m_previousInterval`, `m_nextInterval` — StateModel.hpp:147-148). Branching/merging
  is done by a `TimeSyncModel` owning **many events** (TimeSyncModel.hpp:125) and each
  `EventModel` owning **many states** (EventModel.hpp:100). So "A and B1 both start at
  TS_start" = TS_start's event has two states, one feeding A, one feeding B1. Legal.
- **Consecutive sections share one state at an IS.** A single `StateModel` with
  `previousInterval = Bk`, `nextInterval = Bk+1` is a normal linear join; its event sits
  on the IS timesync. This is the ordinary `interval → state → interval` shape, so
  everything that already works for a plain scenario chain (boundary value sync, trigger
  on the timesync, undo) works here.
- **Triggers/sync are native to `TimeSyncModel`** (expression/active/autotrigger,
  TimeSyncModel.hpp:65-72) — the capability we want to expose *is already there* on the
  element we'd promote to.

---

## 5. The unavoidable semantic tension (read carefully)

A continuous lane **A** and a triggerable chain **B** can't both hold at a trigger:

- With **no trigger** on the ISes (pass-through/autotrigger), B's total runtime = sum of
  section durations = A's duration. A and B stay sample-aligned; the split is invisible.
  **This is the common case and it's transparent.**
- With a **waiting trigger** on IS_k: B pauses at IS_k until released; A, being one
  continuous interval, **does not pause**. This is **runtime-confirmed**, not just
  inferred — the engine ticks *all* running intervals every frame
  (scenario_execution.cpp:433-436), a trigger sync returns `NOT_READY` while its
  expression is false (scenario_sync_execution.cpp:82-173), and an interval that reaches
  its own `max_duration` is **frozen at its last value/sample** (not ended, not resumed)
  via the overtick clamp (scenario_execution.cpp:130-228). So:
  - A advances to its max and **holds its last frame** (audio: its processes have
    stopped producing → silence) while B is still held.
  - `TS_end` waits for both → fires only when B finishes; if B ran long, A has been
    frozen/silent since its own max.
  - A cannot "resume mid-file" — it's a single interval; once at max it only holds.

**Consequence:** you can have *continuous audio* **or** *audio that respects mid-sequence
triggers*, **not both** for the same process. The user's proposal chooses continuity for
lane A (audio bed) and accepts that triggers desync the bed from the sequence. That is a
legitimate, probably-desirable choice (you added the trigger precisely to hold the
*sequence*, not the bed) — but it is *the* defining semantic and should be explicit.
If a user instead wants the bed to hold too, they must put that audio **inside the Bk
sections** (segmented, cut at ISes) rather than in A. There is no third option; this is
intrinsic to "continuous == one interval."

*(This is very likely the crux where prior JM discussions stalled: people kept trying to
make A both continuous and trigger-aware, which is a contradiction. Naming the tradeoff
dissolves the stall.)*

---

## 6. The real hard problem: identity, invariants & editing

Feasibility is fine. The cost is that **"Sequence" stops being one owned `ProcessModel`
and becomes a recognizable pattern + editing macros over native scenario elements.**
This is a spectrum; the central design decision is where to land:

| Axis | Full encapsulation (today) | Full native (proposal) |
|---|---|---|
| ISes first-class (sync/trigger)? | ❌ no (hidden in child scenario) | ✅ yes |
| Boundary *value* sync | native (shared IS state) — same either way | native (shared IS state) |
| Boundary *disc rendering* | custom presenter w/ churn (this session's bug) | normal curve presenter (likely no churn) |
| Sequence identity | one process, trivially clean | a grouping/pattern — must be maintained |
| Invariant safety | strong (nothing external can touch internals) | weak — raw scenario tools can break the ladder |
| UX (extend/drag/ports/lanes) | custom, self-contained | must be re-expressed as ops on native elements |
| Nesting | drop into any interval | lives directly in *a* scenario (nestable) |

**Identity options** (how the app knows "these native elements are one sequence"):

- **(a) Tagging / grouping.** Stamp the member intervals/states/timesyncs with a
  `sequenceGroupId` (metadata), plus a small `SequenceGroup` object holding cross-cutting
  settings (parameter namespace, per-parameter port config, lane-A reference). The
  Sequence UI/tools operate on the group. **Most "native," cleanest execution.** Risk:
  ordinary scenario edits (delete a section, drop an interval into the ladder, move an IS
  state off its timesync) can violate the ladder invariant. Mitigation: (i) the sequence
  provides the structural ops and, (ii) either *guard* member elements against raw edits,
  or (iii) *re-derive/validate* the group and degrade gracefully ("this is no longer a
  well-formed sequence") instead of crashing.
- **(b) Owned-but-registered.** A `SequenceModel` still owns/creates the elements but
  registers them into the parent scenario for execution/sync. Awkward dual ownership
  (who serializes? who undoes?); tends to fight the model. Not recommended unless (a)'s
  invariant problem proves intractable.

**Editing operations become native mutations** (and inherit native pitfalls):
- *insert IS* = split a Bk at a date → insert state+event+timesync in the chain (and
  crucially **do not** split A).
- *move IS* = move the IS timesync's date (a `MoveEventMeta`-style op) — **beware the
  MoveEvent snapshot-undo trap already documented for this project**: `MoveEvent::undo`
  reloads processes from a snapshot taken at command construction, so composite
  sequence+move commands must order undo/redo carefully. This trap gets *more* central
  in the native world.
- *ripple* = move an IS timesync and everything after it.
- *extend (blue +)* = append `Bn+1` + an IS, **and** stretch lane A so both still end at
  `TS_end`.
- *delete section*, *merge ISes*, etc. — all must preserve "B is a simple chain between
  TS_start and TS_end, parallel to A."

**Other cross-cutting costs:**
- **Serialization / copy-paste:** a sequence is now a *subgraph* of the scenario. Copy =
  copy the subgraph + reconstruct the group identity; paste into another scenario must
  remap ids. (Contrast: today it's one process blob.)
- **Undo:** compound structural commands over more native elements; higher chance of the
  snapshot/rollback subtleties we've already hit.
- **Ports / dataflow:** today the process exposes one value outlet per parameter. In the
  native model each Bk automation has its own outlet; the "one cable per parameter for
  the whole sequence" abstraction must be rebuilt (e.g. outlets on lane A, or a
  thin wrapper, or cables auto-managed per section). This is JM's per-parameter-ports
  work and needs redesign in the native world.
- **UI grouping / full view:** even though native, users still want the sequence to *look*
  like one cohesive widget. Need a presenter that recognizes the group and overlays the
  sequence UI on the parent scenario view (rather than a nested process view). Feasible
  but new.
- **Backward compatibility / migration:** existing saved encapsulated sequences must load
  and ideally convert to the native form (or both forms coexist for a while).

---

## 6b. Alternatives ruled out (and precisely why)

- **Keep encapsulation, "expose/proxy" ISes to the parent.** In ossia, syncing two
  timing points = **merging** their events/timesyncs (MergeTimeSyncs), which requires
  them to be in the **same** scenario. A parent timesync cannot be merged with a timesync
  that lives inside a child `ossia::scenario`. So exposing an internal IS would require a
  brand-new, non-native "mirror/proxy timesync that forwards trigger + sync across the
  encapsulation boundary" — brittle, semantically fuzzy, and it re-implements badly what
  flattening gives natively. This is exactly the "shared timesyncs with outside structure
  is impossible under encapsulation" dead-end from prior notes.
- **Make the Sequence a real *nested* `Scenario` process (drop the custom process).** Its
  ISes would be real timesyncs — but still inside a **child** scenario, so still invisible
  to the parent. Nesting does not achieve outside-sync. (It's basically what the sequence
  already does internally.)
- **Naive promotion: split the host interval at each IS.** This is the original obstacle —
  it cuts the continuous audio bed (audio hard-stops at every boundary, Appendix A). The
  parallel **lane A** is precisely the fix: don't split the bed's interval; run the
  segmented sequence *beside* it.

Net: **flattening the sequence's structure into the parent, with a parallel continuous
lane for spanning processes, is the only option that makes ISes first-class without
breaking audio continuity.** Everything else is strictly weaker.

## 7. Recommended path (cheap-decisive-first)

**Step 0 — validate the *execution semantics* by hand, with zero new code.**
Author, by hand in a normal scenario, the target structure for a 2-section sequence:
`TS_start → {A: one interval with an audio process} ∥ {B1 → IS(state+timesync) → B2}`,
with one automation per parameter in B1/B2 sharing the IS state, and TS_end AND-merging A
and B2. Then test:
1. audio in A plays continuously across the IS; ✔/✘
2. the two automations' boundary values stay equal at the shared IS state (native sync); ✔/✘
3. adding a **trigger** on the IS timesync holds B (and desyncs A exactly as §5 predicts); ✔/✘
4. syncing the IS timesync to an outside timesync works; ✔/✘

This proves (or kills) the whole idea for the price of a few minutes of clicking — **no
model changes**. Everything downstream is UX/identity engineering, which is only worth it
if Step 0 behaves.

**Step 1 — identity & construction.** Prototype option (a): a `sequenceGroupId` tag + a
minimal group object; a `CreateSequence` that builds the ladder (and lane A when
converting an existing interval); `Extend` that appends a section + stretches A.

**Step 2 — editing ops** (insert/move/ripple/delete IS) as guarded native commands,
respecting the MoveEvent trap.

**Step 3 — UI overlay** (recognize the group, render the sequence widget over native
elements) and **ports** redesign.

**Step 4 — migration** of encapsulated sequences.

Steps 1–4 are large; do them only after Step 0, and after the open questions below are
answered.

---

## 7b. BREAKTHROUGH (2026-07-13 overnight #2): the diamond "keep-playing" semantics
### — user's Step-0 test, root cause, and a much smaller canon feature

The user hand-authored the diamond (`~/Desktop/diamond.score`: lane **A** rigid,
parallel to **B1 →(trigger @Sync2)→ B2**, shared end **Sync3**, no trigger on Sync3)
and observed: when A reaches its end while B waits, **A stays green but its content
stops playing**. Desired semantic: *A's end must not act until B has finished; A keeps
playing until the final sync actually fires.* Investigation results (all first-person
verified in source):

### What happens today, precisely
- A is **rigid** (min = max = nominal). `run_interval` clamps ticking at max
  (scenario_execution.cpp:148-190): A's date freezes, but A is never removed from
  `m_runningIntervals` → **green + frozen + silent**. Matches the observation exactly.
- Sync3 (no trigger) fires only when **all** incoming lanes passed their min
  (AND-convergence) — it does wait for B; the problem is only that frozen-A's *content*
  stops.

### The engine ALREADY implements the desired semantic — for flexible intervals
- With `max = ∞`, `run_interval` ticks the interval **unconditionally**
  (scenario_execution.cpp:211-214) until its end sync fires; `make_happen` then stops it
  cleanly.
- Tokens are passed to processes **unclamped** past the nominal duration
  (time_interval.cpp:388-427, token `{from, to, m_nominal, …}`), and:
  - a **sound file keeps playing its real content** past nominal (sound_sampler.hpp
    reads unclamped positions; silence only when the file data itself ends; looping
    sounds wrap — sound_utils.hpp);
  - an **automation holds its last value** (curve.hpp `value_at`, 210-236 — walks past
    the last point and returns it);
  - a **looping process keeps looping** (time_process.hpp `looping_process`).
- Re-clamp landmine checked: `m_itv_end_map` (scenario_execution.cpp:138-142) only
  populates under **musical quantization** of the sync (`quantify_time_sync`,
  scenario_sync_execution.cpp:11-36) — a plain diamond never re-clamps the flexible
  lane; a *quantized* end sync aligns the release to the grid (correct interplay).
- If the sync's trigger **expression is already true**, the gating block is skipped
  entirely and the sync fires the moment AND-convergence is ready
  (scenario_sync_execution.cpp:97-111).

**⇒ ZERO engine changes needed.** "A keeps playing until B is done" = A flexible
(min = nominal, max = ∞) + plain shared end sync.

### The real root cause is an AUTHORING gap (and it explains the historical workaround)
- `SetRigidity` (the command that makes an interval flexible) is invoked by exactly
  three things: **AddTrigger** (de-rigidifies `intervalsBeforeTimeSync`,
  AddTrigger.hpp:40-46), **RemoveTrigger**, **RemoveSelection**. Nothing else.
- The interval inspector **hides all min/max/∞ controls while the interval is rigid**
  (DurationSectionWidget.cpp:225-234) and offers no rigidity toggle.
- **⇒ The ONLY user-accessible door into flexibility is adding a trigger.** This is
  precisely why the user's long-standing workaround exists (trigger at the end of the
  early lane + OSC value-holder fired by the other lane's final state to release it):
  the trigger was never wanted — it was the only way in; the OSC hack then re-created,
  by hand, the release-on-convergence the engine's AND semantics would do natively.
- Meanwhile flexible-without-trigger is **fully legal in the model**: every document's
  root interval is exactly that (`rigid=false, maxInfinite=true`,
  ScenarioDocumentModel.cpp:44-47); the flags serialize; execution maps `isMaxInfinite`
  → `ossia::Infinite` (IntervalExecution.cpp:397); and the **dashed rendering is driven
  purely by the duration flags** (IntervalView rigid/infinite/min/maxWidth), so a
  flag-flexible lane renders dashed automatically — the user's instinct that the lane
  "should become dashed, its duration is not fixed anymore in a diamond" is exactly
  what the existing visual language already means.

### Immediate zero-code recipes (testable NOW)
1. **`~/Desktop/diamond-flexible.score`** (generated): identical to the user's file
   with **A set flexible** (`Rigidity=false, MinDuration=nominal, MaxInf=true`).
   Expected: A plays through Sync2's wait — file tail / loop / held automation — until
   B2 completes, then Sync3 fires and all lanes stop together. A shows dashed past its
   nominal length.
2. **Pure-UI, today** (3 steps — the naive 1-step version is a trap): (a) put a trigger
   on Sync3 — `AddTrigger` flexes A and B2, **but with min = 0** (its `SetRigidity`
   sets minNull); (b) in each lane's inspector (min/max widgets are now visible),
   re-check **"Min"** — this unmasks the stored min, which is still the nominal
   duration; (c) give the trigger an **always-true expression**. Now each lane pends at
   its nominal end and the sync fires the instant both have arrived. ⚠ Skipping step
   (b) makes the sync fire the moment B2 starts (AND-of-mins with min=0) — the diamond
   collapses. No OSC, no value-holder.

### The canon feature (small, precedented) — refined by the authoring sweep
- **⚠ Do NOT reuse `SetRigidity(false)` verbatim.** Its redo sets `minNull=true` +
  `maxInfinite=true` — i.e. **min = 0** (SetRigidity.cpp:46-64). In a trigger-less
  diamond, a min-0 lane pends immediately; if *all* lanes were flexed that way the sync
  would fire at once and the diamond collapses. The needed variant is **`SetFlexible`:
  `rigid=false, minNull=false, maxInfinite=true, min := computed`** (ordering trap:
  `setMinDuration` is a silent no-op while `minNull` is set — clear flags before
  setting values, IntervalDurations.cpp:69).
- **The min is a GRAPH property, not the lane's own nominal (user decision, 2026-07-13).**
  A flexed lane's min = *the earliest date its end sync could fire because of the other
  branches* — a PERT/critical-path forward pass over the scenario DAG:
  `earliest(sync) = max over incoming intervals of [earliest(startSync(itv)) + min(itv)]`,
  summing along chains and taking the **max at convergences** (re-diverging branches fall
  out naturally); then `min(flexedLane) := earliest(endSync) − earliest(startSync)`,
  computed **excluding the flexed lane itself**. ⚠ `min(itv)` here is the **masked**
  getter value (`IntervalDurations::minDuration()`): a `minNull` interval contributes
  **0**, exactly as the engine sees it — not the stored `m_minDuration`. Rationale: with
  `min = nominal` the disease is merely mirrored — if the parallel branch finishes
  *early* (trigger fired fast), *its* last rigid interval freezes green-and-silent
  waiting for the flexed lane's nominal. With the graph min, the structural branch is
  the **master clock** and the flexed lane elastically follows: cut short or extended,
  the sync fires exactly when the master branch completes. (User's diamond: B1 is
  min-null → `min(A) = 0 + dur(B2) = dur(B2)`.)
  - Note: at runtime, *any* `min(A) ≤` the true graph floor is execution-equivalent —
    B's own gates are what actually hold the sync. The tight PERT value is still
    preferred: the min brace then shows the real earliest end on the timeline, and it
    stays correct-by-construction if B's structure is later edited (trigger removed,
    branch re-rigidified) pending recompute.
  - Consequences: the min must be **recomputed** when the diamond's topology or member
    durations change (command-level maintenance, or computed at execution-build time —
    JM taste question).
  - Degenerate case needing an anchor: if **every** lane of a diamond is auto-flexed, the
    mutual computation collapses to 0 (each defers to the others). At least one branch
    must keep authored mins — i.e. a notion of the **timing-defining ("master") branch**
    vs elastic accompaniment lanes. In practice the branch carrying the structure (the
    sequence chain, with its authored section durations) is the master; auto-flexed
    accompaniment lanes are excluded from the earliest-date computation.
  - For the promoted-sequence case the two rules coincide: sections are rigid, so lane
    A's graph-min = Σ section durations = its nominal — until sections themselves become
    flexible, at which point the graph rule is the correct one.
- **(i) Expose flexibility without a trigger.** A toggle in the duration inspector
  (`EditionGrid`, DurationSectionWidget.cpp:130-141; the min/max widgets are merely
  *hidden* when rigid, :225-240) and/or an action next to `AddTrigger`
  (ScenarioActions.hpp:121). Undo-safe command machinery already exists.
- **(ii) Auto-flex on diamond creation.** Exactly two hook points cover all authoring
  paths, both with ready templates:
  - **`StandardCreationPolicy.cpp:150`** — every `CreateInterval*` funnels through
    `ScenarioCreate<IntervalModel>::redo`; it already auto-flexes a new interval created
    into a *triggered* sync (`if(tn.active())`, even seeding min/max = 0.8/1.2·dur).
    Widen the condition to `tn.active() || hasOtherIncomingIntervals(tn)` for the new
    interval, and give `CreateInterval` `SetFlexible` children for the *pre-existing*
    lanes (the `AddTrigger.hpp:38-68` pattern).
  - **`MergeTimeSyncs`** (Merge/MergeTimeSyncs.cpp:76-95) — where a sync gains its 2nd+
    incoming branch by merging; collect `SetFlexible` children in the ctor, replay in
    redo/undo (the RemoveSelection.cpp:160/320/334 pattern).
- **(iii) Sequences**: the sequence machinery creates/extends lane A with
  `rigid=false, min=nominal, max=∞` — one flag-set at creation/extend time.

### Landmines for the implementation (from the authoring sweep)
1. **`RemoveSelection.cpp:160-163` force-re-rigidifies** all intervals before any
   deleted timesync, unconditionally — a trigger-less flexible lane silently snaps back
   to rigid. Must become trigger/diamond-aware.
2. **`MergeTimeSyncs` already violates the "flexible ⟺ trigger" invariant** today
   (ORs `active()` onto the destination but never de-rigidifies its pre-existing
   incoming intervals) — proof the invariant is only enforced at authoring edges;
   nothing structural will fight the new state.
3. **`fixAllDurations` force-resets `rigidity = true`** (IntervalDurations.cpp:187-191)
   — and the in-flight Sequence code calls it at 9 sites. Lane-A flexibility must
   survive those (use `changeAllDurations` semantics or guard the reset).
4. Execution **never reads `isRigid`** — only the masked min/max reach ossia. But the
   *play-time* dashes and exec repaint-rects do check it (TemporalIntervalView.cpp:162,
   FullViewIntervalView.cpp:74, ScenarioPresenter.cpp:441-459), so `rigid=false` must
   genuinely be set (faking with maxInf alone won't render running dashes).
5. Cosmetic: with max=∞ the dashes/boundingRect stop at `defaultWidth`
   (TemporalIntervalView.cpp:52,142) — fine in a diamond (both lanes nominally end at
   the shared sync's date), but worth knowing when reports of "the dashes stop at the
   nominal end" come in.

### Adversarial verification results (fresh-eyes pass, folded in)
A second, adversarial source pass confirmed the core (AND-convergence, freeze-at-max,
keep-playing/hold-last-value in the flexible zone, clean stop at fire) and corrected /
sharpened four points:

1. **Sample-accuracy at the fire instant.** All running intervals are ticked for the
   full buffer *before* sync processing (scenario_execution.cpp:433-436), and overticks
   only exist in the finite-max clamp branch. So when the sync fires, the **flexible
   lane overshoots by ≤1 audio buffer** (its final token gets an end-discontinuity fade
   after the fact); followers start sample-accurately only in the max-reached case, and
   one tick late on a pure expression/manual fire. Musically negligible, but it is not
   "sample-perfect for everything".
2. **Progress UI saturates at nominal for infinite intervals** (my earlier reading of
   `setPlayPercentage` was wrong: the else-branch divides by `defaultDuration`, so the
   percentage grows past 1.0 — but the played-dash *view* clamps at nominal width,
   TemporalIntervalView.cpp:166,177). Cosmetic: the progress bar stops moving at the
   nominal end while execution continues. Worth a small UI fix alongside the feature.
3. **NEW LANDMINE — max forces triggers to fire (“trigger bypass”).** If *any* incoming
   lane of a sync is at its (finite) max, `maximalDurationReached` makes `trigger_sync`
   **skip expression waiting entirely** (scenario_sync_musical_execution.cpp:85-93 →
   scenario_sync_execution.cpp:97-98). Consequence: **a trigger on the shared end sync
   can only hold if EVERY incoming lane is flexible.** (This also finally *explains* the
   trigger⇒de-rigidify coupling in AddTrigger: a trigger genuinely cannot hold a rigid
   incoming interval.) For the diamond: a trigger on TS_end requires flexing B's last
   section too, not just A.
4. **NEW LANDMINE — seek/transport strips flexibility** (the ossia/score#253 hack,
   scenario_offset.cpp:147-185: seeking rigidifies every interval before the seek point
   and raises straddled intervals' min to the offset, "temporary (1 year later: haha)").
   Seeking past a flexible lane's nominal date treats it as finished-at-nominal and
   removes its flexibility for that playback. Diamond keep-playing (and IS triggers)
   will misbehave after such a seek — a known engine debt this feature inherits, worth
   fixing or at least documenting with it.

Also clarified: `is_timesync_ready` is **one** function (defined in the *musical* file,
forward-declared in the plain one — not two variants); the quantized path can only
*delay* firing to the grid, never fire early, and quantization can be **inherited from a
parent interval** even when the sync sets none. Pause/resume and speed changes are safe
in the flexible zone. A rigid lane freezing at max may audibly **click** (truncation
without fade) — one more argument for flexible lanes in diamonds.

### Consequences for this design study
- **§5's tension is largely dissolved**: lane A no longer freezes/goes silent at a held
  trigger — it *keeps playing* (real file tail, loops, held values) until the sequence
  completes, and everything releases together via AND-convergence (grid-aligned if the
  sync is quantized; the flexible lane overlaps the release by at most one audio buffer,
  faded). The only residual semantic: A's *timeline position* runs ahead of B's
  *progress* during a hold — inherent and, per the user, desired.
- **The user's long-standing OSC/value-holder workaround becomes obsolete** — diamond +
  flexible lane + AND-convergence is the native replacement. This benefits *any*
  parallel structure in score, independent of sequences: it is the general answer to
  "one branch must keep playing until another catches up."
- For the promoted-sequence architecture (§3), lane A's flags are set by construction,
  and **every IS trigger now behaves musically sensibly** out of the box.

---

## 7c. Implementation recommendation: auto-flexibility in diamonds (2026-07-13, with user)

### The criterion (two flavors, converged with the user)
For each interval `L` ending at a sync `S` with ≥2 incoming branches:

1. **Flavor 1 — redundant (parallel) edge → full elasticity.** `L` is *redundant* iff a
   directed path `startSync(L) → S` exists that avoids `L` (graph reachability). Such a
   lane is accompaniment by construction: `min := PERT floor of the parallel structure`
   (masked mins, max at convergences, authored values snapshot-before-apply),
   `max := ∞`. Cuttable **and** extendable. Only *matters* when the parallel structure
   is nondeterministic (contains an active trigger or non-rigid interval) — for a fully
   rigid structure floor = ceiling = nominal and flexing is inert (lean: skip it).
   - Covers: the user's diamond lane A; the promoted sequence's lane A (host interval —
     parallel path = the section chain). Sections/chain intervals never match.
   - Guards the master branch structurally: no chain interval is ever redundant, so the
     naive-rule failure (flexing B2 with a date-relative min) cannot occur.
2. **Flavor 2 — structural lane facing OTHER nondeterministic branches → extend-only.**
   `L` is not redundant, but another branch into `S` is nondeterministic (can outlast
   `L`): then `L` freezes green-and-silent whenever it arrives early. Fix:
   `max := ∞, rigid=false`, **min stays authored** — keeps playing while waiting, never
   cut (its content is structure, not accompaniment). Arises only with ≥2
   nondeterministic branches converging (e.g. two TP-bearing chains) — in a 2-branch
   diamond with a properly flavor-1-flexed accompaniment, the structural chain can
   never wait (earliest chain arrival ≥ accompaniment's floor ⇒ accompaniment already
   pending).
3. Neither condition → leave rigid (deterministic diamond: nothing to absorb).

**Inverse rule (diamond removed):** same predicates, stateless — when topology changes
and `L` no longer satisfies 1 or 2 (and its end sync has no active trigger), restore
authored rigidity (`min=max=default`). This is semantically necessary, not cosmetic: a
leftover flexible lane into a 1-incoming trigger-less sync fires at *min*, cutting
content.

### The unifying principle — wait-absorption
### (user case #2, `diamond-flexible+authored.score`; CORRECTED 2026-07-13: the user
### meant **B2** throughout, not B1 — the earlier "floor-equalization/min-raising"
### reading is DEAD)
One principle unifies everything: **any interval that can be made to WAIT at its end
sync — the sync's fire date can exceed the interval's own arrival — must be flexible.**
Early arrival is *absorbed by keep-playing*, never *prevented* by raising mins.

The failure in the user's file, correctly read: TP fires early at `T` → B1 is cut at
`T` (**by design** — that is what an interactive trigger is for) → B2 plays its rigid
content, finishes at `T + dur(B2) < min(A)` (A's authored gate, 1806324218) → **B2
freezes green-and-silent while A plays on**. The fix is on **B2**:
`rigid=false, min := its own DefaultDuration, max = ∞` (extend-only). Timeline:
`Sync3 fires at max(T + dur(B2), min(A))` — early TP → B2 finishes its content, then
dashes until A's authored min, then everything closes with A cut exactly at its min;
later TP → closes when B2's content is done. Nothing freezes in any case, **A's
authored min stays fully operative, and B1's min-null early-trigger freedom is fully
preserved (B1 needs NO change).**

### Simplified taxonomy: only two flexible-interval configurations

| Configuration | min | max | applies to |
|---|---|---|---|
| **Fully elastic** (cut + extend) | computed from sibling branches (PERT floor, masked mins) | ∞ | accompaniment lanes spanning the diamond — the *redundant edges* (A; the sequence's host interval) |
| **Extend-only** (never cut) | its **own DefaultDuration** (the authored/composed length in the inspector) — emphatically NOT `min=max=default`, which would re-rigidify; *being flexible* is the essential half | ∞ | **structural intervals that can be made to wait** at a multi-incoming sync — causes are all the same condition ("the sync can fire later than my arrival"): a sibling's **authored min gate** exceeding my earliest arrival (this file), sibling branches containing **TPs** (≥2 nondeterministic chains — the former "flavor 2"), or any combination |

Everything else stays rigid/authored. Consistency check across the user's cases: no
TP/authored-gate in A ⇒ B2 can never wait ⇒ B2 stays rigid (`diamond-flexible.score`);
A gains an authored min > B2's earliest arrival ⇒ B2 extend-only (this file).

**Conservative safety (important simplification):** extend-only is harmless when not
exercised — the min is what the interval had anyway, and the ∞ max is simply never
used if the interval never waits. So the auto-rule may **over-apply** extend-only
(e.g. to every structural interval ending at a multi-incoming sync) without semantic
risk; precision only affects how many intervals *display* as flexible. This kills the
need for exact waitability analysis, for `SetMinDuration` equalization edges, and for
any stateful min-raising/restoring machinery — all of which existed only in the
misread version.

### Architecture (the AddTrigger pattern, precedented end-to-end)
1. **Pure helper** `Scenario/Process/Algorithms/ParallelBranches.{hpp,cpp}`:
   `isRedundantEdge(scenario, itv)`, `branchIsNondeterministic(...)`,
   `parallelFloor(scenario, itv)` — reuse the boost graph in `Graph.cpp` (timesyncs =
   vertices, intervals = edges). ~150 lines, unit-testable.
2. **New command** `Commands/Interval/SetFlexible` (modeled on `SetRigidity`, but:
   `rigid=false, minNull=false, maxInf=true, min := supplied`; undo restores all prior
   fields). `SetRigidity` untouched for the trigger workflow. Flag-ordering trap:
   clear `minNull` before `setMinDuration` (silent no-op otherwise).
3. **Topology-edge commands get SetFlexible/restore children** (as `AddTrigger` carries
   `SetRigidity` children):
   - creating: `CreateInterval`, `CreateInterval_State`, `CreateInterval_State_Event`
     (all funnel through `StandardCreationPolicy.cpp:150`, which already auto-flexes
     into *triggered* syncs — widen for the new interval; children handle pre-existing
     lanes), `MergeTimeSyncs`/`MergeEvents`.
   - removing: `RemoveSelection` — **replaces landmine 1**: the unconditional
     re-rigidify at :160-163 becomes predicate-aware (`keep flexible iff sync.active()
     || flavor-1 || flavor-2`); plus `SplitTimeSync`/`SplitEvent` (splitting can
     dissolve a diamond).
4. **Phase 2**: (a) at execution build (`makeDurations`), recompute the floor fresh for
   non-rigid redundant edges — playback immune to stale mins after sibling-duration
   edits (any stored min ≤ true floor is execution-equivalent; display staleness is
   cosmetic); (b) manual flexible-toggle in the inspector (opt-out/in); (c) cosmetics:
   dash extent past nominal for ∞ intervals, progress-bar saturation.

### To bless (JM)
- The **behavior change** (diamonds now auto-flex instead of freezing) applies at
  edit-time only — saved documents untouched until re-edited (optional explicit
  "modernize" action).
- Deterministic diamonds: skip flexing (lean) or apply-inert for uniformity?
- Multi-hop *accompaniment chains* (a chain, not a spanning edge, meant as
  accompaniment) are V1-out-of-scope: predicate correctly won't match; those users get
  the manual toggle. Generalizing requires an explicit master/elastic branch choice.

Test fixtures: the user's `diamond.score` / `diamond-flexible.score`; unit tests for
the PERT helper; a 3-branch (two TP chains) fixture for flavor 2.

---

## 8. Open questions for JM / the user (answer before Step 1)

1. **Trigger semantics — mostly RESOLVED by §7b:** lane A is created flexible
   (min = nominal, max = ∞) so it *keeps playing* while B is held and stops exactly when
   the final sync AND-fires. Remaining sub-questions: (a) auto-flex on diamond creation
   vs explicit affordance? (b) should A's *min* be its nominal length (can't end early)
   — I'd say yes; (c) is the residual "A's playhead runs ahead of B during a hold"
   acceptable (it's inherent)?
2. **Do parallel non-sequence processes need to span the *whole* sequence, or vary per
   section?** The clean model supports whole-span (lane A) and per-section (inside Bk).
   Is that enough, or is "different bed under different sections, continuous" required
   (which is impossible — see §5)?
3. **Identity model:** grouping/tag (a) vs owned-but-registered (b)? And how hard do we
   guard against raw scenario edits breaking a sequence — hard-lock, warn, or
   degrade-gracefully?
4. **Ports:** where do the per-parameter outward cables attach in the native model —
   lane A, a wrapper, or auto-managed per section?
5. **Does the sequence still need to be droppable into arbitrary process containers,**
   or is "exists directly in a scenario (nestable)" acceptable? (The native form only
   lives in a scenario.)
6. **Migration:** convert existing encapsulated sequences on load, or support both forms?

---

## 9. One-paragraph recommendation

The parallel-ladder / native-IS direction is, as far as the model shows, **the only way
to make ISes genuinely first-class** (syncable, triggerable) — the encapsulation
alternative structurally can't. Its certain payoff is native trigger/sync on ISes; its
likely payoff is shedding the bespoke `SequencePresenter` churn that caused this
session's disc bug (the *value* sync is already native today, so that part is not a new
gain). The feasibility is not the blocker; the blocker is turning "Sequence-the-owned-
process" into "Sequence-the-recognized-pattern + guarded editing macros," and accepting
the continuous-vs-triggerable tradeoff for the audio bed. I'd **do Step 0 first**
(hand-authored scenario, zero code) to confirm the execution semantics and the desync
behavior; if those feel right, the rest is a substantial but well-understood UX/identity
engineering effort, best sequenced as §7 with the §8 questions settled up front.

Encouragingly, the delta is smaller than "rewrite": `SequenceModel` is *already* a
`ScenarioInterface` using the same `ProcessPolicy`/`SetNextInterval` machinery, and
extend *already* moves the host interval's end event — so much of the construction/
editing logic (curve-splitting on insert-IS, section wiring, extend) transfers; the new
work concentrates in (i) where the entities live, (ii) identity/guards, (iii) the UI
overlay, (iv) ports.

---

## Appendix A — code evidence (file:line)

**Topology / model (score).**
- Interval has exactly one start & one end state: IntervalModel.hpp:88-92, 297-298.
- State is 1-in / 1-out (single prev/next interval): StateModel.hpp:100-106, 147-148;
  `SetNextInterval` overwrites any prior: ProcessPolicy.cpp:169-178.
- Event holds many states: EventModel.hpp:65-66, 100. TimeSync holds many events:
  TimeSyncModel.hpp:57-62, 125. Scenario owns flat entity maps: ScenarioModel.hpp:147-151.
- Scenario is a multigraph (TimeSyncs=vertices, Intervals=edges; parallel edges OK):
  Graph.cpp:206-220; only global invariant is no-cycle: Graph.cpp:184-186, 246-249.
- Trigger fields on a timesync: TimeSyncModel.hpp:65-72 (expression/active/autotrigger).
- "Sync two elements" = **merge** their events/timesyncs: MergeEvents.cpp,
  MergeTimeSyncs.cpp; driven by MoveAndMergeState.hpp:318-332. (Not an expression ref.)
- Boundary value sync via shared state: ProcessPolicy.cpp:21-81 (`AddProcessBefore/After
  State` + previous/followingProcesses), wired at AddProcess:123-130 /
  SetPrevious/NextInterval:158-178. **Only bridges a state's own prev↔next pair** — so it
  syncs consecutive sections (what we need) but not across a parallel divergence.

**Execution / continuity (ossia).**
- Interval owns its processes; start/stop fan out to them: time_interval.cpp:316-342,
  time_interval.hpp:233. Boundary crossing stops previous, starts next (no carry-over):
  scenario_execution.cpp:41-74; discontinuity flags: continuity.hpp:9-45. → audio cut,
  automations restart clean.
- AND-convergence: scenario_sync_musical_execution.cpp:71-155 (`is_timesync_ready`).
- Trigger halt-until-true: scenario_sync_execution.cpp:82-173; entry
  process_this:177-202. Interval caps at max_duration (overtick clamp):
  scenario_execution.cpp:130-228; all running intervals ticked each frame :433-436.
- Per-process loop (repeats inside its own interval): time_process.hpp:135-157; wiring
  IntervalExecution.cpp:587-607. Full/small view are UI-only: IntervalModel.hpp:137-138.
- Sequence currently executes as a **nested `ossia::scenario`** with a custom
  `sequence_node`: SequenceExecution.cpp:44-100, 210-253; per-param forwarding :126-171.

**Current Sequence internals (score).**
- `SequenceModel : ProcessModel, ScenarioInterface`, four entity maps:
  SequenceModel.hpp:42-44, 53-56. 1 timeSync:1 event:1 state per IS: createIS
  SequenceModel.cpp:396-417. Sections wired via SetNext/PreviousInterval: createSection
  :420-443.
- Extend = AppendSequenceSection + MoveEventMeta on host interval end:
  ExtendSequence.cpp:39-71; append mutation :845-993.
- insert/move/ripple IS + curve polyline split: SequenceModel.cpp:1127-1246 (insert),
  1022-1062 (move), 1064-1112 (ripple), 207-282 (polyline helpers).
- Per-parameter ports (one ValueOutlet per param): rebuildParamPorts :588-646; vec expand
  :660-679; colour→Gradient :99-102. Serialization v2 (four maps):
  SequenceModelSerialization.cpp:26, 34-45, 181-201.
- Presentation: nested TemporalIntervalPresenters: SequencePresenter.cpp:181-263; row
  ports :359-419.

---

## Appendix B — a picture of the two options at a glance

```
TODAY (encapsulated):                     PROPOSED (native, flattened):

 Parent scenario                           Parent scenario
 ┌──────────────────────────┐              TS0 ─────── A (host itv, audio bed) ─────── TSn
 │ host interval H          │               │                                          │
 │  ├ audio process (bed)   │               └─ B1 ─(IS1*)─ B2 ─(IS2*)─ … ─ Bn ──────────┘
 │  └ Sequence PROCESS      │                     each Bk: 1 automation / param
 │      ├ section itv 1     │              (IS* = real parent timeSync: trigger-able,
 │      ├ (IS = internal ts)│               merge-able with any other timesync)
 │      ├ section itv 2 …   │
 │      └ per-param outlets │              "Sequence" = a GROUP over {A, B1..Bn, IS ts's}
 │  (ISes invisible outside)│               + editing macros; ISes first-class.
 └──────────────────────────┘
```
