# Brief: the theme catalog, step 13 — the mock renderer at the tablet's own geometry (2026-10-03)

Model: Opus, effort high. You are THE CODER.

Project: Warptempo GUI, /home/b/.warptempo/warptempo_gui. Read CLAUDE.md first (you are THE CODER: you edit files, may build build/, run only read-only git, never stage or commit, never launch the GUI, never build the APK, never run scripts/warptempo_sync in any form, never edit CLAUDE.md). THIS BRIEF TOUCHES tools/palette/, tools/theme_catalog/crops.py and its README, docs/themes/ (the regenerated crops and CATALOG.md), and scratch under tmp/. HEAD is 21814db3. tools/palette/README.md and render.py's head describe the renderer; its last judged geometry is set AD2 (themes/ad2.json).

## The ruling (architect 2026-10-03)

"Bring the mock tool into greater accuracy … we're just targeting the tablet." Today the renderer draws relief lines 2 device px wide and the UI face at 34 px, where the app at gui_scale 275 draws a one-Windows-px line as 3 device px and the 13-px face at 35.75 px (CLAUDE.md, ROUNDING AND THE UNIT: every chrome length authored in Windows-95 px, scaled_px = std::nearbyint(px × 2.75) AT THE ELEMENT, composites summed from rounded parts; fonts as unrounded doubles 13 / 10 / 12 × 2.75; Roboto / Roboto Mono from fonts/ with SLIGHT hinting, src/gui/gui_font.h). THE TARGET: one TABLET geometry (2304 × 1440 at 275) in which every chrome length, line width, font size, baseline and text position is the app's own, derived the app's way from the app's authored constants (read them at their owners: main.cpp's lane table, render.h, paint_handler.cpp, folder_overlay.h, the modal dialog and card painters), not measured off the old mocks.

## The work

1. A tablet geometry option (or a new base theme, say which) that reproduces the app at 275: the menu row, the icon row (the 23 × 22 case), the trim lane, the ruler and playhead, the marker lane and flags, the well, the bottom row (status panels, clock, buttons), the dialog and the card added in step 11. Keep the old geometries available so earlier sets still re-render byte-identically (compare.py 0 px on ad2, win95_standard, frozen); the new geometry becomes the default for windows-95-standard mocks from now on.
2. Text through the same shaping road as the app where Python allows (FreeType + HarfBuzz on the embedded faces, SLIGHT hinting, the app's baselines: redesign_baseline / line_baseline); state any residual difference you cannot close and its size in device px.
3. DERIVE, DO NOT MEASURE (architect 2026-10-03: "derive that from the code"; the tablet's cover is shut, so there is NO screencap this round). Check the derivation by self-consistency instead: for each region, list the app's authored lengths and the renderer's device px, and show they agree at 275 (a table in the report). A capture-diff against the live app is DEFERRED to a later glass session; say which regions you would diff first.
4. THE CROPS (tools/theme_catalog/crops.py, docs/themes/crops/): re-render all 97 in the CURRENT design — the selected flag's WHITE OUTLINE (step 12, HEAD 21814db3: flags.selection "outline"; the underline retired) — in the new tablet geometry if the crop windows can follow it (adjust crops.py's region rows to the new geometry, stated), and update their README / CATALOG.md wording.
5. Re-render mock set AW (tmp/colour_arc2/aw/make_aw.py, from step 12) in the new geometry AND the current levels (the field, card and selection now darken in proportion: the editing flag's field must show #494949, not step 11's white) into tmp/colour_arc2/aw2/ with the same names, for the planner to push.

## Report

Files changed, the geometry's derivation table (each length: Windows px → device px at 275, and its owner in src/gui), the residuals, the AW re-render paths. Commit nothing.
