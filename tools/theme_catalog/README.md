# tools/theme_catalog — the imported themes, their provenance, the compiled chrome themes and the crops

THE CATALOG RECORDS IMPORTED THEMES ONLY, NO DERIVATION (architect 2026-10-03: "no derived, imported only; derivation
stays in git history"; "I don't want to be designing my own theme"). This tool turns the era's own theme files into
`docs/themes/catalog.json` — every colour a recorded byte with its provenance — and lists them in
`docs/themes/CATALOG.md` (the crops of the app that once stood beside each entry left the repository 2026-10-08). THE
CATALOG IS A RECORD AND THE SOURCE OF EACH CHROME'S COMPILED BYTES, NOT A SHIPPED SET (architect 2026-10-08: "okay to
retire the color theme catalog"; "we just need hard-coded chromes"): no theme file ships and the app reads none, no
device key or Settings row chooses one; each chrome vocabulary wears its own theme, compiled in (`src/gui/theme_file.h`;
Windows 2000's, the one chrome today). The architect's workshop for colors is the app's
PALETTES (named presets picked in the app); a look made official becomes a new chrome variant, its theme compiled in
the same way from a catalog entry. THE CATALOG IS THE CHROME'S ALONE (architect 2026-10-07): the program's own colors (the waveform, the flags, the
playhead's stem and the scanner) are no theme's but the app's PALETTES, compiled in (`src/gui/palette_file.h`; the
built-in ones Cool Edit's presets since 2026-10-09, below), and
the program's own family of chosen palette entries (the presets of his retired picker tool) left the catalog with them
("it'll still be in the git history"). THE ONE DESIGNED FAMILY (architect 2026-10-10 ~06:30: "I have created a theme
called Cool Edit Pro for the Chrome … make it a permanent part of the hard-coded as one of the options alongside
Windows 2000 Standard"): `warptempo`, THE PRODUCT'S OWN CHROME SCHEMES — his scheme files, picked in the app and saved by
the picker, recorded verbatim (build.py `warptempo_entries`; the family below). A standalone utility: no link path from
any product target, no CMake, Python 3 alone.

```
python3 tools/theme_catalog/fetch.py [--refresh]   # the pinned sources -> tmp/theme_sources/ (git-ignored)
python3 tools/theme_catalog/build.py               # -> docs/themes/catalog.json (runs the checks, prints the families)
python3 tools/theme_catalog/build.py --check-only  # no sources: the checks on the committed catalog, nothing written
python3 tools/theme_catalog/gen_theme_files.py     # -> src/gui/chrome_schemes.inc and chrome_schemes_product.inc (committed), the Windows 2000 column checked
python3 tools/theme_catalog/catalog_md.py          # -> docs/themes/CATALOG.md
python3 -I tools/theme_catalog/gen_cool_edit_presets.py [text]  # cool_edit_presets.txt -> src/gui/palette_presets.inc (committed)
```

## Cool Edit's presets — the built-in palettes (architect 2026-10-09)

THE PROGRAM'S BUILT-IN PALETTES ARE COOL EDIT PRO 2.1'S COLOR PRESETS (architect 2026-10-09 ~13:30: "only Cool Edit Pro
palettes for waveform needed now"; the program is Cool Edit under every chrome) and no catalog entry's: the per-chrome
default palettes (`windows-2000-standard`, `clearlooks`, `solaris` as palette keys, 2026-10-07..10-09) retired that day.
`cool_edit_presets.txt` is the source, committed: the scheme text inside `coolpro.exe` as extracted from his own copy
(its head is the provenance note), twenty schemes in Cool Edit's order, Cool Edit's "(Default Scheme)" first, every one
named — the two names the exe's text lacks, "3D" and "Easy on the Eyes", from Cool Edit's own registry entries written
under Wine (2026-10-09, the file's head).
`gen_cool_edit_presets.py` (standard library alone; the source's path optional on its command line, the committed copy
by default) transcribes each into the twelve program roles (`kGuiPaletteRoles`, `src/gui/palette_file.h`, read off the
header and checked) and writes `src/gui/palette_presets.inc`, the rows of `kGuiBuiltinPalettes` — never hand-edited.
Its head is the transcription's statement: WvBk, WvFg, GrdL, Cntr, CueM, RngM, Curs and Face to their roles (a scheme
without a Face takes the default's 626C7B), THE LIT OUTLINE the ink at HLS lightness 0.3157 (the view bar span's shadow
rule, `src/gui/cool_edit_derive.h`, ported operation for operation; `palette_file.cpp` proves every row against the
C++ tone()), and the three roles Cool Edit has no key for the same in every preset; the keys `cool-edit-<name>`, the
display names Cool Edit's own ("Default" for the default); a values line with no name line after it is the run's hard
fail. Byte-stable like the others.

## The compiled caption and the built-in schemes (architect 2026-10-04; compiled in since 2026-10-08; the caption alone since 2026-10-10)

SINCE 2026-10-10 THE APP'S CHROME INHERITS COOL EDIT (architect 2026-10-10: "the caption remains the only thing that's
outside of Cool Edit"): every chrome role but the caption's six is a tone of the live palette (`src/gui/chrome_derive.h`),
so what the app takes from this catalog is CAPTIONS alone. It compiles in ONE CAPTION PER CHROME
(`src/gui/theme_file.h`'s head): `windows-2000-standard`'s for the one chrome,
HAND-RECORDED as `kGuiCaptionWin2000`, which `gen_theme_files.py` checks against the entry at every run. (The Clearlooks
engine's tones, `build.py`'s `engine_tones`, are not computed since the clearlooks chrome's removal, 2026-10-09: its
arithmetic read three files that left with that chrome and stands in git history at d5b91f52^. The committed
catalog.json's `clearlooks` entry still carries its `engine_tones`: they are the record of a run `build.py` no longer
performs, and they leave catalog.json at its next build.) A catalog change is `build.py`, then `gen_theme_files.py` and
`catalog_md.py`, then the build, the outputs committed together; the output is byte-stable (the catalog's order,
uppercase hex, LF, no timestamp). No entry ships as a theme: the bundled theme files (`assets/themes/<key>.theme`,
2026-10-04..10-08) retired with the `theme` key, the git history keeping them. WHAT SHIPS OF EVERY ENTRY BUT
`clearlooks` (architect 2026-10-10: "we can remove the Clearlooks theme"; 104 of the 105, the windows-2000 chrome's
own and the product's own among them) is its SIX CAPTION KEYS — the caption's start, end and text and the inactive
caption's three (the ground, text, selection and field keys of 2026-10-08 retired 2026-10-10) — and NO FACE (architect
2026-10-09 ~21:20: "the scheme's default font should stop being honored — it should only be honored from the font
picker"; the app's face is its `font` device key; a Windows entry's `font` record stays the catalog's record and is
read by no generator) as the palette's BUILT-IN SCHEMES: `gen_theme_files.py` transcribes them into the generated
`src/gui/chrome_schemes.inc` (`kGuiChromeSchemes`, `src/gui/palette_file.h`; the transcription's rules and the display
names are the generator's head; never hand-edited), the product's own keys a second time into
`src/gui/chrome_schemes_product.inc` (`kGuiProductSchemeKeys`, the picker's Chrome menu seating them beside the
chrome's own). A built-in scheme takes THE CAPTION THE ENTRY RECORDS, each the recorded byte, a caption start recorded
without its gradient end making the end the start, a FLAT CAPTION (the generator's head is the statement):

| app role | from the entry |
|---|---|
| `caption_active`, `caption_active_gradient`, `caption_active_text`, `caption_inactive`, `caption_inactive_gradient`, `caption_inactive_text` | its recorded title colours (architect 2026-10-05, `roles.caption_roles`): Windows' ActiveTitle / GradientActiveTitle / TitleText and InactiveTitle / GradientInactiveTitle / InactiveTitleText, KDE 3's active / inactive background and foreground, CDE's colour sets 1 and 2 under their Motif foregrounds; a gradient end only where the entry records one (the 18 Windows entries with Gradient*Title), the generator's flat-caption rule making the end the start otherwise |

The catalog's other roles (the ground, the label, the relief quartet, the selected, field and info pairs) stay its
RECORD of each desktop and ship nowhere. The chrome is the entry AS RECORDED, through ONE light-roles function, `roles.light_roles` (the emboss's light copy
the recorded Hilight), which the frozen crops were rendered through too, so a compiled theme and its crop show one
chrome. The dark level (2026-10-03..04: a second, computed row per entry) was dropped with the app's
`theme_level` (architect 2026-10-04: a dark look is a theme he designs).

## The contract

- A THEME IS DRAWN AS ITS OWN DESKTOP DREW IT. Where the source records the bevels (a Windows scheme records all four
  3D colours) the bytes are the record. Where it records only base colours and its toolkit computed the rest at run
  time (a KDE 3 `.kcsrc`, a CDE `.dp`, a GTK 2 gtkrc and its metacity theme), build.py runs THAT TOOLKIT'S OWN RULE
  once, at import (`toolkit_rules.py`: Motif's `CalculateColorsRGB`, KDE 3's `createApplicationPalette` over Qt 3's
  integer HSV, GTK 2's shade — the Clearlooks engine's `ge_shade_color` — and metacity's blend in doubles, each value
  the byte the program paints: through cairo and pixman for the engine, through a GdkColor for GTK and metacity; each
  ported from the pinned source cited at its function) and the catalog stores the resulting bytes, the rule named in
  the entry's provenance (a GNOME 2 entry's also each computed value's derivation, `rule.derivations`) and described
  once in the catalog's `rules`. That is part of the import, not a
  derivation of ours.
- THE CHROME IS THE THEME'S; THE WAVEFORM PANE, THE FLAGS AND THE PLAYHEAD ARE THE PROGRAM'S OWN ELEMENTS (architect
  2026-10-03), their colors the app's PALETTES since 2026-10-07, never a theme's (`src/gui/palette_file.h`, whose
  role table owns them). The flags were the FLAT Acid flag, outlined in the canvas's color and shaded by nothing,
  2026-10-07/08; since 2026-10-09 they are Cool Edit's cues, red and blue triangles and dots (the palette's `cue` and
  `range`). Each entry still names the rule its family's desktop shaded a 3D face with (`flag_rule`:
  Windows' Appearance dialog, `windows_dialog`, over shlwapi's 240-scale integer HLS as Wine implements it; KDE 3's;
  Motif's; FLAT for GNOME 2, whose Clearlooks draws no one-line bevel round a raised face — `toolkit_rules.flag_bevel`), KEPT as the record of the design the flat flag replaced (the retired mock-up
  renderer's "bevelled" flag style); the app reads it nowhere. These are the only rules in the tool.
- EVERY SOURCE IS PINNED (`sources.py`): a repository at a commit, or a file inside a disc image on archive.org (the
  item, the image's SHA-1 and the file's own, which fetch.py checks), and every entry's provenance names the project,
  the file, the URL and the commit (or the image and its SHA-1s). Fetched files live in
  `tmp/theme_sources/` and are never committed; only the bytes and their provenance are. A LOCAL SOURCE
  (`LOCAL_SOURCES`, architect 2026-10-07) is a file of a disc image already on the build host, never fetched: the
  Debian 6.0.10 squeeze live image's Clearlooks gtkrc and metacity theme, extracted into `tmp/squeeze_fs/fs/`
  (`unsquashfs`, the recipe at `LOCAL_SOURCES`), each pinned by its own sha256 (build.py checks it) beside the image's
  and the squashfs's sha256 and the image's URL; the engine's and metacity's tarballs, cited for the rule, by their
  sha256 and URL. The Windows 95 OSR2 disc image (`tmp/W95_PLUS_AR.iso`, architect 2026-10-09) is the other: its
  `shell2.inf`, extracted into `tmp/theme_sources/win95_shell2inf/` (`7z`, the recipe at `sources.py`), pinned by its
  own sha256 (build.py checks it) beside the cabinet's, the continuation's and the image's; the image has no URL. The
  third (architect 2026-10-09) is his two guidebookgallery screenshots of WordPad under Windows Me and Windows 2000
  Professional, `tmp/winme.png` and `tmp/win2000pro.png` as he saved them, each pinned by its sha256 (build.py
  checks it) and read by parse_windows.py's standard-library PNG reader for `windows-me-standard`.
- NO BACKSTOPS: the inputs are pinned third-party files; a malformed one is a one-line hard fail naming it.
- A THEME OWNS THE CHROME. The catalog records EVERY raw value its source has, under the source's own key names
  (`raw`), even those no role reads today, so the waveform pane can later inherit from a theme by choice.
- A role a source has no word for is ABSENT, the entry does not name it, and Windows 2000's value applies; nothing is guessed.

## An entry

`key` (unique ASCII, lowercase words joined by hyphens behind the family prefix — `windows-`, `plus-`, `kde3-`,
`cde-`; a `gnome2` key is the GTK theme's own name, lowercase, with no prefix, `clearlooks`, and a `warptempo` key the
scheme's name the same way, `cool-edit-pro-me`), `name` (the source's display name verbatim; a Windows 98
theme's file name, a CDE palette's file stem), `family`, `imitates` (optional: a KDE scheme whose name says it imitates
another desktop), `provenance` (`sources`: one record per source file; `rule`: the toolkit rule's id, its parameters
and every value it computed), `raw`, `roles`, `flag_rule` (the rule the bevelled flag style took — kept as a record, read by
nothing since the retired renderer's "bevelled" style: `{"id":
"windows-dialog"}` for the families `windows`, `windows-plus` and `warptempo` — the architect: "take Windows' rule" —,
`{"id": "kde3", "contrast": c}` at the scheme's own contrast, `{"id": "motif"}` for `cde`, `{"id": "flat"}` for
`gnome2`; described in the catalog's `rules`), `display_tier` (below), `notes` (every disagreement between sources, every relabelling).

## The display tier (`display_tier`; architect 2026-10-03, late)

Each entry is tagged by the smallest period colour set holding every colour its roles use (build.py `display_tier`;
the sets are also catalog.json's `display_tiers`, and each entry's CATALOG.md block names its tier):

| tier | the set |
|---|---|
| `vga` | the 16 VGA colours: 000000 800000 008000 808000 000080 800080 008080 C0C0C0 808080 FF0000 00FF00 FFFF00 0000FF FF00FF 00FFFF FFFFFF |
| `windows-20` | those and Windows' four static extras C0DCC0 A6CAF0 FFFBF0 A0A0A4 (reserved in a 256-colour display's system palette, so always solid there) |
| `high-colour` | anything else |

Today: `vga` 4 (Windows Storm, Teal, and Red, White, and Blue, and Windows 95 Storm; build.py asserts the list),
`windows-20` 0, `high-colour` 101. Windows 98 Standard misses `vga` only by its tooltip ground FFFFE1, Windows 95 Standard by that and
its 3DLight DFDFDF (asserted for Windows 98 Standard).

## Families and sources

| family | source | what |
|---|---|---|
| `warptempo` | the architect's own scheme files, written by the app's color picker (Save As), their twelve chrome keys recorded verbatim in build.py's `WARPTEMPO_SCHEMES` and in each entry's provenance (`recorded`) | THE PRODUCT'S OWN CHROME SCHEMES (architect 2026-10-10), designed, not imported, first in the catalog: `cool-edit-pro-me` ("Cool Edit Pro ME", his file of 2026-10-10 ~08:40, replacing his ~06:50 file) — Windows Me Standard's caption, inactive caption and selection fill under Cool Edit Pro 2.1's panel tones (the ground 626C7B its panel face, the "Dockable Window 3D Color" default and the palette's `face`, since ~08:45 — "using a lighter chrome makes that line … stand out as a different shade", the frame's Shadow line under the menu row against Cool Edit's darker band line; the toolbar recess 4E5662 that morning; the field 414751 its pane's bottom mid line, the text, selection text and field text EFF0F0 its text white); the raw keys are the scheme's own (`chrome_ground` …), the relief quartet Windows' Appearance-dialog rule on the ground, run at import (`windows-dialog`), the record of the file (the app takes its six caption keys alone since 2026-10-10) |
| `windows` | ReactOS `boot/bootdata/hivedef.inf` ("New Schemes", COLOR_* indices, 0x00BBGGRR, English names from the first [Strings] block), corroborated by the Windows XP classic schemes saved as .theme files (zkedem/windows10-classic-themes; 1j01/98 `desktop/Themes/classicthemes8`) and Windows 98's `Windows Default.theme`; Windows 2000's own setup hive, `I386/HIVEDEF.INF` of the Windows 2000 Professional SP3 disc image on archive.org (its HKCU `Control Panel\Colors` and its `Appearance\Schemes` values, each a 712-byte SCHEMEDATA whose 29 COLORREFs parse_windows.py reads); Windows 95's own Appearance schemes, `shell2.inf` (dated 1996-08-24) in the cabinet `win95/PRECOPY1.CAB` (spanned with `PRECOPY2.CAB`) of the Windows 95 OSR2 disc image `W95_PLUS_AR.iso`, a LOCAL SOURCE (below): 27 registry values of 492 bytes, each a COLORREF[25] (COLOR_SCROLLBAR .. COLOR_INFOBK, R, G, B and a flag byte) in its last 100 bytes, which parse_windows.py `parse_win95_schemes` reads | a hivedef.inf scheme is imported only when a second, independent source records it with equal bytes on every role key (the rest: Not imported, below); Desert and Spruce (absent from ReactOS) come from the two XP records. THE THREE DEFAULT SCHEMES (architect 2026-10-06, "just make it accurate"), one entry per distinct byte set, keyed by the release whose default the bytes are, every source's own label in the notes: `windows-2000-standard`, Windows 2000's own HKCU colours (its hive names the scheme "Windows Standard"), corroborated by zkedem's `standard.theme` ("Windows Standard"), classicthemes8's "Windows XP Classic" and ReactOS's "ReactOS Standard"; `windows-98-standard`, the C0C0C0 face under the navy-to-1084D0 caption, zkedem's `classic.theme` ("Windows Classic"), corroborated by Windows 2000's own "Windows Classic" scheme value, ReactOS's "ReactOS Classic" and Windows 98's `Windows Default.theme` (equal on every key it records but the desktop Background; it carries no Gradient key) — Windows 95 Standard's roles but for the 3DLight (C0C0C0 against DFDFDF) and the caption's gradient ends (1084D0 / B5B5B5 against flat); `windows-95-standard`, the Windows 95 CD's "Windows Standard" (architect 2026-10-09: it replaced the hand-recorded entry of 2026-10-03, whose bytes the CD equals on all 25 values; DFDFDF the 3DLight, as the retail captures show it). `windows-me-standard` (architect 2026-10-09), right after `windows-2000-standard`: Windows 2000 Standard's bytes under the font record `ms-sans-serif` (as a built-in scheme, Windows 2000 Standard's twelve under Me's name: a scheme carries no face since 2026-10-09 ~21:20), Windows Me's classic desktop being Windows 2000's in MS Sans Serif — a LOCAL SOURCE (below), his two guidebookgallery captures of WordPad (`tmp/winme.png`, `tmp/win2000pro.png`), byte-checked against the hive's values where they show one (ButtonFace, ButtonHilight, ButtonShadow, ButtonDkShadow, ActiveTitle, GradientActiveTitle) and the face measured off them (the title's capital 9 rows against Tahoma's 8). THE WINDOWS 95 FLAVOURS (architect 2026-10-09): of the CD's 27 schemes, the six whose bytes differ from Windows 2000's of the same name are entries of their own right after `windows-95-standard`, under a `windows-95-` prefix, 25 values each and a flat caption (Windows 95 has no Gradient, MenuHilight or MenuBar): `windows-95-maple` (ActiveTitle, InactiveTitle, AppWorkspace), `windows-95-wheat` (AppWorkspace), `windows-95-marine` (TitleText), `windows-95-storm` (InactiveTitleText), `windows-95-rose` (InactiveTitleText), `windows-95-plum` (TitleText, Hilight); the other 21 are not entries (11 byte-equal to an existing entry, 10 size variants and High Contrast schemes: catalog.json's `not_imported.windows_95_cd`, build.py's WIN95_* maps, which account for every one of the 27). The former `windows-classic` (the 2000 bytes) and `windows-standard` (the 98 bytes) are retired into them |
| `windows-plus` | 1j01/98 `desktop/Themes/Windows Official/*.theme`, `[Control Panel\Colors]` | the Windows 98 / Plus! desktop themes; `Windows Default` corroborates Windows 98 Standard, the byte-identical `Copy of Dangerous Creatures` is not a second entry |
| `kde3` | TDE tdebase `kcontrol/krdb/kcs/*.kcsrc` (49), less the 24 not imported: KDE 3.5's three usability schemes and the 21 Trinity added later — KDE 3.5's own 25 (architect 2026-10-03, late: the KDE catalog keeps only what KDE 3.5 shipped; build.py asserts 25). The Q4OS 6.9 TDE image was a second source until 2026-10-03; its six schemes were all not imported, and the source was dropped | relief by KDE 3's rule at the scheme's own `contrast=` (default 7) |
| `cde` | cdesktopenv `cde/programs/palettes/*.dp` | the eight colour sets of each palette (16-bit, recorded as each channel's top byte, the verbatim lines in the provenance), and Motif's foreground, select colour and two shadows for every set; the four monochrome palettes (Black, White, BlackWhite, WhiteBlack: X colour names, refused by dtsession on a colour display) are reported, not imported |
| `gnome2` | the Debian 6.0.10 squeeze live image's own bytes (`LOCAL_SOURCES`): `usr/share/themes/Clearlooks/gtk-2.0/gtkrc` (gtk2-engines 1:2.20.1-1) and `metacity-1/metacity-theme-1.xml` (gnome-themes 2.30.2-1), read by `parse_gtkrc.py`; the rule cited at the gtk-engines 2.20.2 and metacity 2.30.3 tarballs | `clearlooks` ("Clearlooks", architect 2026-10-07: squeeze's GNOME 2.30 default, the clearlooks chrome vocabulary's theme 2026-10-07 to 2026-10-09, a built-in scheme since): the gtkrc's eight `gtk-color-scheme` colours as recorded and five values by the programs' own rules, each with its derivation (`rule.derivations`): the engine's ONE-LINE EDGE as the relief quartet (light, light, dark, dark) = shade 1.06 / 0.94 of the ground (`clearlooks_draw_inset`, `clearlooks_draw_highlight_and_shade`), the tooltip border (shade 0.6 of its ground), the insensitive text (`darker (@bg_color)`), the unfocused title (metacity's `blend/gtk:fg[NORMAL]/gtk:bg[NORMAL]/0.45`); the focused title's literal #FFFFFF raw. The engine's other tones (its shade table, the gummy ramps, metacity's band) are not catalog roles, and since the Clearlooks painters left (2026-10-09) build.py no longer computes them (`engine_tones`, git history at d5b91f52^). His captures (`tmp/squeeze/`) show every computed byte as recorded |

Every family's line in the build: warptempo 1, windows 26, windows-plus 16, kde3 25, cde 36, gnome2 1 — 105 entries
(the program's own family of palette entries, the two presets of his retired picker tool, left 2026-10-07 with the
program roles; the `warptempo` family is the product's own chrome schemes since 2026-10-10). Where the pinned sources
cannot be fetched (the cloud: the Trinity mirror lies outside its egress; the squeeze image off the laptop),
`build.py --check-only` runs the checks on the committed catalog.json and writes nothing.

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

| role | windows, windows-plus | kde3 | cde | gnome2 |
|---|---|---|---|---|
| `ground` | `ButtonFace` | `background` | `set5` | `bg_color` |
| `label` | `ButtonText` | `foreground` | `motif:set5.fg` | `fg_color` |
| `bevel_hilight` | `ButtonHilight` | `kde3:light` | `motif:set5.ts` | `gtk2:inset_light` |
| `bevel_light` | `ButtonLight` | `kde3:midlight` | `motif:set5.ts` | `gtk2:inset_light` |
| `bevel_shadow` | `ButtonShadow` | `kde3:dark` | `motif:set5.bs` | `gtk2:inset_dark` |
| `bevel_dkshadow` | `ButtonDkShadow` | `kde3:shadow` | `motif:set5.bs` | `gtk2:inset_dark` |
| `selected_fill` | `Hilight` | `selectBackground` | absent | `selected_bg_color` |
| `selected_text` | `HilightText` | `selectForeground` | absent | `selected_fg_color` |
| `info_ground` | `InfoWindow` | absent | absent | `tooltip_bg_color` |
| `info_text` | `InfoText` | absent | absent | `tooltip_fg_color` |
| `info_frame` | absent | absent | absent | `gtk2:tooltip_border` |
| `field_ground` | `Window` | `windowBackground` | `set4` | `base_color` |
| `field_text` | `WindowText` | `windowForeground` | `motif:set4.fg` | `text_color` |
| `disabled_text` | `GrayText` | `kde3:disabled_foreground` | absent | `gtkrc:fg[INSENSITIVE]` |
| `title_active` | `ActiveTitle` | `activeBackground` | `set1` | `selected_bg_color` |
| `title_inactive` | `InactiveTitle` | `inactiveBackground` | `set2` | `bg_color` |

The `warptempo` family (2026-10-10) maps the scheme's own keys: `ground` `chrome_ground`, `label` `chrome_text`, the
quartet `windows-dialog:hilight` / `:light` / `:shadow` / `:dkshadow`, the selected pair `chrome_selection` /
`chrome_selection_text`, the field pair `chrome_field` / `chrome_field_text`, the titles `chrome_title_start` /
`chrome_inactive_title_start` (its caption the six `chrome_*title*` keys, `roles.CAPTION`); no info pair, no disabled
text.

A `kde3:`, `motif:`, `gtk2:`, `gtkrc:`, `metacity:` or `windows-dialog:` value is computed by that rule at import (`provenance.rule.computed`); every other
value is a raw key. KDE 3's relief comes from the scheme's `background` (its `buttonBackground` is recorded raw: the
app has one ground). CDE's colour sets (Motif `ColorObj.c`'s resource defaults, dtsession `SrvPalette.c`, dtwm
`WmResource.c` / `Dtwm.defs`): 1 the active window frame, 2 the inactive frame, 3 and 7 workspace backdrops, 4 text
and lists, 5 THE PRIMARY (an application's background; dtsession's high-colour `*background`), 6 the secondary (menus,
dialogs), 8 the front panel; Motif paints two shadows, so its quartet is (ts, ts, bs, bs); it has no selection
highlight (text selection is inverse video), no tooltip pair and no disabled colour (insensitive text is stippled).

THE APP-SPECIFIC ROLES (architect 2026-10-03), drawn from the catalog roles above rather than stored — the record of
2026-10-03 to 2026-10-08: the ruler label and the trim lane's arrow glyph <- `label`; the ruler ticks <-
`bevel_shadow`; the playhead head's outline <- `label`; THE DISABLED EMBOSS's light copy <- `bevel_hilight`
(`roles.light_roles`). Since 2026-10-09 the ruler, the trim lane, the playhead's head and
the cues are Cool Edit's program, their tones the palette's `face` and its derived tones and the palette's roles
(`src/gui/palette_file.h`, `src/gui/cool_edit_derive.h`). THE PROGRAM'S OWN COLORS are no catalog roles and no theme's
(2026-10-07): they are the app's palettes. The frozen crops, rendered before the split, show the program colors of their
day (CATALOG.md's head says which).

## The checks (build.py, before the write)

Rainy Day's raw and roles are 8399B1 / C1CCD9 / 8399B1 / 4F657D / 000000 (face, Hilight, Light, Shadow, DkShadow);
Windows 95 Standard's quartet is FFFFFF / DFDFDF / 808080 / 000000 on C0C0C0; KDE 3 at contrast 7 on 303030 gives
3D3D3D / 343434 / 111111 / 000000; Motif on 303030 gives 989898 / 6E6E6E (the dark branch) and on 41525C A6AEB3 /
1E252A (both from an 8-bit colour as X parses `#rrggbb`, each byte replicated; `toolkit_rules.py` asserts the same at
import); Windows' dialog rule gives D4D0C8 -> Hilight EAE8E3 and Rainy Day 8399B1 -> C1CCD9 / 4F657D; every entry's
`flag_rule` is its family's and runs; kde3 has 25 entries; the not-imported lists are exactly the schemes found (a re-pinned source cannot
change them silently) and each duplicate is role-identical to its twin; no entry carries program roles and every
family is one of build.py's FAMILIES; `cool-edit-pro-me` leads the catalog, its raw the twelve of WARPTEMPO_SCHEMES,
its quartet AEB5BF / 626C7B / 414752 / 000000 on 626C7B (`src/gui/chrome_derive.h` asserts the same); the `vga` entries are exactly Windows Storm, Teal, Red, White, and Blue and Windows 95 Storm, none is `windows-20`;
every key is unique and ASCII; Clearlooks' roles are exactly the squeeze bytes (#EDECEB, #000000, the quartet
#FBFBFA / #FBFBFA / #E0DEDD / #E0DEDD, #86ABD9 / #FFFFFF, #F5F5B5 / #000000 / #BABA45, #FFFFFF / #1A1A1A, #A9A5A2,
#86ABD9 / #EDECEB), its unfocused title #6B6A6A, its flag rule flat (`toolkit_rules.py` asserts the engine's shade
table points shade[3] of #EDECEB = #C4C2BF and spot[1] of #86ABD9 = #92B4DF, and the captured #BABA45, #FBFBFA /
#E0DEDD and #6B6A6A, at import). The last lines print each family (entries, corroborated, sources), the display
tiers' counts and the schemes not imported.

## The crops (left the repository 2026-10-08)

`docs/themes/crops/<key>.png` were the app as the mock-up renderer drew it on 2026-10-05 and 06 in the tablet's
geometry, one per entry but `clearlooks`, cropped, never scaled; they showed the retired Windows 95 chrome in Nimbus
Sans and were never re-rendered (the render road broke at the Windows 2000 pivot and retired with the mock-up renderer
on 2026-10-07). They left the repository with their lines in CATALOG.md on 2026-10-08 (the git history keeps them);
`catalog_md.py` leaves an empty slot where an entry's crop stood, and CATALOG.md's head still tells their story.
