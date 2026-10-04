# tools/theme_catalog — the imported themes, their provenance and their crops

THE APP CARRIES IMPORTED THEMES ONLY, NO DERIVATION (architect 2026-10-03: "no derived, imported only; derivation
stays in git history"; "I don't want to be designing my own theme"). This tool turns the era's own theme files into
`docs/themes/catalog.json` — every colour a recorded byte with its provenance — and renders the app in each one
(`docs/themes/crops/`, listed in `docs/themes/CATALOG.md`), which is how a theme is chosen. THE ONE FAMILY THAT
IMPORTS NOTHING is the program's own, `warptempo`, and it derives nothing either: `warptempo-2026-10-03` records the
app's look of that morning off render.h's constants, and `warptempo` — THE APP'S DEFAULT THEME — is CHOSEN, NOT
IMPORTED (architect 2026-10-03, the colour loop's mock sets: BA03's chrome, Windows 95 Standard darkened in proportion
to a ground of relative luminance 0.010 under white text, and BM02's selection grey), its bytes his ruling as recorded
(build.py `chosen_entry`), because no desktop of the era recorded the look he picked; and each
`warptempo-preset-<n>` ("Warptempo Preset <n>") is THE ARCHITECT'S PRESET <n> ON THE COLOUR PICKER, chosen, not
imported either (architect 2026-10-04; build.py `preset_entries`, from `tools/palette/picker/presets/presets.json`):
the preset's chrome ground through the picker's chrome rule (`tools/palette/colour.py` `windows95_chrome`) applied
here, at generation, THE LABEL AND THE FIELD TEXT the preset's `label` (the picker's Label element, architect
2026-10-04: text is a pickable element, never a black / white switch or a contrast rule; white when the preset records
none, as every preset saved before the Label round was painted), and THE SELECTED PAIR the preset's `selected_fill`
and `selected_text` (the picker's Selection and Selected Text, the open-flag round, architect 2026-10-04; #666666 under
white when it records none, the picker's starting colours), so the entry's light level is exactly the chrome the
picker painted; its dark level is levels.py's dark rule's (the selected fill darkened in proportion, its label and
selected text white); the preset's other elements are program keys, whose device-config lines `preset_keys.py` prints
(the label and the selected pair are theme roles, so it prints nothing for them). A
standalone utility: no
link path from any product target, no CMake, Python 3 + numpy (and `tools/palette/` for the crops).

```
python3 tools/theme_catalog/fetch.py [--refresh]   # the pinned sources -> tmp/theme_sources/ (git-ignored)
python3 tools/theme_catalog/build.py               # -> docs/themes/catalog.json (runs the checks, prints the families)
python3 tools/theme_catalog/build.py --presets-only  # no sources: the preset entries anew, every other entry carried
python3 tools/theme_catalog/preset_keys.py [n]     # prints each preset's device-config lines (writes nothing)
python3 tools/theme_catalog/gen_theme_table.py     # -> src/gui/theme_table.h, the app's generated table (committed)
python3 tools/theme_catalog/levels.py --verify     # the vectorized ground search against its exhaustive definition
python3 tools/theme_catalog/crops.py [key ...]     # -> docs/themes/crops/<key>.png, docs/themes/CATALOG.md
python3 tools/theme_catalog/crops.py --md          # renders nothing: CATALOG.md from catalog.json, stale crops deleted
```

## The app's table and the two levels (architect 2026-10-03)

The app carries the catalog as ONE GENERATED, COMMITTED HEADER, `src/gui/theme_table.h` (`gen_theme_table.py`; its
head says "do not edit"): every entry at TWO LEVELS, LIGHT / DARK, eleven roles a level, so the app does no
colour arithmetic. A catalog change is `build.py`, then `gen_theme_table.py` and `crops.py`, the outputs committed
together; the output is byte-stable (the catalog's order, fixed formatting, no timestamp). THE LEVEL ARITHMETIC is one
module, `levels.py`, read by the generator and by `crops.py` (its head states the rule): LIGHT the entry as
recorded; DARK the ground's HLS hue and saturation at relative luminance 0.035, all four relief lines'
HLS lightness scaled by L(new face) / L(base face) (black stays black, a 3DLight equal to the base face becomes the
new face), the label white, the emboss's light copy Windows' own dialog-rule Hilight of the new face
(`toolkit_rules.windows_dialog`); the selected pair the entry's (a CDE entry's its `title_active` with colour set 1's
Motif foreground) and the field pair the entry's — as recorded at LIGHT, and at DARK each of the two grounds
DARKENED IN PROPORTION with the face
(architect 2026-10-03, mock sets AS and AT): its recorded luminance x the level's target / L(the recorded ground), its
hue and HSV saturation kept (`pair_ground_at`), the text on both the level's white. The info pair is imported (the
role table below) and NOT CARRIED (architect 2026-10-03, mock set AY): the tooltip and the cards are the level's
ground under its label on both levels, render.h's palette block (THE INFO FACE). The searches are exhaustive
by definition (`ground_at_exhaustive`, the lightness at every 1 / 100000 step; `pair_ground_at_exhaustive`, the HSV
value likewise); `ground_at` and `pair_ground_at` are the same searches vectorized with numpy, verified identical on
every case the table reads (`levels.py --verify`). No level stands between the two (architect 2026-10-03: a middle
level's proportional field, card and selection fall into a grey zone, and fixing that would need another rule). The app reads the row the device config names (`theme`, `theme_level`; render.h's palette
block maps the roles onto the painters).

## The contract

- A THEME IS DRAWN AS ITS OWN DESKTOP DREW IT. Where the source records the bevels (a Windows scheme records all four
  3D colours) the bytes are the record. Where it records only base colours and its toolkit computed the rest at run
  time (a KDE 3 `.kcsrc`, a CDE `.dp`), build.py runs THAT TOOLKIT'S OWN RULE once, at import
  (`toolkit_rules.py`: Motif's `CalculateColorsRGB`, KDE 3's `createApplicationPalette` over Qt 3's integer HSV,
  each ported from the pinned source cited at its function) and the catalog stores the resulting bytes, the rule
  named in the entry's provenance and described once in the catalog's `rules`. That is part of the import, not a
  derivation of ours.
- THE CHROME IS THE THEME'S; THE WAVEFORM PANE, THE FLAGS AND THE PLAYHEAD ARE THE PROGRAM'S OWN ELEMENTS (architect
  2026-10-03), their colours the app's device keys. The flags are the FLAT Acid flag, outlined in the theme's DkShadow
  and shaded by nothing. Each entry still names the rule its family's desktop shaded a 3D face with (`flag_rule`:
  Windows' Appearance dialog, `windows_dialog`, over shlwapi's 240-scale integer HLS as Wine implements it; KDE 3's;
  Motif's — `toolkit_rules.flag_bevel`), KEPT for tools/palette's "bevelled" flag style, the record of the design the
  flat flag replaced; the app and the crops read it nowhere. `windows_dialog` is also the levels' emboss copy rule
  (`levels.py`). These are the only rules in the tool.
- EVERY SOURCE IS PINNED (`sources.py`): a repository at a commit, or a fixed local image, and every entry's
  provenance names the project, the file, the URL and the commit (or the image). Fetched files live in
  `tmp/theme_sources/` and are never committed; only the bytes and their provenance are.
- NO BACKSTOPS: the inputs are pinned third-party files; a malformed one is a one-line hard fail naming it.
- A THEME OWNS THE CHROME. The catalog records EVERY raw value its source has, under the source's own key names
  (`raw`), even those no role reads today, so the waveform pane can later inherit from a theme by choice.
- A role a source has no word for is ABSENT and the app's own value applies; nothing is guessed.

## An entry

`key` (unique ASCII, lowercase words joined by hyphens behind the family prefix — `windows-`, `plus-`, `kde3-`,
`cde-`, `warptempo-` — what is typed in Settings), `name` (the source's display name verbatim; a Windows 98
theme's file name, a CDE palette's file stem), `family`, `imitates` (optional: a KDE scheme whose name says it imitates
another desktop), `provenance` (`sources`: one record per source file; `rule`: the toolkit rule's id, its parameters
and every value it computed), `raw`, `roles`, `flag_rule` (the rule the bevelled flag style takes — kept, read by
tools/palette's "bevelled" style alone since the flat flag, 2026-10-03: `{"id":
"windows-dialog"}` for the families `windows`, `windows-plus` and `warptempo` — the architect: "take Windows' rule" —,
`{"id": "kde3", "contrast": c}` at the scheme's own contrast, `{"id": "motif"}` for `cde`; described in the catalog's
`rules`), `display_tier` (below), `notes` (every disagreement between sources, every relabelling).

## The display tier (`display_tier`; architect 2026-10-03, late)

Each entry is tagged by the smallest period colour set holding every colour its roles use (build.py `display_tier`;
the sets are also catalog.json's `display_tiers`, and each entry's CATALOG.md block names its tier):

| tier | the set |
|---|---|
| `vga` | the 16 VGA colours: 000000 800000 008000 808000 000080 800080 008080 C0C0C0 808080 FF0000 00FF00 FFFF00 0000FF FF00FF 00FFFF FFFFFF |
| `windows-20` | those and Windows' four static extras C0DCC0 A6CAF0 FFFBF0 A0A0A4 (reserved in a 256-colour display's system palette, so always solid there) |
| `high-colour` | anything else |

Today: `vga` 3 (Windows Storm, Teal, and Red, White, and Blue; build.py asserts the list), `windows-20` 0,
`high-colour` 95. Windows Standard misses `vga` only by its tooltip ground FFFFE1, Windows 95 Standard by that and its
3DLight DFDFDF (asserted for Windows Standard).

## Families and sources

| family | source | what |
|---|---|---|
| `windows` | ReactOS `boot/bootdata/hivedef.inf` ("New Schemes", COLOR_* indices, 0x00BBGGRR, English names from the first [Strings] block), corroborated by the Windows XP classic schemes saved as .theme files (zkedem/windows10-classic-themes; 1j01/98 `desktop/Themes/classicthemes8`) and Windows 98's `Windows Default.theme`; Windows 95 Standard hand-recorded | a hivedef.inf scheme is imported only when a second, independent source records it with equal bytes on every role key (the rest: Not imported, below); Desert and Spruce (absent from ReactOS) come from the two XP records; ReactOS's "ReactOS Standard" and "ReactOS Classic" are Windows Classic and Windows Standard under ReactOS names and are folded into those entries |
| `windows-plus` | 1j01/98 `desktop/Themes/Windows Official/*.theme`, `[Control Panel\Colors]` | the Windows 98 / Plus! desktop themes; `Windows Default` corroborates Windows Standard, the byte-identical `Copy of Dangerous Creatures` is not a second entry |
| `kde3` | TDE tdebase `kcontrol/krdb/kcs/*.kcsrc` (49), less the 24 not imported: KDE 3.5's three usability schemes and the 21 Trinity added later — KDE 3.5's own 25 (architect 2026-10-03, late: the KDE catalog keeps only what KDE 3.5 shipped; build.py asserts 25). The Q4OS 6.9 TDE image was a second source until 2026-10-03; its six schemes were all not imported, and the source was dropped | relief by KDE 3's rule at the scheme's own `contrast=` (default 7) |
| `cde` | cdesktopenv `cde/programs/palettes/*.dp` | the eight colour sets of each palette (16-bit, recorded as each channel's top byte, the verbatim lines in the provenance), and Motif's foreground, select colour and two shadows for every set; the four monochrome palettes (Black, White, BlackWhite, WhiteBlack: X colour names, refused by dtsession on a colour display) are reported, not imported |
| `warptempo` | `src/gui/render.h` at da0b1051 (git show); the architect's ruling of 2026-10-03 | `warptempo-2026-10-03`, the app's own look on the morning of 2026-10-03, so it stays selectable as bytes; `warptempo` (display name `warptempo`, all lowercase, his spelling), the app's default, CHOSEN, NOT IMPORTED: its roles are the ruled bytes (build.py `CHOSEN_ROLES`), its info pair absent (the app carries none), its dark level whatever levels.py's dark rule makes of them, untuned; `warptempo-preset-<n>` (display name `Warptempo Preset <n>`), one per preset the colour picker saved (`tools/palette/picker/presets/presets.json`, architect 2026-10-04), CHOSEN, NOT IMPORTED: the preset's chrome ground through the picker's chrome rule, the label and the field text the preset's Label (white when it records none), the selected pair the preset's Selection and Selected Text (#666666 / #FFFFFF when it records none), its dark level levels.py's dark rule's |

Every family's line in the build: windows 19, windows-plus 16, kde3 25, cde 36, warptempo 4 (with the two presets of
2026-10-04) — 100 entries. A new copy of presets.json adds its new presets on the next run (a preset's number is never
reused: the picker only appends). Where the pinned sources cannot be fetched (the cloud: the Trinity mirror lies
outside its egress), `build.py --presets-only` writes the preset entries and the header anew and carries every other
entry and the not-imported record from the committed catalog.json byte for byte, after recomputing the two entries
that need no source and asserting they equal the carried ones; both roads write through one function, so the full
run writes the same bytes.

## Not imported (architect 2026-10-03, late; catalog.json's `not_imported`, asserted by build.py)

| what | reason |
|---|---|
| ReactOS's own schemes (7): Green Olive, Sand, Sky, High Contrast 1, High Contrast 2, High Contrast Black, High Contrast White | no independent source records them as Windows'; Sand and Green Olive are role-identical to Windows Desert and Spruce under other names (build.py asserts it); the four High Contrast schemes are usability schemes |
| KDE 3 High Contrast Black Text, High Contrast White Text, High Contrast Yellow on Blue | usability schemes |
| `cde-broica` | role-identical to `cde-default` (byte-identical on every raw value too) |
| KDE Human, Last.fm, Lizard, Platinum, Sienna, WedgieWeb (6) | not shipped by KDE 3.5: Trinity added them to tdebase in commit 688aa0fc28d3de8665b7b151eba65fe49e02187f (2023-10-18, "Add six new color schemes taken from https://www.opendesktop.org.") |
| KDE Different, Jewels - Amethyst, - Aquamarine, - Carbon, - Citrin, - Emerald, - Ruby, - Sapphire, - Topaz, Lila, Pinkie, Seasons - Autumn, - Spring, - Summer, - Winter (15) | not shipped by KDE 3.5: Trinity added them to tdebase in commit 69ac490a9e43b46efbc7fbf3ce32e96365f5805d (2025-01-24, "Add 15 color schemes taken from https://www.opendesktop.org.") |
| CDE Black, White, BlackWhite, WhiteBlack (.dp) | X colour names for monochrome displays, refused by dtsession on a colour display |

## The role mapping (`roles.py`, the one table)

| role | windows, windows-plus | kde3 | cde | warptempo |
|---|---|---|---|---|
| `ground` | `ButtonFace` | `background` | `set5` | `kRedesignContentGround` |
| `label` | `ButtonText` | `foreground` | `motif:set5.fg` | `kRedesignLabel` |
| `bevel_hilight` | `ButtonHilight` | `kde3:light` | `motif:set5.ts` | `kReliefHilight` |
| `bevel_light` | `ButtonLight` | `kde3:midlight` | `motif:set5.ts` | `kRelief3DLight` |
| `bevel_shadow` | `ButtonShadow` | `kde3:dark` | `motif:set5.bs` | `kReliefShadow` |
| `bevel_dkshadow` | `ButtonDkShadow` | `kde3:shadow` | `motif:set5.bs` | `kReliefDkShadow` |
| `selected_fill` | `Hilight` | `selectBackground` | absent | `kRedesignAccent` |
| `selected_text` | `HilightText` | `selectForeground` | absent | `kRedesignHighlightLabel` |
| `info_ground` | `InfoWindow` | absent | absent | `kInfoGround` |
| `info_text` | `InfoText` | absent | absent | `kInfoText` |
| `field_ground` | `Window` | `windowBackground` | `set4` | `kModalFieldGround` |
| `field_text` | `WindowText` | `windowForeground` | `motif:set4.fg` | `kRedesignLabel` |
| `disabled_text` | `GrayText` | `kde3:disabled_foreground` | absent | absent |
| `title_active` | `ActiveTitle` | `activeBackground` | `set1` | absent |
| `title_inactive` | `InactiveTitle` | `inactiveBackground` | `set2` | absent |

The `warptempo` column is the recorded entry's; the chosen `warptempo` entry and the preset entries map nothing,
their roles being the ruled or the picker's bytes under the role names themselves. A `kde3:` or `motif:` value is computed by that toolkit's rule at import (`provenance.rule.computed`); every other
value is a raw key. KDE 3's relief comes from the scheme's `background` (its `buttonBackground` is recorded raw: the
app has one ground). CDE's colour sets (Motif `ColorObj.c`'s resource defaults, dtsession `SrvPalette.c`, dtwm
`WmResource.c` / `Dtwm.defs`): 1 the active window frame, 2 the inactive frame, 3 and 7 workspace backdrops, 4 text
and lists, 5 THE PRIMARY (an application's background; dtsession's high-colour `*background`), 6 the secondary (menus,
dialogs), 8 the front panel; Motif paints two shadows, so its quartet is (ts, ts, bs, bs); it has no selection
highlight (text selection is inverse video), no tooltip pair and no disabled colour (insensitive text is stippled).

THE APP-SPECIFIC ROLES (architect 2026-10-03), drawn from the catalog roles above rather than stored: the ruler label
and the trim lane's arrow glyph <- `label`; the ruler ticks <- `bevel_shadow`; the flag OUTLINE <- `bevel_dkshadow`
(round the flag, keeping overlapping flags apart; the stem crosses its bottom line); the playhead head's outline <-
`label`; THE DISABLED EMBOSS's light copy <- `bevel_hilight` on the light level (`levels.py`). THE PROGRAM'S OWN
COLOURS, not catalog roles, the app's open device keys (src/gui/device_config.h): the waveform ink, canvas and lit
outline (#808080 / #000000 / #5C5C5C); the flag's face and its label, a RECORDED colour beside the face as Windows 95
recorded a text colour beside every face (no luminance rule anywhere: that is WCAG 2.0's contrast math, not
Windows'): white on the slate violet #666699; the selected flag's brighter face #CCCCFF and the one selected label,
black; the invalid face and its label, #993333 and white, its selected face #FF6666 (Windows' error-icon pair #FF0000
/ #FFFFFF was struck, architect 2026-10-03); the playhead's head and stem (#8B8B8B / #FCFCFC) — the defaults of the
colour loop (architect 2026-10-03).

## The checks (build.py, before the write)

Rainy Day's raw and roles are 8399B1 / C1CCD9 / 8399B1 / 4F657D / 000000 (face, Hilight, Light, Shadow, DkShadow);
Windows 95 Standard's quartet is FFFFFF / DFDFDF / 808080 / 000000 on C0C0C0; KDE 3 at contrast 7 on 303030 gives
3D3D3D / 343434 / 111111 / 000000; Motif on 303030 gives 989898 / 6E6E6E (the dark branch) and on 41525C A6AEB3 /
1E252A (both from an 8-bit colour as X parses `#rrggbb`, each byte replicated; `toolkit_rules.py` asserts the same at
import); Windows' dialog rule gives D4D0C8 -> Hilight EAE8E3 and Rainy Day 8399B1 -> C1CCD9 / 4F657D; every entry's
`flag_rule` is its family's and runs; kde3 has 25 entries; the not-imported lists are exactly the schemes found (a re-pinned source cannot
change them silently) and each duplicate is role-identical to its twin; the Warptempo 2026-10-03 entry equals
render.h's constants and the chosen `warptempo` entry the ruled bytes; the `vga` entries are exactly Windows Storm, Teal and Red, White, and Blue, none is `windows-20`;
every key is unique and ASCII. The last lines print each family (entries, corroborated, sources), the display
tiers' counts and the schemes not imported.

## The crops

`crops.py` writes a theme per entry over `tools/palette/themes/tablet.json`, THE TABLET GEOMETRY (architect
2026-10-03, step 13: the app at the tablet's gui_scale 275, every length derived from the app's own constants,
`tools/palette/tablet.py`; scratch: `tmp/theme_catalog/themes/`), renders it in scene 1002's state
(`tmp/theme_catalog/full/`, never committed) and CROPS, never scales: the top strip's left half and its right half
over the same rows (the menu down to 7 canvas rows under the well's top lines, rows 0..296, so all four flag states
show), and the well's two bottom lines over the bottom row (rows 1343..1440), its status panel and state line (columns
0..656) beside its last two button groups (from x 1808, the middle of the group space before the arrows); the windows
follow the geometry (`crops.regions()`, from `tablet.SCENE`). Stacked 1152 x 697 px with a 4-row FULLY TRANSPARENT gap
between them (alpha 0 there, 255 everywhere else, so no join reads as chrome), written as RGBA (indexed with tRNS
when a crop has at most 256 colours; none has: the antialiased text exceeds it), each with the Display-P3 iCCP chunk.
On each: THE APP'S FINAL DESIGN AT THE LIGHT LEVEL (architect 2026-10-03; `levels.level_roles`, the generated table's
own row) with the role mapping above, every option the app's (the tablet geometry fixes them, render.py
`TABLET_FIXED`): the well's two-line PLAIN SUNKEN edge, top and bottom only, full width (`bevel_shadow` then
`bevel_dkshadow` inward on top, `bevel_light` inward then `bevel_hilight` outward at the bottom); the flat flags left
to right EDITING (the in-place editor, the selected flag opened for edit: its black frame on the selected face, its
text in the selected pair), SELECTED (the app's BRIGHTER FACE, the face and the stem the selected key under the
selected label, the outline still DkShadow; architect 2026-10-03, the colour loop, the white outline retired),
INVALID, DISABLED (the ground, the label embossed, no stem) and unselected; the playhead head outlined in the label; the disabled menu word
and buttons engraved over `emboss_hilight`; the trim arrow in the label (`trim_arrow`). The waveform, the flags' face
and label, the invalid pair and the playhead are the program's colours (above), the icons' fixed inks icons.cpp's.
100 crops, 5.58 MB. Its head states which roles come from the entry and which are the program's.
