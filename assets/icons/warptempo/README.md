# warptempo's icons (the product's own set)

The product's icons, one SVG per `icons::Icon` enumerator (`src/gui/icons.h`), each file named by its enumerator
(architect 2026-10-06: "the icons go full scalable, our own set"). Drawn in the Windows 95 toolbar idiom: Windows 95
is always the source, Chicago95 only where it agrees, an uncontrovertible Microsoft case always taken; an original
is a modern symbol retrofied.

Every file is `viewBox="0 0 16 16"` — one unit is one Windows px of the 16-px toolbar cell — with `<path fill d>`
elements only (no stroke, transform, group or rect), every `d` in M L H V C A Z, every fill one of Windows' twenty
solid colours. `src/gui/icons.cpp` carries each file's paths in file order, each `d` VERBATIM with its own fill, so
a diff between the table and a file is a transcription bug and nothing else. Do not edit a file without editing
its table row the same way.

Shared drawings (one picture, two enumerators, the two files byte-identical, one table row): DialogCancel =
WindowClose; KeyframePrevious = MediaSkipBackward; KeyframeNext = MediaSkipForward; BboxPrev = GoPrevious;
BboxNext = GoNext.

## Licence

KIND: MICROSOFT = Microsoft's own 16-px art of the period redrawn (comctl32's toolbar strips, Marlett, Media
Player, Sound Recorder, Explorer 95) — redrawn as period reference from its geometry, never copied bytes;
CHICAGO95 = Chicago95's composition drawn faithfully, a Windows subject named where the subject is Windows' but
its 16-px pixels were not in hand; ORIGINAL = our own drawing, a modern symbol retrofied (its Breeze or Adwaita
name given) or a Windows idiom composed anew.

The CHICAGO95 drawings derive from Chicago95 by grassmunk (https://github.com/grassmunk/Chicago95), whose icon
theme ships no licence file; its README's "Code and license" section reads `License: GPL-3.0+/MIT`, and the
derived drawings are conveyed here under this project's GPL v3, whose text is the repository's `LICENSE`. The
ORIGINALS are this project's own, under the same GPL v3; the Breeze and Adwaita names in their rows are references
to a shape — no Breeze or Adwaita bytes remain in the tree.

## The icons

| enumerator | act | kind | source | the drawing |
|---|---|---|---|---|
| DocumentSave | Save | MICROSOFT | STD_FILESAVE, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page); Chicago95 actions/document-save agrees pixel for pixel. The label, window and slot are holes showing the button face, as WordPad shows them. | Save, Windows 95's STD_FILESAVE: label, write-protect window and shutter slot show the button face (unpainted). |
| VcsCommit | Save and Commit (Save's face in the history view) | ORIGINAL | Breeze actions/22/vcs-commit retrofied: a node on a line, black, one-unit. | Commit: Breeze vcs-commit, a node on the line. |
| VcsPull | Pull (Save's face while GitHub is ahead) | CHICAGO95 | Chicago95 actions/go-bottom (cyan arrow onto a bar); no Windows original. | Pull: no Windows 95 original; a cyan arrow onto a bar in the idiom. |
| EditUndo | Undo | MICROSOFT | STD_UNDO, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page); Chicago95 edit-undo agrees. One smooth swoop reaching the arrowhead's tip. | Undo, Windows 95's STD_UNDO: the right-triangle arrowhead and ONE smooth swoop, a single cubic whose centreline starts at the arrowhead's tip (2,9.5), control points 10.402,1.098 and 15.781,4.544, ending at 12.55,10.62; the one-unit band is drawn from 1.7 units along it (hidden inside the triangle). |
| EditRedo | Redo | MICROSOFT | STD_REDOW, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page); Chicago95 edit-redo agrees. The undo swoop mirrored. | Redo, Windows 95's STD_REDOW: Undo mirrored about the cell's vertical centre line. |
| EditCopy | Copy Resolved Value | MICROSOFT | STD_COPY, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page); Chicago95 edit-copy agrees. | Copy Resolved Value, Windows 95's STD_COPY: two dog-eared pages, navy front over black back, three text lines each. |
| MediaRecord | Render | MICROSOFT | Sound Recorder's record dot (Windows 98 Sound Recorder screenshot for the maroon; diameter 10). | Render: Sound Recorder's record dot, maroon, diameter 10, centred. |
| DialogCancel | Cancel (Render's mid-render face) | MICROSOFT | Marlett's close glyph at 10x9, two units; the same drawing as WindowClose. | Cancel: Marlett's close glyph at toolbar proportion, 10x9, two-unit bars, one unioned shape. |
| DocumentExport | Source+Warp | CHICAGO95 | Chicago95 actions/document-export (the page with the arrow leaving); no Windows original. | Source+Warp: a page with a navy title bar and a maroon arrow leaving to the right, its tail risen from a tick (no Windows 95 original; the pick's composition). |
| DocumentImport | Target+Warp | CHICAGO95 | Chicago95 actions/document-import (the arrow entering); no Windows original. | Target+Warp: the page with a maroon arrow entering leftwards, its tail dropping to a tick. |
| ChronometerStart | Target+Phase | ORIGINAL | Pick A (architect 2026-10-06): the page family (its siblings' page with the navy title bar) carrying a maroon reset bar and return arrow, Media Player's to-start idiom. No clock (ruled out). | Target+Phase A: the page with a reset bar and a return arrow (Media Player's to-start idiom): the stream snaps back to the bar. |
| ZoomFitBest | Full Zoom Out | CHICAGO95 | Chicago95 actions/zoom-fit-best (the magnifier over a fit frame); magnifiers kept by ruling. | Full Zoom Out: the magnifier (one-unit ring, diameter 12; handle black with navy flanks) with four navy corner brackets. |
| ZoomOriginal | Center on Focus | CHICAGO95 | Chicago95 actions/zoom-original (the 1:1 magnifier). | Center on Focus: the magnifier with a navy figure one (the zoom group's glass). |
| ZoomInY | Toggle Waveform Magnification | CHICAGO95 | Chicago95 actions/zoom-in (the plus magnifier). | Toggle Waveform Magnification: the magnifier with a navy plus, arms two units wide. |
| ListAdd | Drop Marker | CHICAGO95 | Chicago95 actions/list-add (the plus); row 8 is bare symbols, no flags. | Drop Marker: a plus of two-unit arms, 10x10 at (3,3). |
| ListRemove | Delete Markers | CHICAGO95 | Chicago95 actions/list-remove (the minus). | Delete Markers: a two-unit bar, 10x2 at (3,7). |
| ViewHidden | Toggle Disabled | CHICAGO95 | Chicago95 actions/action-unavailable (the red no-sign). | Toggle Disabled: a red disc in a one-unit black ring (diameter 14 at 1,1) with a white X (IDI_ERROR's subject at 16 px; the pick's composition). |
| InsertLink | Toggle Inherit | CHICAGO95 | Chicago95 actions/insert-link as it composes the chain: two flat links off the cell's edges, the edge-on middle link, the maroon arrow; no Windows 16-px link in hand (the STD strip has none, Office 97's is not in hand). | Toggle Inherit: the chain as Chicago95 composes it: two flat links running off the cell's edges (three-unit tubes: black, silver, black, with holes), the middle edge-on link fully on screen, the maroon arrow below. No Windows 16-px link exists in the strips or captures in hand. |
| Merge | Flatten | ORIGINAL | Breeze actions/22/merge retrofied: two tracks joining into one, black, one-unit. | Flatten: Breeze merge, two tracks joining into one. |
| BlackSum | Toggle Cumulative | ORIGINAL | Breeze black_sum retrofied: the summation sigma, one unit. | Toggle Cumulative: the AutoSum sigma, one-unit bars and zigzag, one unioned shape, 10x11 at (3,2). |
| GoJump | Toggle Follow | ORIGINAL | Breeze go-jump / Adwaita go-last reading retrofied: an arrow crossing a sheet rightwards (the view follows); no cycle. | Toggle Follow: a sheet of paper with a green arrow crossing it rightwards (the view follows), Windows' page construction; no cycle, since follow does not loop. |
| TimelineLift | Toggle Restrict Undo | CHICAGO95 | Chicago95 actions/view-pin drawn faithfully (teal and cyan, point left); the head a true ellipse; no Windows canonical push-pin in hand. | Toggle Restrict Undo: Chicago95's push-pin, point left: grey-tipped black needle, the head a true ellipse (rx 2.5, ry 3.5: a disc seen at an angle) black-ringed and teal with a cyan upper highlight and an inner shadow toward the barrel, the teal barrel with a cyan top stripe. 11x7 at (2,4). |
| MusicNote16th | BPM Iterations | CHICAGO95 | Chicago95 devices/music-player. | BPM Iterations: a teal music player with a navy display carrying a white trace and three keys (no Windows 95 original; the pick's off-palette greys replaced by the 20 solids). |
| Mathmode | Toggle Grid Iterations | CHICAGO95 | Chicago95 actions/view-grid. | Toggle Grid Iterations: a navy window with a two-unit title bar and six black squares in a grid (no Windows 95 original). |
| PreviewRenderOn | Play Renders | ORIGINAL | A Windows idiom composed anew: Media Player's window (navy title bar) carrying the transport's play triangle (pick B, 2026-10-06); never the bare transport triangle. | Play Renders B: Media Player's window (navy title bar) with a play triangle. |
| DialogOkApply | Load in Place | MICROSOFT | Marlett's check mark. | Load in Place: a check mark, Marlett's menu check idiom, a bent stroke 2.8 units wide. |
| VcsDiff | Toggle History View | CHICAGO95 | Windows 98's History subject (a folder with a clock) as Chicago95 actions/document-open-recent composes it, the clock drawn as a sundial (a ringed dial with a gnomon fin; unproven on glass). | Toggle History View: the folder (the family's two-row tab) with a sundial: a black-ringed white dial carrying a black gnomon fin with an olive face. The sundial is unproven on glass. |
| ShallowHistory | Toggle History Walk | ORIGINAL | Breeze actions/22/shallow-history retrofied: a clock face, black ring, white dial, one-unit hands. | History Walk: Breeze shallow-history, a clock face. |
| EditSelect | Toggle Add to Selection | CHICAGO95 | Chicago95 actions/edit-select (the pointer with a plus). | Toggle Add to Selection: Windows' arrow pointer with a plus at its lower right (add to the selection), 15x14 at (0,1). |
| KeyframePrevious | Older (the history step) | MICROSOFT | Media Player's skip-back glyph (the ToastyTech multimedia capture for geometry); the same drawing as MediaSkipBackward. | Older / Go to Start: a two-unit bar and two triangles pointing left, 12x9 at (2,3), Media Player's idiom. |
| KeyframeNext | Newer (the history step) | MICROSOFT | Media Player's skip-forward glyph; the same drawing as MediaSkipForward. | Newer / Go to End: the mirror. |
| GoPrevious | Left (row 8's arrow) | CHICAGO95 | Chicago95 actions/go-previous (the cyan-filled left arrow); the same drawing as BboxPrev. | Previous Marker / Left: a cyan arrow pointing left, black one-unit outline, 14x14 at (1,1), centred (no Windows 95 original). |
| GoNext | Right (row 8's arrow) | CHICAGO95 | Chicago95 actions/go-next; the same drawing as BboxNext. | Next Marker / Right: the exact mirror. |
| DocumentRevert | Revert | CHICAGO95 | Chicago95 actions/document-revert (the page with the arrow curving back). | Revert: a page with a navy title bar and a maroon bent arrow returning left (no Windows 95 original; the pick's composition). |
| MediaSkipBackward | Go to Start | MICROSOFT | Media Player's skip-back glyph (bar and triangle). | Older / Go to Start: a two-unit bar and two triangles pointing left, 12x9 at (2,3), Media Player's idiom. |
| MediaPlaybackStart | Play | MICROSOFT | Media Player's play triangle. | Play Renders: the transport's play triangle, 9x9 at (3,3), Media Player's idiom. |
| MediaPlaybackStop | Stop | MICROSOFT | Media Player's stop square, 8x8. | Stop: the transport's 8x8 square at (4,4). |
| MediaPlaybackPause | Pause (the render player) | MICROSOFT | Media Player's pause bars, 6x8. | Pause: two 2x8 bars, 6x8 at (5,4). |
| MediaSkipForward | Go to End | MICROSOFT | Media Player's skip-forward glyph. | Newer / Go to End: the mirror. |
| GoDown | Down (row 8's arrow) | CHICAGO95 | Chicago95 actions/go-down. | Down: Up flipped about y = 8. |
| GoUp | Up (row 8's arrow) | CHICAGO95 | Chicago95 actions/go-up. | Up: the cyan registered one unit inside the outline everywhere (the barbs' inner line at y 7). |
| Lock | Toggle Read-Only (locked) | CHICAGO95 | Internet Explorer's padlock subject as Chicago95 actions/lock composes it (gold body, black shackle). | Read-Only, locked: the shackle's black now a band curving with the gold over the right half. |
| Unlock | Toggle Read-Only (unlocked) | CHICAGO95 | Chicago95 status/stock_lock-open; the shackle's legs reach the body. | Read-Only, unlocked: the shackle swung left, its legs reaching the body's top at y 6. |
| BboxPrev | Previous Marker | CHICAGO95 | Chicago95 actions/go-previous: the architect's 2026-10-05 pick for the walk, the same drawing as GoPrevious. | Previous Marker / Left: a cyan arrow pointing left, black one-unit outline, 14x14 at (1,1), centred (no Windows 95 original). |
| BboxNext | Next Marker | CHICAGO95 | Chicago95 actions/go-next; the same drawing as GoNext. | Next Marker / Right: the exact mirror. |
| TabDetach | Switch Tab | CHICAGO95 | Chicago95 actions/view-paged. | Switch Tab: two overlapping dog-eared pages, black one-unit frames, 14x16 at (1,0). |
| SettingsConfigure | Settings | MICROSOFT | STD_PROPERTIES, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page): the pointing hand over a sheet with two dashed lines (the strip's clipped navy object omitted). | Settings: STD_PROPERTIES retrofied: a pointing hand over a sheet with two dashed lines (the strip's clipped navy object at its right omitted). |
| Folder | a folder row (the folder overlay, the project picker) | CHICAGO95 | shell32's closed folder as Chicago95 places/folder composes it (grey outline, yellow face for the dither, olive inner edge, black shadow); the tab two interior rows. | The folder row: the shell's closed folder; the tab two interior rows (y 0..3), the body y 3..14, the drop shadow to 16. |
| AudioXWav | a wav row | CHICAGO95 | The Wave Sound document (mmsys) as Chicago95 mimetypes/audio-x-wav composes it: the page with the speaker over its left edge, from the pick's pixels. | The wav row from Chicago95's mimetypes/audio-x-wav (the Wave Sound document): the page with the speaker drawn over its left edge at the pick's own small geometry (bell rows 3-12, a 3-wide cylinder, short rays), the overlap resolved by layering; whole-unit lines, no scaling. |
| MediaRepeatSingle | Toggle Repeat One (the render player) | CHICAGO95 | Chicago95 status/media-playlist-repeat. | Toggle Repeat One: the loop's corners now rounded (radius 2), one-unit band, the arrowhead in the floor's gap; 12x9 at (2,3). |
| GoParentFolder | Up a Folder (the render player) | MICROSOFT | Explorer 95's VIEW_PARENTFOLDER (the folder with the bent arrow; ToastyTech's Explorer capture for geometry, colours Windows' own): the Microsoft rule closes the choice. | Up a Folder: Explorer 95's VIEW_PARENTFOLDER, the folder with the bent arrow; the tab two interior rows (y 0..3); the arrow ONE polygon. |
| DialogInformation | a NORMAL card's glyph | CHICAGO95 | user32's information subject as Chicago95 status/dialog-information composes it: a white speech bubble with a blue serif i. | The NORMAL card: the balloon's outline grey on the light side and black on the shadow side, a grey drop shadow along the right that continues under the point, a blue serif i. |
| DialogError | a CRITICAL card's glyph | CHICAGO95 | user32's error subject as Chicago95 status/dialog-error composes it: a red disc, maroon ring, white X. | The CRITICAL card: the disc centred at (7.5, 7.5) so its ring is whole at the top; grey drop shadow; white X unioned. |
| WindowClose | Close (the render player) | MICROSOFT | Marlett's close glyph at 10x9, two units; the same drawing as DialogCancel. | Cancel: Marlett's close glyph at toolbar proportion, 10x9, two-unit bars, one unioned shape. |
| HelpWhatsthis | Toggle Tooltips | MICROSOFT | STD_HELP, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page): the pointer with the navy three-unit question mark; Chicago95 help-hint agrees. | Toggle Tooltips: Windows' STD_HELP; the question mark now ONE continuous three-unit stroke from its left arm over the top and down to the stem, plus the dot. |
| GoJumpDeclaration | Jump to Defining Marker | ORIGINAL | Pick A (architect 2026-10-06): a jump arc from a foot on the left to a head coming down on the right; black, one unit. No modern source. | Jump to Defining Marker, candidate A: a jump arc from a foot on the left over to a head coming down on the right. |
| EditDelete | Delete Folder (the render player) | MICROSOFT | STD_DELETE, Windows 95 comctl32 toolbar strip (tb-stdimages, Microsoft's image-index page): the X as two tapering pen strokes, black. | Delete Folder: Windows' STD_DELETE read as two tapering ink-pen strokes, each thick at the left and thin at the right, black, unioned. |
| AppIcon | the caption's icon | CHICAGO95 | Sound Recorder's caption speaker (the ToastyTech multimedia capture) as Chicago95 status/audio-volume-high composes it, from the pick's pixels. | The caption's speaker from Chicago95's status/audio-volume-high (Sound Recorder's subject): olive upper rim, black lower rim over the rim's olive underside, yellow face with a white band under the rim, the lip white / silver / grey, a black throat, a 4-wide cylinder with dark caps, silver cap bands and left edge, white body, grey right edge and the cone's crescent; three straight grey rays at the pixels' rows. Ink 16x16. |

58 icons. Counts: MICROSOFT 19, CHICAGO95 31, ORIGINAL 8.
