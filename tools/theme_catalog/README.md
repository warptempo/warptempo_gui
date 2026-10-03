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
  derivation of ours; no other rule exists in the tool.
- EVERY SOURCE IS PINNED (`sources.py`): a repository at a commit, or a fixed local image, and every entry's
  provenance names the project, the file, the URL and the commit (or the image). Fetched files live in
  `tmp/theme_sources/` and are never committed; only the bytes and their provenance are.
- NO BACKSTOPS: the inputs are pinned third-party files; a malformed one is a one-line hard fail naming it.
- A THEME OWNS THE CHROME. The catalog records EVERY raw value its source has, under the source's own key names
  (`raw`), even those no role reads today, so the waveform pane can later inherit from a theme by choice.
- A role a source has no word for is ABSENT and the app's own value applies; nothing is guessed.

## An entry

`key` (unique ASCII, lowercase words joined by hyphens behind the family prefix — `windows-`, `plus-`, `reactos-`,
`kde3-`, `cde-`, `warptempo-` — what is typed in Settings), `name` (the source's display name verbatim; a Windows 98
theme's file name, a CDE palette's file stem), `family`, `imitates` (optional: a KDE scheme whose name says it imitates
another desktop), `provenance` (`sources`: one record per source file; `rule`: the toolkit rule's id, its parameters
and every value it computed), `raw`, `roles`, `notes` (every disagreement between sources, every relabelling).

## Families and sources

| family | source | what |
|---|---|---|
| `windows` | ReactOS `boot/bootdata/hivedef.inf` ("New Schemes", COLOR_* indices, 0x00BBGGRR, English names from the first [Strings] block), corroborated by the Windows XP classic schemes saved as .theme files (zkedem/windows10-classic-themes; 1j01/98 `desktop/Themes/classicthemes8`) and Windows 98's `Windows Default.theme`; Windows 95 Standard hand-recorded | an entry is `windows` only when a second, independent source records it with equal bytes on every role key; Desert and Spruce (absent from ReactOS) come from the two XP records; ReactOS's "ReactOS Standard" and "ReactOS Classic" are Windows Classic and Windows Standard under ReactOS names and are folded into those entries |
| `windows-plus` | 1j01/98 `desktop/Themes/Windows Official/*.theme`, `[Control Panel\Colors]` | the Windows 98 / Plus! desktop themes; `Windows Default` corroborates Windows Standard, the byte-identical `Copy of Dangerous Creatures` is not a second entry |
| `reactos` | ReactOS hivedef.inf | its schemes no second source records (Green Olive, Sand, Sky; the four High Contrast schemes, whose Windows bytes no independent record was found for) |
| `kde3` | TDE tdebase `kcontrol/krdb/kcs/*.kcsrc` (49) + the six the Q4OS 6.9 TDE image adds | relief by KDE 3's rule at the scheme's own `contrast=` (default 7) |
| `cde` | cdesktopenv `cde/programs/palettes/*.dp` | the eight colour sets of each palette (16-bit, recorded as each channel's top byte, the verbatim lines in the provenance), and Motif's foreground, select colour and two shadows for every set; the four monochrome palettes (Black, White, BlackWhite, WhiteBlack: X colour names, refused by dtsession on a colour display) are reported, not imported |
| `warptempo` | `src/gui/render.h` at da0b1051 (git show) | the app's own look on 2026-10-03, so it stays selectable as bytes |

## The role mapping (`roles.py`, the one table)

| role | windows, windows-plus, reactos | kde3 | cde | warptempo |
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

NOT MAPPED IN THIS STEP: the flags, the red, the waveform ink and canvas are not catalog roles; the app-specific roles
the app derives from its ground today (the ruler label, the playhead head, the flag border, the checked face) are
drawn in the crops by the app's current rules over each theme's ground (crops.py's head) — their mapping is the
architect's decision once he has seen the crops.

## The checks (build.py, before the write)

Rainy Day's raw and roles are 8399B1 / C1CCD9 / 8399B1 / 4F657D / 000000 (face, Hilight, Light, Shadow, DkShadow);
Windows 95 Standard's quartet is FFFFFF / DFDFDF / 808080 / 000000 on C0C0C0; KDE 3 at contrast 7 on 303030 gives
3D3D3D / 343434 / 111111 / 000000; Motif on 303030 gives 989898 / 6E6E6E (the dark branch) and on 41525C A6AEB3 /
1E252A (both from an 8-bit colour as X parses `#rrggbb`, each byte replicated; `toolkit_rules.py` asserts the same at
import); the Warptempo entry equals render.h's constants; every key is unique and ASCII. The last lines print each
family: entries, corroborated, sources.

## The crops

`crops.py` writes a theme per entry over `tools/palette/themes/ad2.json`'s geometry (scratch:
`tmp/theme_catalog/themes/`), renders it on the default scene (`tmp/theme_catalog/full/`, never committed) and CROPS,
never scales: the top strip's left half, its right half down to the trim lane, and the bottom row's status panel
beside its last button groups, stacked 1152 px wide with a 4-px #7F7F7F rule between them; indexed PNG when a crop has
at most 256 colours, else RGB (every crop so far: the antialiased text exceeds 256), each with the Display-P3 iCCP
chunk. Its head states which roles come from the entry and which keep the app's rules.
