# Chicago95's speaker (the program icon's one source)

`16/audio-volume-high.png` is the one file here: the program icon outside the window, the source of the Android
launcher icon (`android/app/res/mipmap-anydpi-v26/ic_launcher.xml` holds the recipe) and of the Linux desktop icon
`packaging/warptempo_gui.svg` (`tools/desktop_icon/gen_desktop_icon.py` writes it). The architect picked it on
2026-10-05 from Chicago95's `status/16/` folder. Inside the window the caption wears the product's own vector
redrawing of it (`assets/icons/warptempo/AppIcon.svg`).

Provenance: Chicago95 by grassmunk, https://github.com/grassmunk/Chicago95, a recreation of the Windows 95 look
for Linux desktops. The file is byte-identical to `Icons/Chicago95/status/16/audio-volume-high.png` at upstream
commit `5da19b8b1e2a886ebf6628403023d5fac3acc3ee` (master, 2026-10-01; the icon tree last changed at `8139beb5`,
2026-08-06), checked against that checkout and against the architect's archive on 2026-10-06.

Licence: upstream carries NO licence file for the icon theme — no LICENSE or COPYING at its root or under
`Icons/Chicago95/` (the only licence files in the tree belong to other parts: `Icons/Chicago95-puffy/`,
`Lightdm/Chicago95/` and the LibreOffice iconset). Its one statement is the README's "Code and license" section,
read at the commit above: `License: GPL-3.0+/MIT`. The file is conveyed here under this project's GPL v3, whose
text is the repository's `LICENSE`. Do not edit it: a new pick is a new copy from upstream.
