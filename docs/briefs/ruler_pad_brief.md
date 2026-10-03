# BRIEF: the ruler labels' cap top sits 4 authored rows under the lane's top AT EVERY SCALE (2026-10-02)

Coder rules (CLAUDE.md): you are the only writer of source; read-only git only; no commits; never launch the
GUI; never the sync script; build to self-check with `cmake --build build -j$(nproc)`; no edits to CLAUDE.md.
Not a frozen area (src/gui/). Opus high.

THE RULING (architect 2026-10-02, on the tablet at gui_scale 200): "reduce the space between the trim bar and
the timestamp lane by one authored pixel — two device pixels on the tablet". The owner comments already hold
the arithmetic: `kRulerLabelPadTopPx` (src/gui/paint_handler.cpp ~3828) seats the labels' cap top 4 rows under
the lane's top at 100% (pad 1 + ascent 15 − cap 12 = 4), but at 200% the 32px face measures ascent 30 and a
22-row cap, so pad 2 + 30 − 22 = 10 device rows = FIVE authored rows, one more than designed. That extra
authored row is the gap he sees.

THE CHANGE: the pad is no longer a constant scaled; it is DERIVED so the cap top lands at exactly 4 authored
rows (4 × scale device px) under the lane's top at every scale:
    pad = 4·scale − (ceil(ascent) − cap_height)   of the ruler label's face, clamped at 0
(at 100%: 4 − 3 = 1, unchanged; at 200%: 8 − 8 = 0, the labels move up 2 device px; at 50%: 2 − (8 − 5) → 0
clamped, unchanged). Read the face's ascent and cap height from the product's own metrics road (the metrics
table at src/gui/gui_font_bundled.cpp; `redesign_baseline` in paint_handler.cpp already centres the cap band,
so it knows where cap height lives — reuse that accessor, do not add a second metrics table). Rounding per
the ROUNDING rule (std::nearbyint where a value becomes a pixel). The 4 becomes a named constexpr owning the
ruling (the authored cap-top row), replacing kRulerLabelPadTopPx; rewrite the owner comment to the new rule
with the three worked scales and the date; update the kRulerLaneHeightPx block in src/gui/render.h (~1915),
whose worked 200% and 50% lines state the old baseline (its 200% line becomes: baseline 0 + 30 = 30, ink rows
8..29, head from 34: FOUR rows of ground — state that plainly; the lane height itself is UNCHANGED, 29 authored
rows, so main.cpp's stack figures do not move). Grep every site that quotes the pad or the "cap top 4 rows"
(render.h ~1916, paint_handler.cpp ~3941 and ~4053) and re-derive them. Nothing else changes: no lane heights,
no tick geometry, no playhead head.

Report: the diff summary, the build result, the three worked scales as the code now computes them (print
them from a scratch calculation, not from memory), and the head's clearance at 200% after the change.
