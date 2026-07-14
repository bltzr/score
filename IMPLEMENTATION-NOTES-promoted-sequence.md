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
- Creating from a bare state still uses the legacy CreateSequence (old-style
  process with device-value ramps) — it then gets migrated on its first
  extension. Porting the ramp logic to a native-first creation is a clean
  follow-up if preferred.
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
