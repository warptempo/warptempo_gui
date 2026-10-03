# Coder brief: tools/palette/render.py gains `buttons.sep_gap` (extra air either side of each separator)

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER — you edit files, run only read-only git, never stage or commit, never launch the GUI, never run scripts/warptempo_sync, never edit CLAUDE.md). Only tools/palette/render.py and tools/palette/README.md change. Read `button_geometry` in render.py (added at commit 2f21cdc5, `git show 2f21cdc5` for the record) and README.md's `buttons.gap` row before editing.

## What and why
The architect judged set X on the tablet (2026-10-02 ~18:55): THIN relief stays, the buttons TOUCH within a group (buttons.gap 0), and now he wants to see the air around each SEPARATOR increased in steps — "by either one or two pixels, or however many there are", and the mock-up policy is to go a little further than looks pleasant so he has a range to choose from. Today the measured gap on each side of a separator is 8 device px on the icon row and 10 on the bottom row (4 / 5 logical px; the two rows differ in the app and the tool reads them off the scene).

## The option
`buttons.sep_gap`: `null` (the default) = the measured gaps as today (byte-identical output for every existing theme; verify with cmp exactly as last time: frozen.json on 1002, 1002a, s4, with and without --label; set_w_thick's W1 on 1002; set_x_touch's X1 on 1002). An integer >= 0 = EXTRA logical px ADDED to every measured separator gap on BOTH sides of every separator in both rows (device px = sep_gap × S), so each row keeps its own base and the rows keep their difference. It applies inside `button_geometry`'s pack: every gl and gr the pack lays down grows by the extra, including the broken chain's kept gap (the view separator's 8 px to Source+Warp) — so the view group's separator moves left by the extra and the left chain's trailing separator (none on 1002) would move right. `sep_gap` requires a re-pack: when `buttons.gap` is null and `sep_gap` is not, re-pack with the measured button pitch (gap 2 on these scenes reproduces it — derive the measured within-group pitch from the scene rather than assuming 2, or simply treat gap null + sep_gap set as "pack with the scene's own within-group step"). Validation like `buttons.gap`'s (whole number, not bool, >= 0). The overlap refusal stays.

## README
Add a `buttons.sep_gap` row beside `buttons.gap`, same voice, and show `"sep_gap": null` in the buttons default.

## Deliver (tmp/palette/set_y_sep/, gitignored scratch; create it)
Five themes from tmp/palette/set_x_touch/mock_X1_thin_touch.json (thin, gap 0), differing only in sep_gap and name/description:
- mock_Y1_sep_plus1.json  sep_gap 1
- mock_Y2_sep_plus2.json  sep_gap 2
- mock_Y3_sep_plus3.json  sep_gap 3
- mock_Y4_sep_plus4.json  sep_gap 4
- mock_Y5_sep_plus6.json  sep_gap 6
Description for each: "Set Y = the frozen design (thin relief), the roster buttons touching within a group (buttons.gap 0), the air either side of each separator widened by N logical px (icon row 4 → 4+N, bottom row 5 → 5+N; architect 2026-10-02 ~18:55: 'increase the separation around a separator … show me an increase by one or two pixels')." with N filled in. Render all five with `--label` on the default scene into the same dir. Report: the cmp results, the resulting per-side gaps in device px per row for each N, the packed x of IconLoadInPlace, ViewSW and IconMarkerDrop for Y5, and `git status --short` (only the two tools/palette files modified; nothing staged). Do not commit.
