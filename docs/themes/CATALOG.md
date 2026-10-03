# The theme catalog

Every entry below is a desktop theme of the era IMPORTED, not designed (architect 2026-10-03: "no derived, imported only"): its colours are the bytes its source records, each with its provenance in [catalog.json](catalog.json); where the source records only base colours and its own toolkit computed the relief at run time (KDE 3, CDE / Motif), that toolkit's rule ran once at import and is named. The KEY is what to type in Settings to pick it. Each crop is the app rendered in the theme (tools/palette in its tablet geometry: the tablet's 2304 x 1440 at gui_scale 275, every length derived from the app's own constants; cropped, never scaled; tools/theme_catalog/crops.py): the top strip in two halves over the well's bottom lines and the bottom row, transparent between them, at the theme's LIGHT level (the app's `theme_level`; dim and dark are the generated table's, tools/theme_catalog/levels.py). The chrome is the theme's; the waveform pane, the flags and the playhead are the program's own elements in the app's default colours (architect 2026-10-03): the well keeps the app's two-line sunken edge (the theme's Shadow and DkShadow above, its 3DLight and Hilight below); the flags are the flat Acid flag, the face the app's purple with its recorded black label and a one-px outline in the theme's DkShadow, the stem leaving the face across the bottom outline, shown left to right editing (the in-place editor on the theme's field ground, its text in the selected pair), selected (a white outline round the box), invalid (#BB575A, black label), disabled (the ground, the label embossed) and unselected; the playhead's #8B8B8B head carries a one-px outline in the theme's label over its #FCFCFC stem; disabled words and glyphs are Windows' emboss; the ruler label and the trim arrow are the theme's label, the ruler ticks its Shadow. The DISPLAY TIER is the smallest period colour set holding every colour the entry's roles use: vga (the 16 VGA colours), windows-20 (those and Windows' four static extras #C0DCC0, #A6CAF0, #FFFBF0, #A0A0A4, always solid on a 256-colour display), else high-colour. Not imported: catalog.json's `not_imported`. Built by `tools/theme_catalog/` (fetch.py, build.py, crops.py).

## Windows: the Appearance schemes (ReactOS hivedef.inf, corroborated by the Windows XP classic schemes saved as .theme files; Windows 95 Standard hand-recorded)

19 entries, darkest ground first.

### `windows-rainy-day`

**Rainy Day** · ground #8399B1 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-rainy-day](crops/windows-rainy-day.png)

### `windows-plum`

**Plum** · ground #A89890 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-plum](crops/windows-plum.png)

### `windows-eggplant`

**Eggplant** · ground #90B0A8 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-eggplant](crops/windows-eggplant.png)

### `windows-lilac`

**Lilac** · ground #AEA8D9 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-lilac](crops/windows-lilac.png)

### `windows-slate`

**Slate** · ground #9DB9C8 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-slate](crops/windows-slate.png)

### `windows-marine`

**Marine** · ground #88C0B8 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-marine](crops/windows-marine.png)

### `windows-rose`

**Rose** · ground #CFAFB7 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-rose](crops/windows-rose.png)

### `windows-brick`

**Brick** · ground #C2BFA5 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-brick](crops/windows-brick.png)

### `windows-spruce`

**Spruce** · ground #A2C8A9 · zkedem/windows10-classic-themes@f126b0b1 `spruce.theme` + 1 more

Display tier: high-colour

![windows-spruce](crops/windows-spruce.png)

### `windows-storm`

**Storm** · ground #C0C0C0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: vga

![windows-storm](crops/windows-storm.png)

### `windows-teal`

**Teal** · ground #C0C0C0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: vga

![windows-teal](crops/windows-teal.png)

### `windows-red-white-and-blue`

**Red, White, and Blue** · ground #C0C0C0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: vga

![windows-red-white-and-blue](crops/windows-red-white-and-blue.png)

### `windows-standard`

**Windows Standard** · ground #C0C0C0 · zkedem/windows10-classic-themes@f126b0b1 `classic.theme` + 2 more

Display tier: high-colour

![windows-standard](crops/windows-standard.png)

### `windows-95-standard`

**Windows 95 Standard** · ground #C0C0C0 · hand-recorded (architect 2026-10-03) + 1 more

Display tier: high-colour

![windows-95-standard](crops/windows-95-standard.png)

### `windows-desert`

**Desert** · ground #D5CCBB · zkedem/windows10-classic-themes@f126b0b1 `desert.theme` + 1 more

Display tier: high-colour

![windows-desert](crops/windows-desert.png)

### `windows-classic`

**Windows Classic** · ground #D4D0C8 · zkedem/windows10-classic-themes@f126b0b1 `standard.theme` + 2 more

Display tier: high-colour

![windows-classic](crops/windows-classic.png)

### `windows-pumpkin`

**Pumpkin** · ground #ECD59D · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-pumpkin](crops/windows-pumpkin.png)

### `windows-maple`

**Maple** · ground #E6D8AE · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-maple](crops/windows-maple.png)

### `windows-wheat`

**Wheat** · ground #DEDEA0 · reactos/reactos@d004b2c1 `boot/bootdata/hivedef.inf` + 2 more

Display tier: high-colour

![windows-wheat](crops/windows-wheat.png)

## Windows 98 / Plus! desktop themes (the shipped .theme files)

16 entries, darkest ground first.

### `plus-underwater`

**Underwater (high color)** · ground #3868C8 · 1j01/98@52451052 `desktop/Themes/Windows Official/Underwater (high color).theme`

Display tier: high-colour

![plus-underwater](crops/plus-underwater.png)

### `plus-dangerous-creatures`

**Dangerous Creatures (256 color)** · ground #707070 · 1j01/98@52451052 `desktop/Themes/Windows Official/Dangerous Creatures (256 color).theme`

Display tier: high-colour

![plus-dangerous-creatures](crops/plus-dangerous-creatures.png)

### `plus-mystery`

**Mystery (high color)** · ground #687868 · 1j01/98@52451052 `desktop/Themes/Windows Official/Mystery (high color).theme`

Display tier: high-colour

![plus-mystery](crops/plus-mystery.png)

### `plus-travel`

**Travel (high color)** · ground #908070 · 1j01/98@52451052 `desktop/Themes/Windows Official/Travel (high color).theme`

Display tier: high-colour

![plus-travel](crops/plus-travel.png)

### `plus-space`

**Space (256 color)** · ground #809098 · 1j01/98@52451052 `desktop/Themes/Windows Official/Space (256 color).theme`

Display tier: high-colour

![plus-space](crops/plus-space.png)

### `plus-the-60s-usa`

**The 60's USA (256 color)** · ground #D068D8 · 1j01/98@52451052 `desktop/Themes/Windows Official/The 60's USA (256 color).theme`

Display tier: high-colour

![plus-the-60s-usa](crops/plus-the-60s-usa.png)

### `plus-science`

**Science (256 color)** · ground #8399B1 · 1j01/98@52451052 `desktop/Themes/Windows Official/Science (256 color).theme`

Display tier: high-colour

![plus-science](crops/plus-science.png)

### `plus-more-windows`

**More Windows (high color)** · ground #9098A0 · 1j01/98@52451052 `desktop/Themes/Windows Official/More Windows (high color).theme`

Display tier: high-colour

![plus-more-windows](crops/plus-more-windows.png)

### `plus-jungle`

**Jungle (256 color)** · ground #B8A068 · 1j01/98@52451052 `desktop/Themes/Windows Official/Jungle (256 color).theme`

Display tier: high-colour

![plus-jungle](crops/plus-jungle.png)

### `plus-leonardo-da-vinci`

**Leonardo da Vinci (256 color)** · ground #BFA59F · 1j01/98@52451052 `desktop/Themes/Windows Official/Leonardo da Vinci (256 color).theme`

Display tier: high-colour

![plus-leonardo-da-vinci](crops/plus-leonardo-da-vinci.png)

### `plus-baseball`

**Baseball (256 color)** · ground #D0A870 · 1j01/98@52451052 `desktop/Themes/Windows Official/Baseball (256 color).theme`

Display tier: high-colour

![plus-baseball](crops/plus-baseball.png)

### `plus-inside-your-computer`

**Inside your Computer (high color)** · ground #A8C8A8 · 1j01/98@52451052 `desktop/Themes/Windows Official/Inside your Computer (high color).theme`

Display tier: high-colour

![plus-inside-your-computer](crops/plus-inside-your-computer.png)

### `plus-windows-98`

**Windows 98 (256 color)** · ground #B4C3DC · 1j01/98@52451052 `desktop/Themes/Windows Official/Windows 98 (256 color).theme`

Display tier: high-colour

![plus-windows-98](crops/plus-windows-98.png)

### `plus-nature`

**Nature (high color)** · ground #D8C0A0 · 1j01/98@52451052 `desktop/Themes/Windows Official/Nature (high color).theme`

Display tier: high-colour

![plus-nature](crops/plus-nature.png)

### `plus-the-golden-era`

**The Golden Era (high color)** · ground #B8C8B8 · 1j01/98@52451052 `desktop/Themes/Windows Official/The Golden Era (high color).theme`

Display tier: high-colour

![plus-the-golden-era](crops/plus-the-golden-era.png)

### `plus-sports`

**Sports (256 color)** · ground #B0E0A0 · 1j01/98@52451052 `desktop/Themes/Windows Official/Sports (256 color).theme`

Display tier: high-colour

![plus-sports](crops/plus-sports.png)

## KDE 3.5 colour schemes, as Trinity's tdebase carries them (relief by KDE 3's own rule at each scheme's contrast)

25 entries, darkest ground first.

### `kde3-dark-blue`

**Dark Blue** · ground #426794 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/DarkBlue.kcsrc`; kde3 rule at contrast 8

Display tier: high-colour

![kde3-dark-blue](crops/kde3-dark-blue.png)

### `kde3-digital-cde`

**Digital CDE** — imitates CDE (Digital UNIX) · ground #4B7B82 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/DigitalCDE.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-digital-cde](crops/kde3-digital-cde.png)

### `kde3-cde`

**CDE** — imitates CDE · ground #999999 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/CDE.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-cde](crops/kde3-cde.png)

### `kde3-next`

**Next** — imitates NeXTSTEP · ground #A8A8A8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Next.kcsrc`; kde3 rule at contrast 10

Display tier: high-colour

![kde3-next](crops/kde3-next.png)

### `kde3-atlas-green`

**Atlas Green** · ground #AFB49F · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/AtlasGreen.kcsrc`; kde3 rule at contrast 5

Display tier: high-colour

![kde3-atlas-green](crops/kde3-atlas-green.png)

### `kde3-solaris`

**Solaris** — imitates CDE (Solaris) · ground #AEB2C3 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/SolarisCDE.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour

![kde3-solaris](crops/kde3-solaris.png)

### `kde3-blue-slate`

**Blue Slate** · ground #9DB9C8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/BlueSlate.kcsrc`; kde3 rule at contrast 5

Display tier: high-colour

![kde3-blue-slate](crops/kde3-blue-slate.png)

### `kde3-kde-1`

**KDE 1** · ground #C0C0C0 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KDEOne.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-kde-1](crops/kde3-kde-1.png)

### `kde3-storm`

**Storm** · ground #C0C0C0 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Storm.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-storm](crops/kde3-storm.png)

### `kde3-redmond-95`

**Redmond 95** — imitates Windows 95 · ground #C3C3C3 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Windows95.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-redmond-95](crops/kde3-redmond-95.png)

### `kde3-point-reyes-green`

**Point Reyes Green** · ground #D3C5BE · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/PointReyesGreen.kcsrc`; kde3 rule at contrast 0

Display tier: high-colour

![kde3-point-reyes-green](crops/kde3-point-reyes-green.png)

### `kde3-desert-red`

**Desert Red** · ground #D6CDBB · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/DesertRed.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour

![kde3-desert-red](crops/kde3-desert-red.png)

### `kde3-redmond-2000`

**Redmond 2000** — imitates Windows 2000 · ground #D4D0C8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Windows2000.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-redmond-2000](crops/kde3-redmond-2000.png)

### `kde3-system`

**System** · ground #D3D3D3 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/System.kcsrc`; kde3 rule at contrast 2

Display tier: high-colour

![kde3-system](crops/kde3-system.png)

### `kde3-pale-gray`

**Pale Gray** · ground #D6D6D6 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/PaleGray.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour

![kde3-pale-gray](crops/kde3-pale-gray.png)

### `kde3-beos`

**BeOS** — imitates BeOS · ground #D9D9D9 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/BeOS.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-beos](crops/kde3-beos.png)

### `kde3-pumpkin`

**Pumpkin** · ground #EED8AE · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Pumpkin.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour

![kde3-pumpkin](crops/kde3-pumpkin.png)

### `kde3-kde-2`

**KDE 2** · ground #DCDCDC · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KDETwo.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-kde-2](crops/kde3-kde-2.png)

### `kde3-media-peach`

**Media Peach** · ground #F4DDB2 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/MediaPeach.kcsrc`; kde3 rule at contrast 3

Display tier: high-colour

![kde3-media-peach](crops/kde3-media-peach.png)

### `kde3-evex`

**EveX** · ground #E6DEDC · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/EveX.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-evex](crops/kde3-evex.png)

### `kde3-keramik-white`

**Keramik White** · ground #E9E9E9 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KeramikWhite.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-keramik-white](crops/kde3-keramik-white.png)

### `kde3-keramik`

**Keramik** · ground #EAE9E8 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Keramik.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-keramik](crops/kde3-keramik.png)

### `kde3-keramik-emerald`

**Keramik Emerald** · ground #EEEEE6 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/KeramikEmerald.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-keramik-emerald](crops/kde3-keramik-emerald.png)

### `kde3-redmond-xp`

**Redmond XP** — imitates Windows XP · ground #EEEEE6 · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/WindowsXP.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-redmond-xp](crops/kde3-redmond-xp.png)

### `kde3-plastik`

**Plastik** · ground #EFEFEF · TDE/tdebase@d66254bc `kcontrol/krdb/kcs/Plastik.kcsrc`; kde3 rule at contrast 7

Display tier: high-colour

![kde3-plastik](crops/kde3-plastik.png)

## CDE palettes (colour set 5 the ground; foreground and shadows by Motif's own rule)

36 entries, darkest ground first.

### `cde-northern-sky`

**NorthernSky** · ground #41525C · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/NorthernSky.dp`; motif rule

Display tier: high-colour

![cde-northern-sky](crops/cde-northern-sky.png)

### `cde-cinnamon`

**Cinnamon** · ground #9B6862 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Cinnamon.dp`; motif rule

Display tier: high-colour

![cde-cinnamon](crops/cde-cinnamon.png)

### `cde-cabernet`

**Cabernet** · ground #B65C71 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Cabernet.dp`; motif rule

Display tier: high-colour

![cde-cabernet](crops/cde-cabernet.png)

### `cde-neptune`

**Neptune** · ground #637E95 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Neptune.dp`; motif rule

Display tier: high-colour

![cde-neptune](crops/cde-neptune.png)

### `cde-golden`

**Golden** · ground #757CA6 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Golden.dp`; motif rule

Display tier: high-colour

![cde-golden](crops/cde-golden.png)

### `cde-south-west`

**SouthWest** · ground #B86F4B · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SouthWest.dp`; motif rule

Display tier: high-colour

![cde-south-west](crops/cde-south-west.png)

### `cde-mustard`

**Mustard** · ground #887CA3 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Mustard.dp`; motif rule

Display tier: high-colour

![cde-mustard](crops/cde-mustard.png)

### `cde-charcoal`

**Charcoal** · ground #828585 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Charcoal.dp`; motif rule

Display tier: high-colour

![cde-charcoal](crops/cde-charcoal.png)

### `cde-urchin`

**Urchin** · ground #3792A2 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Urchin.dp`; motif rule

Display tier: high-colour

![cde-urchin](crops/cde-urchin.png)

### `cde-sand`

**Sand** · ground #AF8181 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Sand.dp`; motif rule

Display tier: high-colour

![cde-sand](crops/cde-sand.png)

### `cde-camouflage`

**Camouflage** · ground #8E8E77 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Camouflage.dp`; motif rule

Display tier: high-colour

![cde-camouflage](crops/cde-camouflage.png)

### `cde-sky-red`

**SkyRed** · ground #6E91AA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SkyRed.dp`; motif rule

Display tier: high-colour

![cde-sky-red](crops/cde-sky-red.png)

### `cde-arizona`

**Arizona** · ground #C37D79 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Arizona.dp`; motif rule

Display tier: high-colour

![cde-arizona](crops/cde-arizona.png)

### `cde-beige-rose`

**BeigeRose** · ground #919191 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/BeigeRose.dp`; motif rule

Display tier: high-colour

![cde-beige-rose](crops/cde-beige-rose.png)

### `cde-tundra`

**Tundra** · ground #859680 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Tundra.dp`; motif rule

Display tier: high-colour

![cde-tundra](crops/cde-tundra.png)

### `cde-clay`

**Clay** · ground #AF9191 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Clay.dp`; motif rule

Display tier: high-colour

![cde-clay](crops/cde-clay.png)

### `cde-crimson`

**Crimson** · ground #68A6A0 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Crimson.dp`; motif rule

Display tier: high-colour

![cde-crimson](crops/cde-crimson.png)

### `cde-alpine`

**Alpine** · ground #BF9279 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Alpine.dp`; motif rule

Display tier: high-colour

![cde-alpine](crops/cde-alpine.png)

### `cde-pbnj`

**PBNJ** · ground #C69076 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/PBNJ.dp`; motif rule

Display tier: high-colour

![cde-pbnj](crops/cde-pbnj.png)

### `cde-santa-fe`

**SantaFe** · ground #83A5B6 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SantaFe.dp`; motif rule

Display tier: high-colour

![cde-santa-fe](crops/cde-santa-fe.png)

### `cde-nutmeg`

**Nutmeg** · ground #B39C8F · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Nutmeg.dp`; motif rule

Display tier: high-colour

![cde-nutmeg](crops/cde-nutmeg.png)

### `cde-lilac`

**Lilac** · ground #A5A5AC · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Lilac.dp`; motif rule

Display tier: high-colour

![cde-lilac](crops/cde-lilac.png)

### `cde-chocolate`

**Chocolate** · ground #B9A0A0 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Chocolate.dp`; motif rule

Display tier: high-colour

![cde-chocolate](crops/cde-chocolate.png)

### `cde-dark-gold`

**DarkGold** · ground #A8A8A8 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/DarkGold.dp`; motif rule

Display tier: high-colour

![cde-dark-gold](crops/cde-dark-gold.png)

### `cde-grass`

**Grass** · ground #A4A9AA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Grass.dp`; motif rule

Display tier: high-colour

![cde-grass](crops/cde-grass.png)

### `cde-delphinium`

**Delphinium** · ground #78B4BE · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Delphinium.dp`; motif rule

Display tier: high-colour

![cde-delphinium](crops/cde-delphinium.png)

### `cde-desert`

**Desert** · ground #9FAEB5 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Desert.dp`; motif rule

Display tier: high-colour

![cde-desert](crops/cde-desert.png)

### `cde-default`

**Default** · ground #C6B2A8 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Default.dp`; motif rule

Display tier: high-colour

![cde-default](crops/cde-default.png)

### `cde-savannah`

**Savannah** · ground #C6B2A8 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Savannah.dp`; motif rule

Display tier: high-colour

![cde-savannah](crops/cde-savannah.png)

### `cde-orchid`

**Orchid** · ground #9BBED7 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Orchid.dp`; motif rule

Display tier: high-colour

![cde-orchid](crops/cde-orchid.png)

### `cde-sea-foam`

**SeaFoam** · ground #AFBFBA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SeaFoam.dp`; motif rule

Display tier: high-colour

![cde-sea-foam](crops/cde-sea-foam.png)

### `cde-gray-scale`

**GrayScale** · ground #BCBCBC · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/GrayScale.dp`; motif rule

Display tier: high-colour

![cde-gray-scale](crops/cde-gray-scale.png)

### `cde-olive`

**Olive** · ground #CDBDAA · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Olive.dp`; motif rule

Display tier: high-colour

![cde-olive](crops/cde-olive.png)

### `cde-soft-blue`

**SoftBlue** · ground #94D5DF · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/SoftBlue.dp`; motif rule

Display tier: high-colour

![cde-soft-blue](crops/cde-soft-blue.png)

### `cde-wheat`

**Wheat** · ground #94D5DF · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Wheat.dp`; motif rule

Display tier: high-colour

![cde-wheat](crops/cde-wheat.png)

### `cde-summer`

**Summer** · ground #86EAE7 · ThomasAdam/cdesktopenv@9b1f60a7 `cde/programs/palettes/Summer.dp`; motif rule

Display tier: high-colour

![cde-summer](crops/cde-summer.png)

## Warptempo: the app's own look as it stands on 2026-10-03

1 entries, darkest ground first.

### `warptempo-2026-10-03`

**Warptempo 2026-10-03** · ground #303030 · Warptempo GUI@da0b1051 `src/gui/render.h`

Display tier: high-colour

![warptempo-2026-10-03](crops/warptempo-2026-10-03.png)

