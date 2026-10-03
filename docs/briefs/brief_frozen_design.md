# CODER BRIEF — the frozen Windows-95 relief design, the palette scaffold and the knob strike (2026-10-02)

The planner wrote this; the architect ruled every point in it today (dates below). You are the CODER: Opus, high
effort. You are the ONLY writer of source. You edit files, you may build (`cmake --build build -j$(nproc)`) to
self-check, you run only READ-ONLY git (status / diff / log / show). You never stage, never commit, never launch
the GUI, never edit CLAUDE.md, never touch `scripts/warptempo_sync`, never run anything under `tmp/` that writes
outside `tmp/`. Session scratch goes in the gitignored `tmp/`. Read `CLAUDE.md` first (the standing rules: the
freeze, the comment doctrine, no backstops, rounding, names) and `docs/engineering/closed_questions.md`'s headings.
This is ONE commit's worth of work: finish all of it; if you run out of room, write `tmp/brief_progress.md`
(what is done, what is not, where you stopped) so a second coder session continues from it.

Nothing here touches the frozen directories (`src/engine/`, `src/parser/`, `src/audio_io/`, `src/prepost/`,
`src/cli/cli_main.cpp`). Everything is `src/gui/`, `docs/`, `android/` (if a Settings-row string lives there) and
the two device-config documents.

## 0. The spec is a theme file, and the renderer is the geometric reference

- THE PICTURE HE APPROVED: `tmp/palette/set_v/mock_V3_head_11rows.png`, rendered by
  `python3 tmp/palette/mocktool/render.py tmp/palette/set_v/mock_V3_head_11rows.json <out.png>` (run from the
  repo root; pycairo + numpy + ImageMagick `magick`; about half a second). Render it yourself into
  `tmp/brief_render.png` and look at it (the Read tool shows PNGs). The theme's keys are defined in
  `tmp/palette/mocktool/README.md` ("Theme schema", "The edge grammar", "The well", "Measured geometry") — read
  that README in full before touching source. Where this brief is silent on a geometry, `render.py`'s function
  for that element IS the spec (it draws the tablet's 2304x1440 screen at gui_scale 200 %: one logical px = two
  device px; the app authors in LOGICAL px and scales them by `gui_scale`, so divide the README's device numbers
  by two).
- A hex in the theme is a Display-P3 byte triple and IS the constant: since commit 3a6d2ba2 the tablet's window is
  a P3 layer and shows the app's bytes as-is; the laptop's untagged sRGB surface shows the same bytes a little
  differently and that is accepted (the laptop is debug only). So every colour below goes into render.h / icons.cpp
  as the literal bytes named, no conversion.
- THE PACKAGE RULE FOR EVERY COLOUR THIS BRIEF DOES NOT REPLACE: it takes its `two_pass` value from
  `tmp/palette/platform_map.json` (`constants.<kName>.two_pass`; the file maps every colour constant in render.h
  and the kIcon* of icons.cpp as of this morning). A constant missing from the map takes
  `tmp/palette/mocktool/common.py`'s `two_pass()` (the sRGB->P3 matrix twice in linear light, `derive.srgb_to_p3`).
  This is "the Y1b package": the mocks he judged were today's palette through two passes, and the knob is struck
  (section 4) with its output baked into the constants. Where an existing comment DERIVES a constant from another
  by a stated rule (mix_color over a ground, a Qt lighter(), a tint), re-run that rule on the NEW base values and
  keep the derivation; where the comment records a sampled literal, take the two_pass bytes. Red stays error-only
  (two_pass of kMarkerFlagFillRed etc.: (187,87,90) / (104,48,50) / (222,124,128) / (123,69,71)); the history
  diff's added/removed pairs likewise two_pass.
- Numbers below are LOGICAL px ("100 %"), scaled by gui_scale as today's rules do (lanes in authored px through
  `scaled_px`; a relief line is exactly ONE logical px = `scaled_px(1, 1)`; the head's rows authored).

## 1. THE PALETTE SCAFFOLD (render.h's palette block; architect 2026-10-02: "connect all of these design pieces under a few different keys, the Qt style of theming for Windows-type themes … as much derived as possible … accent and ink separate keys, tuned the same")

Restructure the palette block so that a handful of BASE ROLES are the only colour literals in the chrome, and
everything else is DERIVED at ONE site by named constexpr rules that reproduce the frozen bytes EXACTLY. No device
keys in this build (the device-key arc comes later and must then touch only that one site). Base roles:

| role | value | note |
|---|---|---|
| ground | `#303030` | content, menu row, icon row, the three lanes, bottom row, dropdowns, cards, dialogs, the folder overlay, the picker: ONE ground everywhere (kRedesignContentGround, kBackground, kRedesignRowGround, kRedesignPopupGround all collapse into it — see 3.6) |
| label | `#FCFCFC` | kRedesignLabel; disabled = kRedesignDisabledMix 0.322 of the label over its ground (unchanged rule) |
| accent | `#96BFDA` (150,191,218) | kRedesignAccent |
| ink | `#96BFDA` | kWaveformInk. ACCENT AND INK ARE TWO ROLES WITH ONE VALUE, declared separately, each with its own name, "tuned the same" |
| canvas | `#141618` (20,22,24) | kWaveformCanvas; also the modal text field's ground (kModalFieldGround derives = canvas) |
| flag | `#8A5EAC` (138,94,172) | kMarkerFlagFill |
| red | `#BB575A` (187,87,90) | kMarkerFlagFillRed, the one error colour |

DERIVED (state each rule and its ratio AT the rule, and `static_assert` every derived constant against its frozen
bytes so the derivation is checked by the compiler, not by eye):

- THE RELIEF SET from the ground, the scaffold's first rule (the Windows COLOR_3D* family): Hilight `#5E5E5E`,
  3DLight = the ground itself (so a thick inner line would be invisible — the reason relief is THIN), Shadow
  `#1E1E1E`, DkShadow `#0A0A0A`. Grey = nearbyint(ground_channel × ratio): 94 = 48×1.96, 30 = 48×0.625 (= 5/8
  exactly), 10 = 48×0.21. Use a constexpr `derived_grey(ground, ratio)` (or a rational pair) — your choice of
  spelling, one owner.
- from the ground likewise: selected fill `#4A4A4A` (×1.54), the toggled/pressed DOWN face `#3A3A3A` (×1.21), trim
  bar face `#404040` (×4/3), trim cap `#A8A8A8` (×3.5), ruler label `#C2C2C2` (×4.04), playhead head `#8B8B8B`
  (×2.9). (Row ground #373737 and popup #262626 were in the theme but RETIRE under 3.6 — do not create them.)
- outline (kWaveformForegroundOutline) = 50 % linear-light blend of ink over canvas = `derive.lin_mix(ink,
  canvas, 0.5)` in common.py's terms; compute it and freeze the bytes with the rule stated (it need not be
  constexpr-evaluable if the pow() is the obstacle — then a literal with the rule and a comment-level check).
- flag edge `#4D345F` (77,52,95), selected fill `#B37BE0` (179,123,224), selected edge `#63447B` (99,68,123), flag
  border `#151515` (the THEME's flag_border, the frozen byte; two_pass of the old kMarkerFlagBorder would be
  (19,21,22) — the theme wins), flag label black: derive from the flag role where a
  simple rule reproduces the bytes exactly (edge ≈ fill × 0.555 does: 76.6→77, 52.2→52, 95.5→95); where NO simple
  rule reproduces them (the selected pair likely), the value stays a literal in the flag's group with the nearest
  rule and its miss stated in one line. Phase-reset flags share the marker flags' values (kPhaseResetFlag* today
  equal kMarkerFlag*; keep that symmetry, both the two_pass bytes).
- disabled from label + ground (unchanged kRedesignDisabledMix); the popup's disabled label/hotkey constants:
  resolve them onto the ONE ground (3.6) through the same rule, or state why the dropdown keeps its own class.
- Write at the palette block which values are BASE, which DERIVED, and the rule for each. Rewrite the palette
  head: the kdenlive-sampled provenance paragraphs become one historical paragraph ("WHAT WAS HERE BEFORE"
  style, as the block already does for colors.conf); the authoritative text is now the Windows-95 one-line relief
  grammar and the scaffold. The lineage, one line: J4 classic neutral greys → K4 thin relief → L2/M1 well lines →
  N2 etched ticks → O2 trim lane → P6/T4/U4 ruler ground → R3 menu → S4 8 pt → V3 head (set Q, lane order and the
  Acid flat trim, ruled out).

## 2. THE RELIEF GRAMMAR (one helper family, one owner)

Implement ONE set of painting helpers (your names; `paint_relief_raised(cr, rect)`, `_sunken`, `_etched_vline`,
`_etched_hline` or similar) in the painter that owns chrome (paint_handler.cpp, or render.cpp if the trim lane
needs them there too — one definition, declared once). Rules, from the README's "edge grammar" at `relief:
"thin"`:

- RAISED = one logical line of Hilight along the top and the left, one of Shadow along the bottom and the right,
  THE DARK PAIR PAINTED LAST so it owns the top-right and bottom-left corner pixels.
- SUNKEN = the reverse (Shadow top/left, Hilight bottom/right, the light pair last).
- ETCHED separator = a Shadow line then a Hilight line immediately beside it (vertical: Shadow at the measured x,
  Hilight at x + 1; horizontal: Shadow then Hilight below).
- No gradients, no rounded corners, no alpha, anywhere in the chrome. `redesign_rounded_rect_path`,
  kMenuPillRadiusPx, kIconCornerRadiusPx, kPopupCornerRadiusPx, kScrubGrooveRadiusPx and every `cairo_arc` in the
  chrome painters retire (grep them: paint_handler.cpp 417-427, 1375, 1474, 1828, 2464, 2876, 3396-3418, 3561,
  3718, 6348, 6530, 6594, 6794, 7526; render.h 1759, 1847). The playhead head's rows stay integer rectangles as
  today.
- Lines are drawn as integer rectangles (cells), never antialiased strokes (CLAUDE.md's ROUNDING rule:
  `containing_pixel`).

## 3. THE FROZEN DESIGN, surface by surface

### 3.1 Buttons (icon row, bottom row, dialog buttons, the picker's and the overlay's button-rows)
- EVERY button is a RAISED SQUARE (its face = the ground, no lighter face), ALWAYS, enabled or disabled; a disabled
  button keeps the raised edge and dims only its glyph/label (the .322 mix).
- The toggled view button (the one down lamp of the view group) is SUNKEN on the down face `#3A3A3A` with its
  glyph shifted one logical px down and right. The three view icons keep their place.
- PRESSED (AppState::ChromePress's live press face) = sunken + the same one-px shift, on the ground (Windows).
  kRedesignClickMix retires (nothing is tinted by the accent any more).
- NO HOVER FACE ANYWHERE (architect ~11:25: "hover is awkward with pen and sometimes flickers, ok to drop it"):
  Windows 95 had none; the pointer cue (cursor) stays the only hover signal. Remove the hover PAINT on every
  surface: the roster buttons' accent hover (paint_handler.cpp ~1963, ~2511, hover_fade_color), the menu anchor's
  hover pill, the dropdown items' hover outline/fill (kRedesignHoverLightenMix retires), the folder overlay's
  kFolderRowHover / kFolderRowHoverOutline / kFolderRowHoverSelected, the scrub handle's hover outline, the
  modal dialog's hovered button face. KEEP every INPUT semantic that hover state drives (tooltips, the menu-bar's
  hover-switch between open menus, the hovered-item tracking that a press resolves against — ON SCREEN IS AS
  PAINTED still holds for the pressed/selected faces). The HoverFade machinery (render.h ~2738-2790, kHoverFadeMs,
  the 100 ms pill hold) exists only to drive paint: retire it where nothing but paint read it; if a timer or
  damage path depends on it, keep the minimum and say why at the site. The closed_questions lines on the pill's
  fade/hold (near line 676-677) get a superseding line (section 5).
- Separators between button groups: ETCHED vertical lines at today's x and height.
- The bottom row's border-top is GONE (no line between the well and the transport row). The old tab_line/line
  separator colours (kRedesignTabLine, kRedesignLine) retire if no reader remains; grep.

### 3.2 The clock cell and text fields
- The clock cell is a SUNKEN panel (geometry: render.py's `clock_panel` "sunken": x 8 device .. cell end + 8, the
  buttons' rows — i.e. 4 logical px of margin each side of the clock text's cell, the panel's rows = the buttons'
  rows).
- Modal text fields (the prompt, the flag editor's field, the settings editor's field): a SUNKEN panel on the
  canvas-coloured field ground (kModalFieldGround = canvas); the old Breeze-blue outline is GONE (his 09:40
  ruling): kModalFieldBorder retires. The invalid red flash recolours the field as today, in the two_pass red pair.
- KEYBOARD FOCUS in dialogs (planner's reading of the Windows grammar, the architect has not seen it — say so at
  the site with today's date): the ACTIVELY focused dialog button draws the Windows default-button frame, one
  logical DkShadow line around the outside of its raised box; the PASSIVELY focused one (Enter's target while a
  field has the keyboard) draws the same frame. kModalFocusFill / kModalFocusRing / kModalFocusLinePassive retire.
  A focused FIELD shows its caret as today and nothing else.

### 3.3 The menu row and the dropdowns
- Height unchanged. The anchor rectangle is unchanged (the whole lane tall, label width + 10 logical each side) but
  SQUARE: the pill's radius retires. COLD = nothing drawn (the ground). OPEN = a thin SUNKEN frame on the
  anchor's rectangle, no fill (R3). No hover frame. The accent is NOT used on the menu row. The legend untouched.
- DROPDOWN MENUS: ground = THE CONTENT GROUND `#303030` inside a thin RAISED frame (Windows 95 drew menus on the
  button face; kRedesignPopupGround #262626/#1C1F22 and kRedesignRowGround retire); the hovered/keyboard-selected
  row (keep the hovered-item tracking, it is what a press resolves against — and the row IS painted, this is a
  selection face, not a hover face) = a FLAT fill in the ACCENT with BLACK text and black hotkey (the Windows
  highlight; ASSUMED — the alternative is the selected fill #4A4A4A, he judges on the glass: state both at the
  site). Disabled rows: the .322 rule over the ground. Square corners. Item geometry otherwise unchanged.
- The tooltip: ground `#303030`, one logical DkShadow line border, square, label white (Windows' flat tooltip box).

### 3.4 The trim lane (render.cpp's render_trim_flags and its constants; input_trim.cpp's hit geometry unchanged)
- kTrimLaneHeightPx 10 → 11. The lane is a SUNKEN frame running the FULL WIDTH (Shadow line on its top row, Hilight
  line on its bottom row, Shadow at x 0's column and Hilight at the last column — read render.py's `trim.ground
  "sunken"` for the exact corners).
- The bar inside it is RAISED: face `#404040`, Hilight top/left, Shadow bottom/right, the dark pair last; its
  painted extent as today (a bound off screen runs one column past the edge — the one-column sliver at x 0 /
  x 2303 at 200 % is a known thing he said to wait on; do not fix it).
- The two end handles and the centre grip are SOLID RAISED SQUARES in `#A8A8A8`, 9 × 9 logical (the frame's inner
  height: 11 − 2 = 9), the grip's HOLLOW GONE (CONFIRMED today). kTrimMiddleInsetPx / kTrimMiddleClearPx and the
  cap/bevel/bottom-border colour constants (kTrimLaneEndcap, kTrimGroundBevelHi/Lo, kTrimBarBevelHi/Lo,
  kTrimCapBevelHi/Lo, kTrimLaneBottomBorder, kTrimLaneBar's old value) retire. kTrimBarScalePercent: retire if it
  only scaled the old bar, else re-derive.
- The grab tolerance (kTrimEndcapGrabPx) unchanged.

### 3.5 The ruler lane, the ticks and the playhead head
- THE LABELS at 8 pt: Roboto at 8 pt × gui_scale (a SECOND sans size through the one font owner, gui_font.h /
  text_shape; the clock's mono size is the precedent for a second size), colour the derived ruler label `#C2C2C2`.
- THE SEAT: 6 logical rows of ground between the lane's top and the digits' CAP TOP, and 10 logical rows between
  the BASELINE and the marker lane's top. So: kRulerLabelCapTopPx 4 → 6 (the same derived pad rule, measured on
  the 8 pt face: pad = 6·S − (ceil(ascent) − cap), clamped at 0), and the lane = baseline_px + scaled_px(10): a new
  constant for the 10 (name it; it OWNS the overlap rule below), kRulerHeadGroundPx RETIRED, the
  derived-from-the-face machinery kept (ruler_label_baseline_px / ruler_lane_h_px measure the 8 pt face once per
  gui_scale on the scratch surface exactly as a6f53163 does). The head's rows no longer enter the lane's height.
- THE PLAYHEAD HEAD: OPAQUE (kPlayheadHeadAlpha struck — "the classic Windows way"); 11 authored rows, the WIDEST
  row dropped: kPlayheadHeadHeightPx 12 → 11, kPlayheadHeadHalf = {8, 7, 6, 6, 5, 4, 4, 3, 2, 1, 1}; seated on the
  marker lane's top as today, so its top row OVERLAPS the digits' bottom ink row at the playhead's column —
  ALLOWED (his 10:50 ruling "a little overlap is fine"; this morning's one-row clearance ruling is superseded;
  update the closed_questions line that recorded it). Head colour the derived `#8B8B8B`; the held head keeps the
  stem's white. Re-derive every comment that quoted the head's 12 rows or 19 px width (render.h ~2470-2530, ~2870;
  paint_handler.cpp ~4108-4201; main.cpp's stack record).
- TICKS: kept where they are (major rise, minor tops, bottoms as today); EVERY tick, major and minor, is one
  Shadow line (`#1E1E1E`, kRulerTick = derived Shadow) with one Hilight line (`#5E5E5E`) immediately to its RIGHT
  over the same rows, drawn right after its tick (N2, the Sonic Foundry etching); labels, head, flags and stems
  paint over them as today.
- main.cpp's lane-stack record (the "At 500 / At 0" tablet and laptop stacks) is re-derived at both scales for the
  new trim lane (11) and ruler lane; state the 100 %, 200 % and 50 % lane heights at ruler_lane_h_px's comment as
  a6f53163 did (measure them: build and run nothing — compute from the face metrics via a tiny scratch program
  under tmp/ if you need the 8 pt ascent/cap, or read them off render.py's `verify_fonts`/`ruler_label_seat`
  arithmetic: 8 pt = 21.333 px at 200 % gives cap 16 device rows; at 100 % 10.667 px).

### 3.6 The marker lane and the well
- The marker lane: unchanged geometry (20 logical rows; flags as today with their 1-logical-px border — "way too
  many of them for more"); colours the two_pass package (section 1): fill (138,94,172), edge (77,52,95), selected
  fill (179,123,224), selected edge (99,68,123), border `#151515`, label black, stems in the flag's fill.
- THE WELL: top edge = one Hilight line then one DkShadow line (`#0A0A0A`, DERIVED — his answer 2: not black;
  kWaveformBorder's black retires); bottom edge = DkShadow then Hilight; the canvas between. The canvas height =
  the leftover of the stack (max_waveform_height 0 on both devices); the border is two logical rows each side as
  before, so the stack arithmetic keeps its shape. THE STEMS STOP AT THE CANVAS (they no longer run through the
  border rows) — the flags' and the playhead's.
- THE WAVEFORM PACKAGE: canvas (20,22,24); ink (150,191,218); outline = lin_mix(ink, canvas, 0.5); region lift as
  today over the new values (kWaveformRegionCanvas = region_lift(canvas), re-derived); the plate inks ride as
  words as today.

### 3.7 The bottom row, cards, the picker, the folder overlay, the render player, the h view
- Bottom row: ground, raised buttons, the sunken clock panel, no border-top.
- NOTIFICATION CARDS: ground `#303030` inside a thin RAISED frame, SQUARE, NO DROP SHADOW (~11:25): the shadow's
  rings, kNotificationShadowOffsetPx / SpreadPx / Alpha and notification_shadow_bound (notifications.h/.cpp,
  paint_handler.cpp ~3392, viewport.cpp ~178) retire; the card's damage bound is the card. One clause per card as
  today. With the head opaque and the shadow gone, NO PAINT-TIME ALPHA REMAINS ANYWHERE: set_palette_source_alpha
  retires (grep: no caller may remain) and the palette head says the palette composites nothing.
- THE PROJECT PICKER's rows and the FOLDER OVERLAY's rows: the panel = ground inside a thin raised frame; the
  selected row = accent fill with black text (the dropdown's rule); no hover face. The overlay's window-unfocused
  accent (kRedesignAccentInactive, kRedesignRowGroundUnfocused): re-derive from the new ground/accent by their
  stated rules where they still have a reader; retire where the hover removal leaves none.
- THE RENDER PLAYER'S SCRUB BAR (his words: "a trough like the trim with a larger dot for the current position
  indicator"; square CONFIRMED ~11:25 — a round dot would be the one circle in the chrome): the Breeze slider
  (render.h's kScrub* block, paint_handler.cpp ~6500-6600) is replaced by: a SUNKEN trough the trim lane's 11
  logical rows tall over the slider's track; a RAISED bar in the trim bar's face from the track's start to the
  position (the played extent) — the trough's ground (the content ground) beyond it; a LARGER RAISED SQUARE thumb
  in the trim cap colour centred on the position, 13 × 13 logical (overhanging the trough by one row above and
  below, the Windows slider thumb's overhang), the dark pair last. The handle's 20-px grab band and the mapping
  (render_player_scrub_x_of) are unchanged; the thumb's visual box is the new square. The scrub stops reading
  window_activated (its bar carries no accent to dim): kScrubPlayedInactive* retire; record it (section 5). All
  kScrub* colour constants retire; the metrics become the trough/bar/thumb numbers with the trim lane's as owner.
- THE h HISTORY VIEW and its diff lane: flags as the marker lane's with the two_pass added/removed pairs; any
  row/tab chrome follows the grammar (square, raised frames where a frame exists, no hover).
- THE SETTINGS EDITOR / FLAG EDITOR / PROMPT boxes: the dialog paints on the bottom row as today; its buttons are
  3.1's raised squares; its field 3.2's sunken panel; its focus 3.2's frame.
- ICONS: every kIcon* in icons.cpp takes its two_pass value (`platform_map.json` has kIconAccent? — if the map
  lacks the icons, compute with common.two_pass: kIconText #FCFCFC stays; kIconRecord (218,68,83)→(187,87,90);
  kIconPreviewOn and kIconLiftCross #D24D57 → two_pass of it; kIconAccent and kIconWav = THE INK (150,191,218)).
  The glyphs' paths are untouched.

## 4. THE KNOB STRIKE (bundled; architect 2026-10-02: the tuning rule's road to a settled constexpr)

`palette_passes` and `waveform_passes` go, their output baked into the constants (section 1's package rule):
- render.h/.cpp: kSrgbToDisplayP3Linear, set_palette_passes / palette_passes(), tuned_palette / tuned_waveform,
  apply_passes and the g_*_passes state go. KEEP THE THREE CHOKEPOINTS as plain helpers — set_palette_source and
  set_waveform_source (set_palette_source_alpha retires per 3.7) — every colour still reaches cairo through them
  and no site calls cairo_set_source_rgb(a) itself (re-grep and say so): that seam is where the later device-key
  arc plugs in. The flag cache's palette_passes() fingerprint term goes; WaveformPlateWords' fingerprint term goes
  if the words are constexpr again.
- device_config.{h,cpp}: the two keys out of kDeviceConfigKeys, format/parse/is_palette_passes, the struct fields,
  the writer arms; unknown-key fatal stands (a config still carrying them refuses to start — that is the
  intended road, both configs are edited by hand with the APK, by the planner).
- settings_editor.{h,cpp}, settings_io.{h,cpp}, input_handler.{h,cpp} (apply_palette_passes), main.cpp's startup
  install, app_state.h, paint_handler.h, viewport.h, waveform_cache.cpp, platform_wayland.cpp,
  platform_android.cpp: every mention (grep `passes` across src/gui) — the two Settings rows gone, the key counts
  in comments back by two.
- docs/INSTALL.md: the two key rows gone, the first-run line, the "seven lines, the seven keys" count, the
  template block at ~252, and the two keys ADDED to the retired-keys list (the migration section ~390-408 names
  the retired keys; follow its form).
- The P3 window (platform_android.cpp's adopt_window) STAYS.

## 5. Documents you update (the planner edits CLAUDE.md; you edit these)

- `docs/engineering/closed_questions.md`: one line each, in the section that owns the topic, present tense, with
  the date and the owner symbol, following the file's form: the knob struck (the line at ~179 becomes "struck,
  2026-10-02 … settled on the two_pass package"); the kdenlive-sampled chrome superseded by the Windows-95 thin
  relief grammar (the redesign section ~639); rounded corners and the menu pill's radius; hover faces (and the
  pill's fade/hold lines ~676-677 superseded); the cards' drop shadow (~412/665 mention it) and the head's alpha
  (the "two ruled alphas" phrasing at ~304/612/665 is now "the palette composites nothing"); the grip's hollow
  (~659); the trim lane at 10 rows; the ruler lane's head clearance (the 2026-10-02 morning line ~271 superseded
  by the overlap); the Breeze scrub slider and its unfocused groove; the Breeze focus ring; the bottom row's
  border-top; set Q (lane order / Acid trim) RULED OUT 2026-10-02 09:25; hover on the menu title (Windows 98 hot
  tracking) not adopted.
- Every comment you touch: current behaviour and rationale only, at the owner, with the ruling's date (2026-10-02);
  no mock-set letters as rules (the lineage line in the palette head is the one place they appear); no
  review-round numbers. Grep before claiming "every site".

## 6. Self-check before you report

1. `cmake --build build -j$(nproc)` clean with `-Wall -Wextra`, no new warnings. Also configure+build the CLI once
   (`cmake -B build -S . -DWARPTEMPO_BUILD_CLI=ON` is already configured; `cmake --build build --target
   warptempo_cli` if the target exists) — it shares render.h? (it does not link the GUI; just make sure the tree
   builds whole.)
2. `grep -rn 'passes' src/gui docs | grep -v 'bypass\|passes the\|passes it\|passes through\|passes over'` — nothing
   of the knob remains. `grep -rn 'cairo_set_source_rgb' src/gui` — only the chokepoints' bodies.
   `grep -rn 'cairo_arc\|rounded_rect\|Radius' src/gui` — none in the chrome. `grep -rn 'alpha' src/gui/render.h
   src/gui/paint_handler.cpp` — no paint-time compositing alpha left.
3. Render the theme (`tmp/brief_render.png`) and hold your build's painters against it lane by lane in your
   head: the planner takes the tablet screencap after the APK and runs `tmp/palette/mocktool/compare.py`; expect
   every chrome lane byte-exact except the waveform (the app's own peaks) and the 8 pt text (the tool seats it by
   the same rule — if your seat differs, say by how much and why).
4. Report: the list of files touched, every constant retired and every one introduced (base vs derived), the
   measured lane heights at 100 / 200 / 50 %, the planner-assumed items (dropdown highlight colour, dialog focus
   frame, thumb size, tooltip border) so they go to the architect, and anything you could not finish.
