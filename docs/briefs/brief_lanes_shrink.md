# Coder brief: tools/palette/render.py — the ruler and marker lanes SHRINK with the fonts as they grow (set AD, the scale series)

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER — edit files, read-only git only, never stage/commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md). Only tools/palette/render.py and tools/palette/README.md change. Read render.py whole, especially the `fonts` option's seating code you wrote at commit 4285626c (`git show 4285626c -- tools/palette/render.py`) — the flag box / marker lane growth and the ruler lane growth paths.

## Why
Set AD renders the whole chrome at ×1.0 … ×1.5 of the Windows numbers (tmp/palette/set_ad_scale/mock_AD*.json). At ×1.0 (fonts ui_px 13, small_px 10) the render shows the icon row and the trim lane shrunk correctly, but the MARKER lane and the RULER lane stay at the scene's measured heights (40 and 56 device rows) although their text is smaller — the app derives both lanes from the text (the ruler lane from the label face: kRulerLabelCapTopPx's pad + ceil(ascent) + the head's rows; the marker lane from the flag box + a row of ground above and below), so a faithful mock must shrink them by the same rule that grows them.

## The rule (symmetric with the growth you built)
- MARKER LANE: height = the flag box's rows at the font (2-px top band + ascent + descent, as the app's rule you applied) + one logical row of ground above and below; when that is LESS than the measured 40 rows the lane shrinks by the difference, the stack below moving UP (the well top rises, the canvas gains the rows); the flag boxes re-centred in the lane as in the growth case. When the fonts option is unset, nothing changes (byte-identical).
- RULER LANE: height = the app's rule at the label face (ruler_layout's above pad + cap, then the below rows to the marker lane — the derivation you already use for the odd 53) ; when LESS than the measured 56 the lane shrinks by the difference, the stack below moving up; labels and ticks re-seated by the existing road. Unset = byte-identical.
- Both apply through the existing restack machinery (lane_h / case / fonts shifts compose).

## Byte-identity: with fonts unset, every existing theme byte-identical (cmp frozen on 1002, 1002a, s4 ± --label; AB3; AC trim24; AD1). With fonts {19, 15} the AC/AD1 renders must be byte-identical to before this change (they grew; nothing shrinks there) — verify on mock_AC_trim24 and mock_AD1_scale150.

## Deliver: re-render tmp/palette/set_ad_scale/mock_AD2_scale138, AD3_scale125, AD4_scale112, AD5_scale100 in place with --label (AD1 too, to prove identity). Report: the cmp results, each variant's lane table (ruler rows, marker rows, well top, canvas rows), and `git status --short` (two tools/palette files; nothing staged). Do not commit. README: one clause in the fonts row saying the two lanes follow the text both ways.
