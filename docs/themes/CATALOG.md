# The theme catalog

Every entry below is a desktop theme of the era IMPORTED, not designed (architect 2026-10-03: "no derived, imported only"): its colours are the bytes its source records, each with its provenance in [catalog.json](catalog.json); where the source records only base colours and its own toolkit computed the relief at run time (KDE 3, CDE / Motif, GNOME 2's Clearlooks and metacity), that toolkit's rule ran once at import and is named. A theme is the CHROME's colors alone (2026-10-07): the program's own colors are its palettes, compiled into the app (src/gui/palette_file.h), and the program's own family of chosen entries left the catalog with them. THE CATALOG IS A RECORD, NOT A SHIPPED SET (2026-10-08): no theme ships as a file and none is chosen in the app; each chrome's colors are compiled in, and the catalog is their source — `windows-2000-standard` the windows-2000 chrome's, `clearlooks` the clearlooks chrome's (tools/theme_catalog/gen_theme_files.py, src/gui/theme_file.h) — and the record a look made official as a new chrome variant is drawn from. Each entry once had a crop, the app as the retired mock-up renderer drew it on 2026-10-05 and 06 in the tablet's geometry (the tablet's 2304 x 1440 at gui_scale 275, cropped, never scaled: the top strip in two halves over the well's bottom lines and the bottom row, transparent between them), before the product's Windows 2000 pivot of 2026-10-06 evening, so the crops showed the retired Windows 95 chrome in Nimbus Sans; that render road broke with the pivot and retired on 2026-10-07, the crops were frozen as last rendered (a theme's colours read true in them, and a theme imported since had none), and they left the repository on 2026-10-08 (the git history keeps them). The chrome was the theme's as recorded; the waveform pane, the flags and the playhead were the program's own elements in the colors of the crop's day (the lime waveform on black with its green outline, the warp flag purple #800080 and its selected face fuchsia #FF00FF, the invalid flag maroon #800000 and its selected face red #FF0000, white labels, the playhead's white stem; a theme names none of them since 2026-10-07: they are the app's palettes), its playhead's head was the renderer's older grey head, not the app's WordPad ruler marker in the theme's chrome. The well kept the app's two-line sunken edge (the theme's Shadow and DkShadow above, its 3DLight and Hilight below); the flags were the flat Acid flag (the scene's flags were warp markers, so the warp pair), the face with a one-px outline in the theme's DkShadow, the stem leaving the face across the bottom outline, shown left to right editing (the in-place editor: the selected face under a black frame, its text in the selected pair), selected (the selected face under the selected label, the stem with it), invalid (the removed pair), disabled (the ground, the label embossed) and unselected; the playhead's head carried a one-px outline in the theme's label; disabled words and glyphs were Windows' emboss; the ruler label and the trim arrow were the theme's label, the ruler ticks its Shadow. The DISPLAY TIER is the smallest period colour set holding every colour the entry's roles use: vga (the 16 VGA colours), windows-20 (those and Windows' four static extras #C0DCC0, #A6CAF0, #FFFBF0, #A0A0A4, always solid on a 256-colour display), else high-colour. Not imported: catalog.json's `not_imported`. Built by `tools/theme_catalog/` (fetch.py, build.py, catalog_md.py; the theme files by gen_theme_files.py).

## Windows: the Appearance schemes (ReactOS hivedef.inf, corroborated by the Windows XP classic schemes saved as .theme files; Windows 2000 Standard from Windows 2000's own setup hive; Windows 95 Standard and the six Windows 95 flavours from the Windows 95 CD's own shell2.inf; Windows Me Standard, Windows 2000 Standard's bytes in MS Sans Serif, checked on his two WordPad captures)

26 entries, darkest ground first.

The Windows 95 CD carries 27 Appearance schemes (shell2.inf); only the six whose bytes differ from Windows 2000's of the same name are entries here, under a `windows-95-` prefix — Maple, Wheat, Marine, Storm, Rose and Plum — the other 21 being equal to an entry (Windows Standard, Brick, Spruce, Teal, Red, White, and Blue, Pumpkin, Eggplant, Rainy Day, Desert, Lilac, Slate), a size variant of one, or a usability scheme (`not_imported.windows_95_cd`, catalog.json).

### `windows-rainy-day`

**Rainy Day** · ground #8399B1 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-plum`

**Plum** · ground #A89890 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-95-plum`

**Windows 95 Plum** · ground #A89890 · W95_PLUS_AR.iso `shell2.inf`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `windows-eggplant`

**Eggplant** · ground #90B0A8 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Times New Roman)


### `windows-lilac`

**Lilac** · ground #AEA8D9 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: tahoma (the source's menu font: Tahoma)


### `windows-slate`

**Slate** · ground #9DB9C8 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-marine`

**Marine** · ground #88C0B8 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-95-marine`

**Windows 95 Marine** · ground #88C0B8 · W95_PLUS_AR.iso `shell2.inf`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `windows-rose`

**Rose** · ground #CFAFB7 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-95-rose`

**Windows 95 Rose** · ground #CFAFB7 · W95_PLUS_AR.iso `shell2.inf`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `windows-brick`

**Brick** · ground #C2BFA5 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-spruce`

**Spruce** · ground #A2C8A9 · zkedem/windows10-classic-themes@f126b0b1 `spruce.theme` + 1 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-storm`

**Storm** · ground #C0C0C0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: vga · Face: tahoma (the source's menu font: Tahoma)


### `windows-teal`

**Teal** · ground #C0C0C0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: vga · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-red-white-and-blue`

**Red, White, and Blue** · ground #C0C0C0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: vga · Face: ms-sans-serif (the source's menu font: Times New Roman)


### `windows-95-standard`

**Windows 95 Standard** · ground #C0C0C0 · W95_PLUS_AR.iso `shell2.inf`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `windows-95-storm`

**Windows 95 Storm** · ground #C0C0C0 · W95_PLUS_AR.iso `shell2.inf`

Display tier: vga · Face: ms-sans-serif (the source's menu font: Arial)


### `windows-98-standard`

**Windows 98 Standard** · ground #C0C0C0 · zkedem/windows10-classic-themes@f126b0b1 `classic.theme` + 3 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-desert`

**Desert** · ground #D5CCBB · zkedem/windows10-classic-themes@f126b0b1 `desert.theme` + 1 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-2000-standard`

**Windows 2000 Standard** · ground #D4D0C8 · Windows2000ProfessionalSP3.iso `I386/HIVEDEF.INF` + 4 more

Display tier: high-colour · Face: tahoma (the source's menu font: Tahoma)


### `windows-me-standard`

**Windows Me Standard** · ground #D4D0C8 · guidebookgallery.org screenshots of WordPad under Windows Me and Windows 2000 Professional (the architect's copies, 1:1) `winme.png` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `windows-pumpkin`

**Pumpkin** · ground #ECD59D · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-maple`

**Maple** · ground #E6D8AE · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-95-maple`

**Windows 95 Maple** · ground #E6D8AE · W95_PLUS_AR.iso `shell2.inf`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `windows-wheat`

**Wheat** · ground #DEDEA0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Microsoft Sans Serif)


### `windows-95-wheat`

**Windows 95 Wheat** · ground #DEDEA0 · W95_PLUS_AR.iso `shell2.inf`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


## Windows 98 / Plus! desktop themes (the shipped .theme files)

16 entries, darkest ground first.

### `plus-underwater`

**Underwater (high color)** · ground #3868C8 · 1j01/98@52451052 `desktop/Themes/Windows Official/Underwater (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Haettenschweiler)


### `plus-dangerous-creatures`

**Dangerous Creatures (256 color)** · ground #707070 · 1j01/98@52451052 `desktop/Themes/Windows Official/Dangerous Creatures (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Arial)


### `plus-mystery`

**Mystery (high color)** · ground #687868 · 1j01/98@52451052 `desktop/Themes/Windows Official/Mystery (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Calisto MT)


### `plus-travel`

**Travel (high color)** · ground #908070 · 1j01/98@52451052 `desktop/Themes/Windows Official/Travel (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Comic Sans MS)


### `plus-space`

**Space (256 color)** · ground #809098 · 1j01/98@52451052 `desktop/Themes/Windows Official/Space (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: OCR A Extended)


### `plus-the-60s-usa`

**The 60's USA (256 color)** · ground #D068D8 · 1j01/98@52451052 `desktop/Themes/Windows Official/The 60's USA (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Lucida Sans)


### `plus-science`

**Science (256 color)** · ground #8399B1 · 1j01/98@52451052 `desktop/Themes/Windows Official/Science (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Lucida Sans Unicode)


### `plus-more-windows`

**More Windows (high color)** · ground #9098A0 · 1j01/98@52451052 `desktop/Themes/Windows Official/More Windows (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: News Gothic MT)


### `plus-jungle`

**Jungle (256 color)** · ground #B8A068 · 1j01/98@52451052 `desktop/Themes/Windows Official/Jungle (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Tempus Sans ITC)


### `plus-leonardo-da-vinci`

**Leonardo da Vinci (256 color)** · ground #BFA59F · 1j01/98@52451052 `desktop/Themes/Windows Official/Leonardo da Vinci (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Book Antiqua)


### `plus-baseball`

**Baseball (256 color)** · ground #D0A870 · 1j01/98@52451052 `desktop/Themes/Windows Official/Baseball (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: MS Sans Serif)


### `plus-inside-your-computer`

**Inside your Computer (high color)** · ground #A8C8A8 · 1j01/98@52451052 `desktop/Themes/Windows Official/Inside your Computer (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Abadi MT Condensed Light)


### `plus-windows-98`

**Windows 98 (256 color)** · ground #B4C3DC · 1j01/98@52451052 `desktop/Themes/Windows Official/Windows 98 (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: News Gothic MT)


### `plus-nature`

**Nature (high color)** · ground #D8C0A0 · 1j01/98@52451052 `desktop/Themes/Windows Official/Nature (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Copperplate Gothic Light)


### `plus-the-golden-era`

**The Golden Era (high color)** · ground #B8C8B8 · 1j01/98@52451052 `desktop/Themes/Windows Official/The Golden Era (high color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: Century Gothic)


### `plus-sports`

**Sports (256 color)** · ground #B0E0A0 · 1j01/98@52451052 `desktop/Themes/Windows Official/Sports (256 color).theme`

Display tier: high-colour · Face: ms-sans-serif (the source's menu font: OCR A Extended)


## KDE 3.5 colour schemes, as Trinity's tdebase carries them (relief by KDE 3's own rule at each scheme's contrast)

25 entries, darkest ground first.

### `kde3-dark-blue`

**Dark Blue** · ground #426794 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/DarkBlue.kcsrc`; kde3 rule at contrast 8

Display tier: high-colour


### `kde3-digital-cde`

**Digital CDE** — imitates CDE (Digital UNIX) · ground #4B7B82 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/DigitalCDE.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-cde`

**CDE** — imitates CDE · ground #999999 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/CDE.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-next`

**Next** — imitates NeXTSTEP · ground #A8A8A8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Next.kcsrc`; kde3 rule at contrast 10

Display tier: high-colour


### `kde3-atlas-green`

**Atlas Green** · ground #AFB49F · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/AtlasGreen.kcsrc`; kde3 rule at contrast 5

Display tier: high-colour


### `kde3-solaris`

**Solaris** — imitates CDE (Solaris) · ground #AEB2C3 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/SolarisCDE.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour


### `kde3-blue-slate`

**Blue Slate** · ground #9DB9C8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/BlueSlate.kcsrc`; kde3 rule at contrast 5

Display tier: high-colour


### `kde3-kde-1`

**KDE 1** · ground #C0C0C0 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KDEOne.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-storm`

**Storm** · ground #C0C0C0 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Storm.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-redmond-95`

**Redmond 95** — imitates Windows 95 · ground #C3C3C3 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Windows95.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-point-reyes-green`

**Point Reyes Green** · ground #D3C5BE · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/PointReyesGreen.kcsrc`; kde3 rule at contrast 0

Display tier: high-colour


### `kde3-desert-red`

**Desert Red** · ground #D6CDBB · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/DesertRed.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour


### `kde3-redmond-2000`

**Redmond 2000** — imitates Windows 2000 · ground #D4D0C8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Windows2000.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-system`

**System** · ground #D3D3D3 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/System.kcsrc`; kde3 rule at contrast 2

Display tier: high-colour


### `kde3-pale-gray`

**Pale Gray** · ground #D6D6D6 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/PaleGray.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour


### `kde3-beos`

**BeOS** — imitates BeOS · ground #D9D9D9 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/BeOS.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-pumpkin`

**Pumpkin** · ground #EED8AE · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Pumpkin.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour


### `kde3-kde-2`

**KDE 2** · ground #DCDCDC · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KDETwo.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-media-peach`

**Media Peach** · ground #F4DDB2 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/MediaPeach.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour


### `kde3-evex`

**EveX** · ground #E6DEDC · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/EveX.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-keramik-white`

**Keramik White** · ground #E9E9E9 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KeramikWhite.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-keramik`

**Keramik** · ground #EAE9E8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Keramik.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-keramik-emerald`

**Keramik Emerald** · ground #EEEEE6 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KeramikEmerald.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-redmond-xp`

**Redmond XP** — imitates Windows XP · ground #EEEEE6 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/WindowsXP.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


### `kde3-plastik`

**Plastik** · ground #EFEFEF · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Plastik.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour


## CDE palettes (colour set 5 the ground; foreground and shadows by Motif's own rule)

36 entries, darkest ground first.

### `cde-northern-sky`

**NorthernSky** · ground #41525C · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/NorthernSky.dp`; motif rule

Display tier: high-colour


### `cde-cinnamon`

**Cinnamon** · ground #9B6862 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Cinnamon.dp`; motif rule

Display tier: high-colour


### `cde-cabernet`

**Cabernet** · ground #B65C71 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Cabernet.dp`; motif rule

Display tier: high-colour


### `cde-neptune`

**Neptune** · ground #637E95 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Neptune.dp`; motif rule

Display tier: high-colour


### `cde-golden`

**Golden** · ground #757CA6 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Golden.dp`; motif rule

Display tier: high-colour


### `cde-south-west`

**SouthWest** · ground #B86F4B · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SouthWest.dp`; motif rule

Display tier: high-colour


### `cde-mustard`

**Mustard** · ground #887CA3 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Mustard.dp`; motif rule

Display tier: high-colour


### `cde-charcoal`

**Charcoal** · ground #828585 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Charcoal.dp`; motif rule

Display tier: high-colour


### `cde-urchin`

**Urchin** · ground #3792A2 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Urchin.dp`; motif rule

Display tier: high-colour


### `cde-sand`

**Sand** · ground #AF8181 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Sand.dp`; motif rule

Display tier: high-colour


### `cde-camouflage`

**Camouflage** · ground #8E8E77 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Camouflage.dp`; motif rule

Display tier: high-colour


### `cde-sky-red`

**SkyRed** · ground #6E91AA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SkyRed.dp`; motif rule

Display tier: high-colour


### `cde-arizona`

**Arizona** · ground #C37D79 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Arizona.dp`; motif rule

Display tier: high-colour


### `cde-beige-rose`

**BeigeRose** · ground #919191 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/BeigeRose.dp`; motif rule

Display tier: high-colour


### `cde-tundra`

**Tundra** · ground #859680 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Tundra.dp`; motif rule

Display tier: high-colour


### `cde-clay`

**Clay** · ground #AF9191 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Clay.dp`; motif rule

Display tier: high-colour


### `cde-crimson`

**Crimson** · ground #68A6A0 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Crimson.dp`; motif rule

Display tier: high-colour


### `cde-alpine`

**Alpine** · ground #BF9279 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Alpine.dp`; motif rule

Display tier: high-colour


### `cde-pbnj`

**PBNJ** · ground #C69076 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/PBNJ.dp`; motif rule

Display tier: high-colour


### `cde-santa-fe`

**SantaFe** · ground #83A5B6 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SantaFe.dp`; motif rule

Display tier: high-colour


### `cde-nutmeg`

**Nutmeg** · ground #B39C8F · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Nutmeg.dp`; motif rule

Display tier: high-colour


### `cde-lilac`

**Lilac** · ground #A5A5AC · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Lilac.dp`; motif rule

Display tier: high-colour


### `cde-chocolate`

**Chocolate** · ground #B9A0A0 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Chocolate.dp`; motif rule

Display tier: high-colour


### `cde-dark-gold`

**DarkGold** · ground #A8A8A8 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/DarkGold.dp`; motif rule

Display tier: high-colour


### `cde-grass`

**Grass** · ground #A4A9AA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Grass.dp`; motif rule

Display tier: high-colour


### `cde-delphinium`

**Delphinium** · ground #78B4BE · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Delphinium.dp`; motif rule

Display tier: high-colour


### `cde-desert`

**Desert** · ground #9FAEB5 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Desert.dp`; motif rule

Display tier: high-colour


### `cde-default`

**Default** · ground #C6B2A8 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Default.dp`; motif rule

Display tier: high-colour


### `cde-savannah`

**Savannah** · ground #C6B2A8 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Savannah.dp`; motif rule

Display tier: high-colour


### `cde-orchid`

**Orchid** · ground #9BBED7 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Orchid.dp`; motif rule

Display tier: high-colour


### `cde-sea-foam`

**SeaFoam** · ground #AFBFBA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SeaFoam.dp`; motif rule

Display tier: high-colour


### `cde-gray-scale`

**GrayScale** · ground #BCBCBC · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/GrayScale.dp`; motif rule

Display tier: high-colour


### `cde-olive`

**Olive** · ground #CDBDAA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Olive.dp`; motif rule

Display tier: high-colour


### `cde-soft-blue`

**SoftBlue** · ground #94D5DF · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SoftBlue.dp`; motif rule

Display tier: high-colour


### `cde-wheat`

**Wheat** · ground #94D5DF · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Wheat.dp`; motif rule

Display tier: high-colour


### `cde-summer`

**Summer** · ground #86EAE7 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Summer.dp`; motif rule

Display tier: high-colour


## GNOME 2: Clearlooks as Debian 6 squeeze shipped it (its gtkrc's colour scheme; the relief, the tooltip border and the unfocused title by the engine's and metacity's own rules)

1 entries, darkest ground first.

### `clearlooks`

**Clearlooks** · ground #EDECEB · debian-live-6.0.10-i386-gnome-desktop.iso `usr/share/themes/Clearlooks/gtk-2.0/gtkrc` + 1 more; gtk2-clearlooks rule

Display tier: high-colour


## Cool Edit Pro 2.1's color presets: the program's built-in palettes (architect 2026-10-09)

Not desktop themes and not catalog entries: the WAVEFORM's built-in palettes, compiled into the app (src/gui/palette_file.h's kGuiBuiltinPalettes, generated by tools/theme_catalog/gen_cool_edit_presets.py into src/gui/palette_presets.inc). SOURCE: the color schemes Cool Edit Pro 2.1 (Syntrillium Software) carries as text in coolpro.exe, extracted from the architect's own copy on 2026-10-09 and committed as tools/theme_catalog/cool_edit_presets.txt, in Cool Edit's own order; Default (Cool Edit's "(Default Scheme)") is the default palette under every chrome. Each sets the canvas (WvBk), the ink (WvFg), the grid (GrdL), the center line (Cntr), the cue and range colors (CueM, RngM), the playhead (Curs) and the panel's face (Face; Classic Cool records none and takes the default's); the waveform outline is the ink at HLS lightness 0.3157, and the invalid label's pair (#800000, #FF0000) and the scanner (#FFFFFF) are the same in all. Unnamed 1 and Unnamed 2 are the two schemes whose name line the extraction lacks.

| key | name | ink | canvas | face |
|---|---|---|---|---|
| `cool-edit-default` | Default | #4BF3A7 | #000000 | #626C7B |
| `cool-edit-xp-blue` | XP Blue | #22B893 | #000000 | #1C47C6 |
| `cool-edit-xp-silver` | XP Silver | #22B893 | #000000 | #7B7E8B |
| `cool-edit-xp-olive` | XP Olive | #D8B264 | #000000 | #9AA280 |
| `cool-edit-fire-and-brick` | Fire and Brick | #FFCE0C | #000000 | #8E283A |
| `cool-edit-classic-cool` | Classic Cool | #1AF0AB | #000000 | #626C7B |
| `cool-edit-lipstick-and-grapes` | Lipstick and Grapes | #ED1EC9 | #000000 | #8751CA |
| `cool-edit-seattle-blues` | Seattle Blues | #576AB4 | #000000 | #C7C8CF |
| `cool-edit-midnight` | Midnight | #4FD8D1 | #000000 | #272B6D |
| `cool-edit-periwinkle` | Periwinkle | #3565D6 | #000000 | #AEB6D2 |
| `cool-edit-cepro-1-2` | CEPro 1.2 | #1BF0AB | #000000 | #7088A0 |
| `cool-edit-unnamed-1` | Unnamed 1 | #FE0D0D | #000000 | #2F1AEE |
| `cool-edit-stealth` | Stealth | #13F774 | #000000 | #03040A |
| `cool-edit-grape-lime-and-tangerine` | Grape, Lime, and Tangerine | #83F4A2 | #073C2F | #ACA9D0 |
| `cool-edit-dusty-rose` | Dusty Rose | #B2DCA8 | #000000 | #A8969C |
| `cool-edit-nature-calls` | Nature Calls | #C7A043 | #000000 | #637461 |
| `cool-edit-arctic-freeze` | Arctic Freeze | #0DE0FE | #000000 | #C0C0C0 |
| `cool-edit-midnight-blues` | Midnight Blues | #0C55FF | #000000 | #414141 |
| `cool-edit-safari-so-good` | Safari So Good | #D9B732 | #3D2E20 | #C1B791 |
| `cool-edit-unnamed-2` | Unnamed 2 | #73BA51 | #000000 | #626C7B |
