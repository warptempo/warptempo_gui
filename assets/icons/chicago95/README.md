# Chicago95 icons (bitmap mode's icon set)

The 16-px icons the architect picked on 2026-10-05 (`mapping.md`: each button, its Chicago95 name, and every
icon's ink box and centring), copied unmodified from the Chicago95 icon theme's `<context>/16/` folders.

Provenance: Chicago95 by grassmunk, https://github.com/grassmunk/Chicago95, a recreation of the Windows 95 look
for Linux desktops. Every file here is byte-identical to `Icons/Chicago95/<context>/16/` at upstream commit
`5da19b8b1e2a886ebf6628403023d5fac3acc3ee` (master, 2026-10-01; the icon tree last changed at `8139beb5`,
2026-08-06), checked file by file against that checkout and against the architect's archive on 2026-10-06.

Licence: upstream carries NO licence file for the icon theme — no LICENSE or COPYING at its root or under
`Icons/Chicago95/` (the only licence files in the tree belong to other parts: `Icons/Chicago95-puffy/`,
`Lightdm/Chicago95/` and the LibreOffice iconset). Its one statement is the README's "Code and license" section,
read at the commit above: `License: GPL-3.0+/MIT`. The icons are conveyed here under this project's GPL v3, whose
text is the repository's `LICENSE`. Bundling is the architect's ruling for this one-user program (2026-10-05).

`object-merge.png`, Flatten's first pick, was 32 x 32 in Chicago95's own 16-px folder — no real 16-px art — and is
deleted (architect 2026-10-05, ruling 0): Flatten took `object-group.png` instead and Toggle Cumulative, which had
worn that file, took a fresh pick, `view-sort-ascending.png` (`mapping.md`'s HistoryCumulative row carries the
account). Do not edit these files: a new pick is a new copy from upstream, recorded in `mapping.md`.

`audio-volume-high.png` (architect 2026-10-05, the caption's own pick) came from Chicago95's `status/16/` rather
than `actions/16/`, and wears the caption's icon (`icons::Icon::AppIcon`) at a bitmap gui_scale instead of a roster
button's case — `mapping.md`'s last row carries it.
