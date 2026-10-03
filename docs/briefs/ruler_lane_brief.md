# BRIEF: the ruler lane's height follows the label face, so the playhead head's clearance is ONE authored row at every scale (2026-10-02)

Coder rules (CLAUDE.md): you are the only writer of source; read-only git only; no commits; never launch the
GUI; never the sync script; build to self-check with `cmake --build build -j$(nproc)`; no edits to CLAUDE.md;
do not touch tmp/palette/mocktool/ (another agent owns it). Not a frozen area (src/gui/). Opus high.

CONTEXT. Commit 94403846 (just landed) seats the ruler labels' cap top 4 authored rows under the lane's top at
every scale (kRulerLabelCapTopPx, paint_handler.cpp: pad = 4·scale − (ceil(ascent) − cap)). The lane stayed at
29 authored rows (kRulerLaneHeightPx, render.h; ruler_lane_h_px() = scaled_px(29)), so at 200 % the labels
moved up 2 device px and the room landed UNDER the digits: four device rows of ground between the digits'
lowest ink and the playhead head, where the architect's ruling (2026-09-23, at kRulerLaneHeightPx) is ONE
authored row of empty ground. And the waveform gained nothing: both devices run max_waveform_height=0 (no
clamp), so the waveform takes the whole leftover and only a shorter lane gives it rows.

THE RULING (architect 2026-10-02, on the tablet): the gap between the trim bar and the timestamps closes by
one authored pixel (two device px at 200 %) and that pixel GOES TO THE WAVEFORM; the head's one-authored-row
clearance holds at every scale.

THE CHANGE: the ruler lane's height is DERIVED from the label face's metrics, not authored and scaled:
    lane = pad + ceil(ascent) + scaled_px(1) + head_h
where pad is kRulerLabelCapTopPx's derived pad (the same formula; put it in ONE helper both sites call, so
the painter and the lane table cannot disagree), ascent the label face's (the 16px sans at gui_scale), the
scaled_px(1) is the ruling's one authored row of ground, and head_h the playhead head's height as the painter
draws it (12 authored rows at 100 %, 24 at 200 % — find its owner in paint_ruler_row / render.h and read the
one constant, do not restate it). Worked: 100 %: 1 + 15 + 1 + 12 = 29 (unchanged); 200 %: 0 + 30 + 2 + 24 = 56
(was 58); 50 %: 0 + 8 + 1 + 6 = 15 (was 14, where the head touched the digits). Print these from a scratch
calculation through cairo-ft on fonts/Roboto-Regular.ttf (tmp/ruler_pad_calc/calc.cpp from the previous brief
is a start), not from memory.

THE ROAD TO THE METRICS: ruler_lane_h_px() is read by main.cpp's lane table at layout, outside any cairo
context. The product has ONE metrics owner — the measured table at src/gui/gui_font_bundled.cpp (read its
head: faces, sizes, cap, ascent, SLIGHT hinting on both devices) and cap_height_px / line_baseline in
paint_handler.cpp over a scaled font. Choose the road that keeps ONE owner: either an accessor on the table
(face, px size → ascent, cap) that both the painter's helper and the lane function read, or a cached scaled
font the layout can query. No second table, no hard-coded 30/22. If the table lacks a size the scale asks
for, say so in the report rather than inventing a fallback.

OWNER COMMENTS TO RE-DERIVE (grep, do not trust this list): kRulerLaneHeightPx's block (render.h ~1915: the
29 becomes the 100 % worked value of a derived height; rewrite the block as the rule with three scales and
the two dated rulings); main.cpp's lane table and the stack figures that read the ruler's height (~150, ~210–
235: "58 ruler", "270 above", "118 block", gap 2 at 76 — the 200 % stack becomes 60 + 92 + 20 + 56 + 40 = 268
above; recompute every figure on that page and state the stacks at max_waveform_height=0 as well as at the
default 500, since both devices run 0 — with 0 the tablet's waveform is the leftover 1440 − 268 − 94 = 1078);
render.h ~590 and ~1880 (the head/crop remarks that quote 29); paint_handler.cpp ~4053 and the
kRulerLabelCapTopPx comment's last line; docs/engineering/closed_questions.md's ruler line (owner names);
anything else `git grep -n 'kRulerLaneHeightPx\|ruler_lane_h_px\|29 rows\|lane 58'` finds. COMMENTS state the
current rule and its reason with the date — no history of the 58, no round numbers.

Report: the diff summary, the build result, the printed three scales (pad, baseline, ink rows, lane, head top,
ground rows), the tablet's waveform height at max_waveform_height=0 before and after, and any site you could
not re-derive.
