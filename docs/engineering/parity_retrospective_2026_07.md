# The Ableton-parity arc retrospective (2026-07, closed)

The bird's-eye inspection of the ~142 commits since 2026-07-21 — the
pointer/zoom re-maps through the re-coupling / selection / region /
group-gesture arcs to the C9 nudge-twins collapse — answering the
architect's question "what would we do differently from scratch?".
Two unanchored takes (planner, then a fresh codex session instructed
not to read the planner's) were synthesized 2026-07-24; every planner
position had a codex counterpart with codex's uniformly the more
operational form. This note is the durable record; the standing
rulings at the end are the part that binds future work.

## The headline (both takes converged)

The feel-driven churn was LEGITIMATE and labwc-owned — the strip-drag
v1→v6 walk, the stem death models, the scrub one-shot ruling, the
focus-model iterations all required the architect's hands on the real
compositor, and no up-front design substitutes for that. The EXPENSIVE
defects were missing STRUCTURE: nearly every HIGH/MEDIUM codex finding
of the arc traces to one of five facts the code kept leaving implicit.
A from-scratch build buys cheap reversals, not clairvoyance.

## The five implicit facts (the arc's recurring defect roots)

1. ENTITY IDENTITY — row-index-as-identity forced the reorder remaps,
   the undo touched-set identity hints, and the generation-stamped
   hover guards; stable session marker IDs would have dissolved the
   class. (Recorded as residue R1, HIGH.)
2. OWNERSHIP DIRECTION — when two subsystems share state, who owns
   each transition. The selection→region→trim downward-only rule and
   RegionProvenance arrived LAST, after the two-way coupling shipped,
   failed in use, and died over three demote-site-patching rounds.
   The stable shape (a provenance enum with demote-on-replace, one
   reconciliation owner) was derivable up front. (R2, MEDIUM-HIGH.)
3. AUTHORED GRID — which stored value a gesture changes and whether
   its grid is finer than the requested display step. The dd36ed8
   misread and the min-step churn both came from a guarantee imported
   across gestures without its grid-fineness precondition; the
   distinction belongs at a type boundary (frame-grid kernels get the
   one-column guarantee, cents-grid kernels the minimum-cent rule),
   never inferred from "looks like a nudge".
4. COORDINATE EPOCH — live vs displayed basis. The 2026-07-22
   displayed-basis discipline was established reactively after the
   slippery-drag class; a required captured gesture display basis was
   knowable at the first drag surface.
5. TRANSACTION TAILS — commit/cancel/motionless outcomes and their
   ordering. The C9 tail drift and the A2 coalesce-clock delta are
   exactly what one shared gesture transaction shell (capture,
   cross_threshold, commit, cancel; gestures supply only transform +
   wall policy) prevents structurally.

The operational form codex proposed for all five: a behavioral MATRIX
over {surface, modifier, W/P, S/T, selection cardinality, writability}
written BEFORE any gesture code — each cell naming the semantic verb,
the authored value and its grid, the epoch, the wall policy, every
tail, and the motionless/Esc/lost-button outcomes.

## Standing rulings (architect, 2026-07-24 close)

- THE FROM-SCRATCH REDESIGN IS DECLINED: zero performance benefit,
  theoretical code-quality benefit only, and re-proving codex-hardened
  invariants without a test suite is the hidden cost.
- RETROFIT-ON-DEMAND is the standing rule: a future feature touching
  an area adopts the from-scratch shape FOR THAT AREA in its own arc.
  R1 (stable IDs), R2 (one region reconciliation owner), R5 (the
  input_pointer.cpp monolith split) stay RECORD-ONLY until then.
- R3 (stem persistence coupled to damage/command epochs) is NOT
  residue: the damage-quiescence scope is the ruled product semantic
  ("the stem survives any command that paints nothing"), final-state.
- R4 (genealogy prose) took the MIDDLE PATH: dead-model NAMES (the
  two retired directional coupling labels, retired-revision tags)
  purge to behavioral statements; don't-re-propose records and dated
  provenance of LIVE rulings stay.

## Process adoptions (planner-side, standing, no ruling needed)

- A PRE-CODE FRAME REVIEW plus a report-only codex FALSIFICATION pass
  before source work on any new gesture.
- A state-transition row (store → basis → viewport → selection →
  region → playhead → undo → trigger → stem/damage) in every mutating
  brief.
- Analogies only with DIFFERENCE TABLES (authored type, grid, reach,
  walls, playback, map) — never a bare "the twin of X"; guarantees
  stated precondition-first.
- Minimal labwc vertical slices for feel trials; consequence-harvest
  (assumptions, not just code) as a reversal's definition of done.
- Inventory retells RE-GREP membership; one authoritative enumeration
  site per concept (durable in CLAUDE.md).

## Avoidable vs intrinsic (the commit-level split, condensed)

INTRINSIC (labwc-owned, rightly iterative): strip drag v1-v6, the
AA/normalization display feedback, scrub one-shot vs drag, the stem
death models, most lane/Esc revisions. AVOIDABLE (missing algebra,
ownership, or epoch facts — not taste): the first whole-waveform
scrub's surface claim, the flag/lane split, the displayed/live basis
repairs, the decoupling/re-coupling round-trip (given the eventual
scrub split, the lead-in workflow never required decoupling), the
two-way coupling's cyclic graph, the section-model corrections, the
group-gesture corrective rounds, dd36ed8 in full, and the inventory
retell rounds.

The dispatch patterns that earned their keep unchanged: small commits
with genealogy in commit messages, fresh-session report-only codex
rounds, verified reverts, and one long-lived coder carrying a feature
plus its conversion rounds with zero context re-transfer.
