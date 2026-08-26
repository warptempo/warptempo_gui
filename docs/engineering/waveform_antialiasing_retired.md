# The Waveform Antialiasing — retired 2026-08-01, technique recorded

RETIRED BY ARCHITECT VERDICT at the row-6 side-by-side (the kdenlive-redesign recolor, aliased-vs-AA binaries compared live): "subtle but noticeable — I prefer without it." The deciding property was PLACEMENT DETERMINISM — an aliased column is exactly one column, so where a marker visually sits is where it is, with no sub-pixel haze under the authoring gestures. The deletion reversed the earlier keep-it-inert future-proofing ruling by the architect's own word; this file is the reinstatement seed.

## What the AA was (the 2026-07-26 draw arc's soft passes; full detail in git history — the draw-arc commits of 2026-07-26 and the deletion commit whose reverse diff restores the code)

The plate is a direct ARGB32 coverage renderer, one synchronous full render per user-driven pan/zoom frame. The ALIASED parts that remain: per-column raw min/max interiors on the authoring lattice `g(k0+c)` (bit-exact pan invariance in both views), the 13-rung peaks pyramid — FOUR of them since 2026-08-25, one per display lane (the mono sum and its three frequency bands), persisted in the on-disk `.peaks` sidecar at v8, whose lane-major body carries the band identity (lane count, filter design id, the two crossovers, the kernel half-length) so a sidecar written under a different split reads back stale; the four level-0 lane signals are NOT persisted, so a cache hit still runs the filter pass at load and skips only the pyramid build — and the never-fade >=1px floor (now plain integer geometry). The ANTIALIASED parts that were deleted:

- FRACTIONAL COVERAGE on each column interior's two boundary rows: the min/max extremes carried their sub-pixel remainders as alpha-scaled ink on the first and last row of the column's bar, instead of flooring to whole rows.
- WU TIP POLYLINES: the column-to-column tip contour (max-to-max, min-to-min) drawn as Wu antialiased segments — `draw_segment` over per-axis `deposit_v`/`deposit_h` writers compositing via `blend_max` (max-blend so overlapping segments never over-darken), giving the tips a continuous connected look rather than isolated bars.
- RAW-TIP BANDS: sub-pixel tip material near the extremes rendered from raw samples so the Wu contour had true fractional endpoints to work with (not the floored bar rows).
- SYMMETRIC LEFT/RIGHT HALOS: each column's segments were fed one neighbor column of contour on each side so the polylines joined seamlessly across column boundaries with no half-drawn end caps at the render window's edges.

The never-fade floor guaranteed every column with any material painted at least one full-intensity pixel regardless of coverage — that rule survives in the aliased renderer as `floor`ed inclusive row fills (at least one row always paints; a lone spike stands alone).

## Reinstatement path, should it ever become relevant

Revert the deletion commit (git history around 2026-08-01, "the antialiasing machinery deletes") onto the then-current renderer — the deleted arm was verified instruction-identical to the shipped pre-toggle renderer when active, and the toggle pattern (one runtime `if` at the column loop's top, both arms always compiled) is the proven shape for running the two side by side again. The zoom-regime alternatives discussed at retirement (kdenlive's fine-zoom slope polygons, Reaper's sample-dot extreme zoom) are a distinct third rendering regime, not an AA question; nothing here precludes them.
