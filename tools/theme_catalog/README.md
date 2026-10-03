# tools/theme_catalog — the imported themes, their provenance and their crops

THE APP CARRIES IMPORTED THEMES ONLY, NO DERIVATION (architect 2026-10-03: "no derived, imported only; derivation
stays in git history"; "I don't want to be designing my own theme"). This tool turns the era's own theme files into
`docs/themes/catalog.json` — every colour a recorded byte with its provenance — and renders the app in each one
(`docs/themes/crops/`, listed in `docs/themes/CATALOG.md`), which is how a theme is chosen. A standalone utility: no
link path from any product target, no CMake, Python 3 + numpy (and `tools/palette/` for the crops).

```
python3 tools/theme_catalog/fetch.py [--refresh]   # the pinned sources -> tmp/theme_sources/ (git-ignored)
python3 tools/theme_catalog/build.py               # -> docs/themes/catalog.json (runs the checks, prints the families)
python3 tools/theme_catalog/crops.py [key ...]     # -> docs/themes/crops/<key>.png, docs/themes/CATALOG.md
```

## The contract

- A THEME IS DRAWN AS ITS OWN DESKTOP DREW IT. Where the source records the bevels (a Windows scheme records all four
  3D colours) the bytes are the record. Where it records only base colours and its toolkit computed the rest at run
  time (a KDE 3 `.kcsrc`, a CDE `.dp`), build.py runs THAT TOOLKIT'S OWN RULE once, at import
  (`toolkit_rules.py`: Motif's `CalculateColorsRGB`, KDE 3's `createApplicationPalette` over Qt 3's integer HSV,
  each ported from the pinned source cited at its function) and the catalog stores the resulting bytes, the rule
  named in the entry's provenance and described once in the catalog's `rules`. That is part of the import, not a
  derivation of ours.
- THE CHROME IS THE THEME'S; THE WAVEFORM PANE AND THE FLAGS ARE THE PROGRAM'S OWN ELEMENTS (architect 2026-10-03,
  late), drawn the way a custom control in a Windows-95-era program would be: their base colours are the program's,
  their SHADING the theme's. So each entry names the rule its family's desktop shaded a 3D face with (`flag_rule`),
  and the flags' one-line bevel is that rule run on the flag's face colour (`toolkit_rules.flag_bevel`): Windows'
  Appearance dialog (`windows_dialog`, over shlwapi's 240-scale integer HLS as Wine implements it), KDE 3's, Motif's.
  These are the only rules in the tool.
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
and every value it computed), `raw`, `roles`, `flag_rule` (the rule the flags' bevel takes: `{"id":
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
`high-colour` 120. Windows Standard misses `vga` only by its tooltip ground FFFFE1, Windows 95 Standard by that and its
3DLight DFDFDF (asserted for Windows Standard).

## Families and sources

| family | source | what |
|---|---|---|
| `windows` | ReactOS `boot/bootdata/hivedef.inf` ("New Schemes", COLOR_* indices, 0x00BBGGRR, English names from the first [Strings] block), corroborated by the Windows XP classic schemes saved as .theme files (zkedem/windows10-classic-themes; 1j01/98 `desktop/Themes/classicthemes8`) and Windows 98's `Windows Default.theme`; Windows 95 Standard hand-recorded | a hivedef.inf scheme is imported only when a second, independent source records it with equal bytes on every role key (the rest: Not imported, below); Desert and Spruce (absent from ReactOS) come from the two XP records; ReactOS's "ReactOS Standard" and "ReactOS Classic" are Windows Classic and Windows Standard under ReactOS names and are folded into those entries |
| `windows-plus` | 1j01/98 `desktop/Themes/Windows Official/*.theme`, `[Control Panel\Colors]` | the Windows 98 / Plus! desktop themes; `Windows Default` corroborates Windows Standard, the byte-identical `Copy of Dangerous Creatures` is not a second entry |
| `kde3` | TDE tdebase `kcontrol/krdb/kcs/*.kcsrc` (49) + the six the Q4OS 6.9 TDE image adds, less the four not imported (51) | relief by KDE 3's rule at the scheme's own `contrast=` (default 7) |
| `cde` | cdesktopenv `cde/programs/palettes/*.dp` | the eight colour sets of each palette (16-bit, recorded as each channel's top byte, the verbatim lines in the provenance), and Motif's foreground, select colour and two shadows for every set; the four monochrome palettes (Black, White, BlackWhite, WhiteBlack: X colour names, refused by dtsession on a colour display) are reported, not imported |
| `warptempo` | `src/gui/render.h` at da0b1051 (git show) | the app's own look on 2026-10-03, so it stays selectable as bytes |

Every family's line in the build: windows 19, windows-plus 16, kde3 51, cde 36, warptempo 1 — 123 entries.

## Not imported (architect 2026-10-03, late; catalog.json's `not_imported`, asserted by build.py)

| what | reason |
|---|---|
| ReactOS's own schemes (7): Green Olive, Sand, Sky, High Contrast 1, High Contrast 2, High Contrast Black, High Contrast White | no independent source records them as Windows'; Sand and Green Olive are role-identical to Windows Desert and Spruce under other names (build.py asserts it); the four High Contrast schemes are usability schemes |
| KDE 3 High Contrast Black Text, High Contrast White Text, High Contrast Yellow on Blue | usability schemes |
| `cde-broica` | role-identical to `cde-default` (byte-identical on every raw value too) |
| `kde3-q4os-default` | role-identical to `kde3-keramik-white` (it differs only on the window-frame keys frame, handle, inactiveFrame, inactiveHandle, which no role reads) |
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

A `kde3:` or `motif:` value is computed by that toolkit's rule at import (`provenance.rule.computed`); every other
value is a raw key. KDE 3's relief comes from the scheme's `background` (its `buttonBackground` is recorded raw: the
app has one ground). CDE's colour sets (Motif `ColorObj.c`'s resource defaults, dtsession `SrvPalette.c`, dtwm
`WmResource.c` / `Dtwm.defs`): 1 the active window frame, 2 the inactive frame, 3 and 7 workspace backdrops, 4 text
and lists, 5 THE PRIMARY (an application's background; dtsession's high-colour `*background`), 6 the secondary (menus,
dialogs), 8 the front panel; Motif paints two shadows, so its quartet is (ts, ts, bs, bs); it has no selection
highlight (text selection is inverse video), no tooltip pair and no disabled colour (insensitive text is stippled).

THE APP-SPECIFIC ROLES (architect 2026-10-03, late), drawn from the catalog roles above rather than stored: the ruler
label <- `label`; the ruler ticks <- `bevel_shadow`; the flag OUTLINE <- `bevel_dkshadow` (round the flag, keeping
overlapping flags apart; the stem is not the outline). THE PROGRAM'S OWN COLOURS, not catalog roles: the waveform ink
and canvas; the flag's face and its label, a RECORDED colour beside the face as Windows 95 recorded a text colour
beside every face (the luminance rule is WCAG 2.0's contrast math, not Windows', and no program element reads it):
black on the app's purple, and the invalid flag as Windows' error-icon pair, the face #FF0000 and the label #FFFFFF
(the Stop icon's white X); the playhead's head and stem (the app's #8B8B8B / #FCFCFC). Only the flags' shading is the
theme's, the entry's `flag_rule`.

## The checks (build.py, before the write)

Rainy Day's raw and roles are 8399B1 / C1CCD9 / 8399B1 / 4F657D / 000000 (face, Hilight, Light, Shadow, DkShadow);
Windows 95 Standard's quartet is FFFFFF / DFDFDF / 808080 / 000000 on C0C0C0; KDE 3 at contrast 7 on 303030 gives
3D3D3D / 343434 / 111111 / 000000; Motif on 303030 gives 989898 / 6E6E6E (the dark branch) and on 41525C A6AEB3 /
1E252A (both from an 8-bit colour as X parses `#rrggbb`, each byte replicated; `toolkit_rules.py` asserts the same at
import); Windows' dialog rule gives D4D0C8 -> Hilight EAE8E3 and Rainy Day 8399B1 -> C1CCD9 / 4F657D; every entry's
`flag_rule` is its family's and runs; the not-imported lists are exactly the schemes found (a re-pinned source cannot
change them silently) and each duplicate is role-identical to its twin; the Warptempo entry equals render.h's
constants; the `vga` entries are exactly Windows Storm, Teal and Red, White, and Blue, none is `windows-20`;
every key is unique and ASCII. The last lines print each family (entries, corroborated, sources), the display
tiers' counts and the schemes not imported.

## The crops

`crops.py` writes a theme per entry over `tools/palette/themes/ad2.json`'s geometry (scratch:
`tmp/theme_catalog/themes/`), renders it on the default scene (`tmp/theme_catalog/full/`, never committed) and CROPS,
never scales: the top strip's left half and its right half over the same rows (the menu down to a few canvas rows,
so all four flag states show), and the bottom row's status panel beside its last button groups, stacked 1152 x 702
px with a 4-row FULLY TRANSPARENT gap between them (alpha 0 there, 255 everywhere else, so no join reads as chrome),
written as RGBA (indexed with tRNS when a crop has at most 256 colours; none has: the antialiased text exceeds it),
each with the Display-P3 iCCP chunk. On each: the role mapping above, the well with the app's two-line PLAIN SUNKEN
edge, top and bottom only, full width (architect 2026-10-03, late: `bevel_shadow` then `bevel_dkshadow` inward on top,
`bevel_light` inward then `bevel_hilight` outward at the bottom; crops.py `WELL`), and the Sonic Foundry flags shaded by
the entry's `flag_rule`, the stem leaving the box's first face column through a gap in its bottom lines, the scene's
flags left to right unselected, SELECTED, INVALID, DISABLED and unselected (render.py's `flags.style` "bevelled"); the waveform, the flags' face and label, the invalid flag's pair and the
playhead are the program's colours (above). The icons are the app's, unchanged. 123 crops, 6.66 MB. Its head states which roles come from the entry and which are the program's.
