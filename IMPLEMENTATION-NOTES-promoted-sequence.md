# Promoted-sequence V1 — implementation notes & decisions
*(autonomous overnight run; every decision below is open for correction)*

## ⚠ REVAMP (Pia's correction, applied): the gesture IS the blue +
The context-menu entries are GONE. The entry point is the **blue + dragged
from a state**, exactly like the old sequence process:

- **First drag** from the end state of a plain interval: the interval is
  **converted automatically** (automations → sequence branch; the interval
  becomes the flexible parallel branch), and if the drag went beyond the end,
  a section is appended up to the released date. One undoable command.
- **Subsequent drags** from the shared end state: extend to the released date.
- During the drag you see the normal ghost interval; it is rolled back at
  release and replaced by the real structure.
- **Old encapsulated-Sequence hosts are MIGRATED** (Pia's request): the blue +
  on their end state flattens the old process into the promoted form — every
  internal section becomes a native interval, every internal IS becomes a
  native state/event/timesync at the right date, every automation is copied
  **with its full curve** (same address/min/max), IS values recorded on the
  new states, the old process removed, the host made flexible — then the
  extension is appended. One undoable command; undo restores the old process
  intact. V1 migration limits: automations only (gradients/other section
  processes are dropped from the sequence branch — flagged), cables into the
  old process's per-parameter outlets are lost (native per-automation outlets
  replace them).
- **Bare-state creation is promoted too** (Pia): blue-+ from a bare state
  immediately creates the native diamond — parallel branch to the released
  date + one-section sequence branch seeded with one lane per parameter of
  the start state, ramping from the state's value to the current device value
  (numeric; vec expanded per component; colors as Gradients), domains read
  from the device nodes. No old-style process is ever created any more.
- **Gradients are full sequence-branch citizens** (Pia): moved on conversion,
  copied stops-and-all on migration, continued flat on extension, color
  boundary values recorded on IS states in the address's own unit.

## Ports & slot-sync: DONE via the SequenceAnchor (Pia's owner decision)
Pia settled the identity question: **the parallel branch owns sequence-wide
behavior** ("there will always be one"). Realized as a thin `SequenceAnchor`
process living in the parallel branch (option b2 — zero core-model changes):
- **Per-parameter ports**: the anchor exposes one (inlet, outlet) pair per
  parameter; section lanes are auto-cabled into the inlets by every gesture
  macro; users cable outward from the outlets — one cable per parameter for
  the whole sequence. Ports keep identity across namespace changes so cables
  survive; execution is a trivial forwarding node.
- **Live slot-sync**: the old watchSection/mirrorRackLayout machinery lives
  in the anchor now (lanes matched by address; orphan lanes keep their slot;
  mirroring deferred past command signal bursts; watchers re-derived after
  extensions).
- **Stacking rule**: the sequence branch is created ABOVE the parallel branch
  (IS verticals stay short). Full "two slots of one object" grouped
  move/selection remains presenter work — the one UI piece still open.

### Superseded (kept for history):
(old text) Ports & slot-sync were absent pending the identity decision
Both old-process features are absent and both are gated on the same design
question (who owns sequence-wide behavior in the native world):
- **Per-parameter ports** (one outlet per parameter for the whole sequence):
  natively each section lane exposes its own outlet. A unified port needs an
  owner object — options: outlets on the parallel branch, a thin wrapper
  process, or per-section cables managed automatically. JM's dynamic-port
  work is relevant. → discuss.
- **Slot-sync across sections** (move/resize a slot in one section mirrors to
  all): the old watchSection/mirrorRackLayout machinery needs a live owner;
  candidates: a document plugin that recognizes promoted structures and
  mirrors racks, or command-level interception. The old logic itself is
  largely reusable. → discuss. Meanwhile: new sections are created with one
  slot per lane (one-shot parity at creation; no live sync).
- Extend length = wherever you release (no longer "last section's duration").
- Conversion no longer requires automations (an empty section is fine — drop
  parameters later).

Wiring: `ScenarioCreation_FromState.hpp` release handler →
`PromotedSequence::convertOrExtend(ctx, scenar, prevInterval, releaseDate)`.

## What exists now

New code (all in `Scenario/PromotedSequence/`, plus 2 small edits to
`IntervalActions`):

- **`PromotedSequence.{hpp,cpp}`** — the core:
  - `locate()`: recognizes a sequence from any member interval, **purely
    structurally** (no stored identity): a *parallel branch* (host) spanning
    start→end syncs, beside a *sequence branch* (chain of sections joined by
    shared IS states).
  - `convertToSequence()`: your instruction implemented — the interval's
    **automations move to a new single-section sequence branch**; everything
    else stays in the interval, which becomes the **parallel branch**
    (flexible: min = nominal, max = ∞ per the design's diamond rules).
    Boundary values are recorded as messages on the shared start/end states.
  - `extendSequence()`: appends a section — creates a new IS
    (state+event+timesync) at the old end date, rewires the last section to
    end there, pushes the shared end sync forward by the last section's
    duration (parallel branch stretches, GrowShrink), creates the new section
    between IS and shared end, and continues **every parameter flat at its
    boundary value** (same min/max domain). IS values recorded on the IS state.
- **`SetFlexible`** command — the §7c building block: `rigid=false,
  minNull=false, min := given, maxInf=true` (NOT SetRigidity's min=0).
- **`RewireIntervalEnd`** command — re-points an interval's end state
  (uses `SetPreviousInterval`/`SetNoPreviousInterval`, so process↔state
  boundary sync is rewired natively).
- **UI**: right-click an interval → *Interval ▸ Convert to sequence* /
  *Extend sequence*. Extend works from ANY member (host or section).

Everything is an `AggregateCommand` macro over native commands
(`CreateState`, `CreateInterval`, `CreateTimeSync_Event_State`,
`MoveProcess`, `MoveEventMeta`, `InitAutomation`, `AddMessagesToState`) —
single undo per gesture, no model/serialization changes anywhere.

## What you get for free (the whole point)

- **ISes are real states/events/timesyncs**: add a trigger on an IS (T key) —
  natively; sync an IS with anything (drag-merge) — natively; conditions —
  natively. AddTrigger even de-rigidifies the incoming section natively.
- **Boundary value sync between adjacent sections** = the native
  `ProcessPolicy` shared-state mechanism (what we spent a day debugging in
  the custom presenter is simply *absent* here — no custom presenter).
- Save/load, copy/paste, undo/redo of the *elements* = native scenario
  behavior.
- The parallel branch keeps playing while an IS trigger holds (the diamond
  keep-playing semantics — it's created flexible).

## Decisions taken (your latitude, flag anything wrong)

1. **Identity is structural, not stored.** `locate()` re-derives the sequence
   from the graph each time. No new serialized object, no group id. Cost:
   raw edits can make a sequence unrecognizable (then Extend just refuses);
   nothing corrupts. This keeps V1 honest about §8-Q3 (identity model is
   JM's/your call — nothing is committed to).
2. **Extend length = last section's duration.** No dialog. (Old process: blue
   + drag chose the length.)
3. **New sections continue FLAT at the boundary value** — V1 does *not* read
   the current device value (old process ramped to it). Deliberate: device
   reading pulls in Explorer dependencies; easy to add later.
4. **Conversion requires ≥1 automation** in the interval (otherwise no-op).
   Workflow for "from scratch": draw interval → add automations → convert.
   (From-state creation like the old blue-+ is not in V1.)
5. Section lane is placed at host's y + 0.1 (visual offset).
6. Gradients/vec/color: **not in V1** (automation processes only; a vec
   address currently converts as one automation if it was one automation).
7. No custom UI: no rail, no handles, no slot mirroring, no per-parameter
   single ports (each section automation exposes its native outlet). The
   scenario's own editing (drag events, triggers) IS the editing model now.
8. Old `Scenario/Sequence/` process is untouched and still builds — the two
   coexist; nothing migrates automatically.

## Known risks / untested (test these first)

- **Undo of Extend**: MoveEventMeta's snapshot-undo trap is ordered around
  (rewire → move → create), but undo of the full macro needs GUI testing.
- CreateInterval between existing states derives duration from dates —
  verified in code path, not yet in GUI.
- Save → reload of a converted document.
- Extending twice in a row; converting two intervals in one scenario;
  trigger on an IS then extend.
- The moved automations keep cables? (`MoveProcess` is the native drag-move,
  so should behave as a manual move does.)

## How to test (5 min)

1. Draw an interval; add 2 automations (different addresses) + 1 sound file.
2. Right-click → Interval ▸ **Convert to sequence**. Expect: automations now
   in a thin parallel interval below (section 1); sound stays; host shows
   flexible (brace/dashes per its flags).
3. Select either interval → **Extend sequence**. Expect: new IS
   (state/event/sync) at the old end; new section with flat automations; host
   stretched.
4. Put a **trigger on the IS** (select its sync, T). Play: section 1 plays,
   holds at IS (host keeps playing!), fire trigger, section 2 plays, all ends
   together.
5. Drag the IS event left/right: sections resize natively (curves scale per
   normal interval behavior).
6. Undo everything step by step.

## Where

Branch `deeper-sequence-exploration`, worktree
`~/dev/score/deeper-sequence-exploration`, build dir `build/` (own binary:
`build/score`). Design doc: `DESIGN-ises-in-parent-scenario.md` (§7b/§7c for
the diamond semantics this builds on).

## Self-test harness (2026-07-26, updated same day)

`SCORE_PROMOTED_SEQ_SELFTEST=1 build/score` runs a structural test inside the
real app and exits with 0 (all green) / 1 (assertion failures) / 3 (crash,
with a backtrace on stderr). 99 assertions, `ScenarioValidityChecker` after
every phase:

- convert → auto-extend → extend; undo ×2 / redo ×2 (MoveEvent snapshot
  ordering holds); byte-array save/close/reload; create-from-state.
- migration: an old encapsulated Sequence process (built via commands) is
  converted by the same blue-+ path; undo restores the old process exactly.
- deleting the last section + undo (RemoveSelection landmine) round-trips.
- JSON save/reload: all three promoted hosts locate correctly.
- auto-flex diamonds (see below): elastic PERT min, extend-only closing edge,
  macro undo, dissolution by deletion and by sync-split, all with undo.

It found and fixed two crashes before any GUI session: the anchor's missing
LayerFactory registration, and rebuildPorts destroying Ports before their
PortItems (publish empty port lists first). The risks previously listed as
"untested: undo of extend, save/reload" are now machine-verified; still
GUI-only: slot-mirror feel during drags, stacking-rule visuals, cabling UX.

## Auto-flex diamonds (design §7c V1, @d239acd40)

The general wait-absorption rule now applies to ALL diamonds, not just
sequences: closing an edge onto a sync that already has incoming branches
flexes whatever can be made to wait (elastic redundant edges get the PERT
floor of their parallel structure as min; structural lanes facing a
nondeterministic sibling become extend-only, authored min kept). Deterministic
diamonds stay rigid. Dissolution (deleting a branch, splitting the shared
sync) statelessly restores authored rigidity. `Scenario/Process/Algorithms/
ParallelBranches.{hpp,cpp}` holds the predicates; hooks in
StandardCreationPolicy, CreateInterval, MergeTimeSyncs, RemoveSelection,
SplitTimeSync.

Known V1 limits (from the design doc, deliberate):
- Reverting an elastic lane loses a hand-authored min (restore is
  min=max=default); the doc's "flexibility as a mask over stored values"
  refinement is not implemented yet.
- Sections of a promoted sequence may pick up extend-only flavor when the
  extension edge closes into the final sync — semantically inert (the host's
  min gates the sync) but they may *display* as flexible; judge in GUI.
- No execution-time floor recompute yet (stale mins after sibling edits are
  display-only); no manual flexible-toggle in the inspector yet.

## Second evening pass (2026-07-26, @01c0f172b)

- **Execution smoke-test** added to the self-test (plays the whole document
  ~2.5s via PlayGlobal, then stops). It caught **crash #3**: anchor_node
  double-freed its ossia ports at exec teardown (unique_ptrs + base-class
  ownership) — fixed; pressing stop after playing a promoted sequence would
  have crashed. Caveat: no audio callback in the sandboxed run, so the
  transport doesn't advance — actual audible playback is still yours to test.
- **Demo scores**: open `tests/demos/promoted-sequences-demo.score` (three
  promoted sequences: converted / created-from-state / migrated) and
  `tests/demos/autoflex-diamond-demo.score` (triggered diamond, elastic +
  extend-only lanes). Regenerate anytime with
  `SCORE_PROMOTED_SEQ_SELFTEST_SAVEDIR=<dir>` on a self-test run.
- **Inspector "Flexible" checkbox** (§7c i): duration section, toggles
  SetFlexible (extend-only: authored min, ∞ max) / SetRigidity(true).
  Tracks model changes (trigger, auto-flex). GUI-untested — look at it.
