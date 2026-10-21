# warptempo_gui

warptempo_gui time-warps a recording toward the tempos you choose. It was built for taking commercial orchestral recordings to historically informed tempos, phrase by phrase, by ear.

The engine is a PGHI phase vocoder (Prusa and Holighaus's phase-gradient heap integration, "Phase Vocoder Done Right") run at the authors' own parameters, N = 4096 with 75 % overlap, at the source's own sample rate, and extended in two ways: the analysis hop varies frame by frame with the warp map, and phase resets are placed by hand. Those two and its other departures from the paper (the output timing contract, the pinned randomness of quiet bins) are deliberate and recorded in the engine's own comments. A render is a finished 24-bit WAV through an unconditional limiter, and it is deterministic: the same project on the same machine and build renders the same bytes every time.

There are two products over one interface: the desktop GUI (Wayland and JACK) and the same GUI as an Android APK; a headless CLI renders the same bytes as the GUI. Installing, building, the first run, the tablet and all upkeep are in [`INSTALL.md`](INSTALL.md).

The interface documents itself. With the tooltip lamp lit (Toggle Tooltips, the bare `\`, in the icon row), resting the pointer over any button, or hovering the S Pen over it, names its act and its key. This guide therefore names acts, never keys, and holds only what the tooltips cannot show: what the markers mean and how a movement is worked.

## How the program says no

When a key or a click refuses, the program says why in a card at the top right of the window. Silence means the press was unbound, or a command that acts in one place was already at its state there. So this guide lists no refusals: the program tells you at the moment it matters. A field that refuses what was entered also selects its whole text, so the next keystroke replaces it.

A card leaves on its own after a few seconds, waiting while the pointer rests on it or a press holds it; a few failures, such as a checkpoint that did not reach GitHub, stay until dismissed. A click or tap anywhere on a card dismisses it, and a Shift-click or a long press on any card dismisses them all, as Esc does on the keyboard.

## The concepts

### Warp markers

A warp marker sets the playback rate from its position until the next marker's. It carries a tempo, a stretch ratio in which 1.00 is the recording as it was made. The tempo may be written as a main speed plus a few signed adjustments that add up to it, so a section's headline speed stays visible beside the corrections made to it, and a marker may carry a fine scale on top, for matching a neighbouring section's effective tempo. A disabled marker contributes nothing, as if it were not there. Positions are authored in source view, the unprocessed recording, where a marker is pinned against an acoustic event: a little ahead of it, in the tail of what precedes, a far steadier cue than the silence before an attack. Tempos are judged in target view, the recording as it will render.

### Inheritance

A marker with no tempo of its own, a pass, takes the tempo of the nearest earlier marker that owns one; disabled markers and label references are passed over, and a pass with no owner before it plays as recorded. The resolution is live: disable or delete the owner and every pass after it re-resolves to the next owner back.

### Label definitions and references

This is the program's reason to exist. A marker can define a named label, and a later marker can reference it. The reference renders its own segment at whatever tempo gives that segment the definition's duration, so editing the definition moves every reference with it; disabling the definition disables its references too. That is what ties a recapitulation to its exposition, or a repeat to its first time, through every later edit. A reference is confirmed against its definition by ear with the A/B audition: with one tab parked at the definition and the other at the reference, it plays the same short span from each tab's playhead, back to back.

### Phase resets

Phase resets are a separate collection of markers, authored in either audio view: in target view, where they are protected by ear, and in source view, where they are transcribed against the recording's own attacks. The vocoder's frame-to-frame phase coherence keeps sustained sound clean but smears an attack; at a phase reset it re-seats its phase instead, so the attack (an entry, a pizzicato, a timpani stroke) arrives clean. A reset belongs a lead-in ahead of its transient, and the drop does that arithmetic for you in either view, taking the lead-in once; a drag or a nudge moves a reset exactly where it is put, and the lead-in's dotted outline is drawn in target view alone. Each one trades a little local coherence for the clean attack, so they are placed one by one, and nudged only when a dip, pop or crackle is heard.

### Two tabs, four views

The two tabs, A and B, are two viewpoints (viewport, zoom, playhead and trim) over one shared set of markers, so one can sit at a definition while the other sits at its reference. The strip of tabs under the toolbar names them, Cool Edit's Organizer tabs: the lit tab is the active one, and a press on the other tab switches to it, as Switch Tab in the bottom row and Ctrl+Tab do; a Shift-press or a long press on the other tab walks both tabs, stepping each to its next marker and centering it, and comes back to the tab it started from (Ctrl+Shift+Tab). The view is chosen by two pairs of buttons at the right end of the icon row, each pair a radio with one member down: Source and Target (the bare keys 1 and 2) choose the audio, Warp and Phase Reset (3 and 4) the markers, each key changing its own half and leaving the other alone. Their four combinations are the four views: source + warp, where warp markers are placed; source + phase reset, where resets are transcribed against the recording; target + warp, where tempos are judged; and target + phase reset, where the transients are protected.

Save is the unsaved-work mark: it lights when the markers or the piece's settings block (the scale, the BPM and the rest) change, and is dimmed again once saved or undone back to the saved state. The tabs' viewpoints, their trims and their read-only locks never light it; they are written with the next save of such a change, and a trim or lock changed on its own is not kept.

### History

A piece's checkpoints are git commits in the projects repository. The tablet authors and commits (`Ctrl+Shift+S`, Save and Commit, in the main window or the history view, under the title `Update <piece>` with nothing asked; it pulls instead when GitHub is ahead), GitHub is the hub, and the laptop pulls (the same key there; each device's config names its role, author or mirror). `Ctrl+S` is the plain save everywhere. The history view walks the checkpoints and shows each as a diff of flags, from which a difference can be reverted or a whole checkpoint loaded in place; the mechanics are in [`INSTALL.md`](INSTALL.md)'s Daily use.

### Colors

The window's colors are built into the program, and one set of them is chosen from the Settings menu: the waveform's. The window itself is made of MATERIALS, pictures of wood and brushed metal drawn for the program (the files in `assets/tiles/`, one per material; a picture dropped there under a material's name replaces it at the next launch): the title bar, the menu row and the toolbar band are WOOD, the band's empty stretch a darker wood, the title and the menu words brushed gold; every button's case is a brushed metal, each case its own patch of it, the title bar's three buttons too; the tab strip, the bottom row, the margins round the waveform, the menus, the dialogs, the cards and the keyboard are a dark brushed METAL, and the recesses below the toolbar — the ground behind the tabs, the strip above the view bar, the ruler, the marker lane and the strip under the waveform — a very dark one; the clock's field is black under amber digits. The picture never changes how anything is drawn: every edge, line and shade is Cool Edit Pro's own, its light and dark edges derived from each material's average color by Cool Edit's rule (on every material painted as a flat stroke of that color, with no grain), each channel of the waveform in a sunken frame of its own, one row of the metal between the two (a press in the lower frame scrubs when released without moving, one in the upper frame places the playhead, and the row between them, like the frames, takes no press); the text, the fields, the selection (Cool Edit's default waveform green) and the cards' frame follow the dark metal's average color by Cool Edit's own rules. Settings → Bevel Light and Bevel Dark (a tuning phase, each a percent, 100 the rule exactly, 0 to 400) weigh every lighter and every darker edge on every material; OK applies the weight at once and keeps it for the next launch. Settings → Tile Scale (a tuning phase, a percent from 10 to 100, 50 the default) sets how small the pictures of wood and metal are drawn, like the icons; the same. The waveform wears a preset of ten colors: Canvas, Ink and Waveform Outline, the waveform's (the view bar's span following the Ink); Grid and Center Line, Cool Edit's lines on the waveform — the grid, under the waveform, quartering each channel's half in horizontal lines (Cool Edit's vertical lines are left out, so they do not compete with the markers' dots), and each channel's center line on its zero, drawn over the waveform so it shows even through a recording's hiss — all one screen pixel thin at every scale, as everything on the waveform is; Cue and Range, Cool Edit's two marker colors, red and blue — a warp marker or a phase reset is a small red triangle with dots down the waveform alternating red and blue (which of the two it is reads from its label), and in the history view an added marker (the plus) the red triangle with red dots alone; Invalid, the color that marks a marker as invalid — its triangle, its dots and its label at rest — and a removed marker in the history view (the minus), each preset's own center-line color (a label at rest wears the panel's light tone, an invalid one the Invalid color, and every label, invalid or not, when selected, the program's light text color, with no highlight behind it — the highlight appears when the label is opened for editing, its whole text selected — and a triangle keeps its color when selected); Playhead, the playhead's yellow triangle in the ruler and its dots down the waveform, the one yellow line; Scanner, the white of the lines that belong to the controls — the line that moves during playback and the zoom's anchor line — Cool Edit's own white, the same on every one of its presets. Settings → Waveform chooses the preset among Cool Edit Pro 2.1's own twenty color presets, Default (`cool-edit-default`), Cool Edit's own default, the preset the program wears until you choose another, Midnight (`cool-edit-midnight`) and CEPro 1.2 (`cool-edit-cepro-1-2`) among the other nineteen (each key is `cool-edit-` and the preset's name in lowercase, hyphens for spaces and punctuation); each sets Cool Edit's canvas, waveform, grid, center line, cue, range and playhead colors, the Waveform Outline a darker shade of its waveform color, the Invalid color its center line's, and the scanner Cool Edit's white in all of them. The presets are built into the program; there are no color files, no color picker and no drop-down list. The row opens the Settings line on its key and the one on screen (`waveform=cool-edit-default`); type another preset's key, and Tab completes what you have typed of one: a beginning only one key shares becomes that key, a beginning several share goes on as far as they agree (`waveform=c` Tab stops at `cool-edit-`), and a Tab that adds nothing moves on to OK. OK (or Enter) applies it at once and keeps it for the next launch — the waveform and its markers change, and everything else keeps its colors — and Cancel (or Esc) leaves the colors as they were; a word that is no preset's key is refused, its reason on a card. True Colors, below the bevel rows, is checked at every launch and shows every color on the tablet as a web browser shows the same hex; uncheck it only to take a screenshot whose bytes are the colors as typed (the tablet's screen reads oversaturated meanwhile, and on the laptop it changes nothing). The icons (Adwaita 3.22's) wear their own colors, never the window's or the waveform's.

### Font

Every text is set in Liberation Sans, a stand-in for Arial, in one Windows text cell, so no row or lane depends on the face. There is no font setting: the face is built into the program.

## A working method

How a movement actually gets authored, in the order that works:

1. Find the introduction measures that define the tempo. Set warp markers there, set the corresponding phase resets, then run BPM iterations and pick the tempo that sits right.
2. Work down the piece in chronological order, moving between the views (1 / 2 for source or target, 3 / 4 for warp or phase reset): source + warp to place markers, target + warp for tempo adjustments by ear, and target + phase reset to protect the transients (source + phase reset when a reset is better placed against the recording itself).
3. Once some headway is made, copy label definitions into references (the exposition into the repeat and the recapitulation), and paste the definition's phase resets across with the propagate commands in the Edit menu.
4. Phase resets are set with the phase-reset drop and nudged only when the phase alignment causes an audible dip, pop or crackle.
5. The A/B audition is crucial for confirming that label references match their definitions: it is the crux of the tempo-locking mechanism, which lets precise timing be replicated in the sonata-form fashion of repeated rhythmic motifs and phrases, at both large and small scale.
6. Grid and BPM iterations, trimmed target previews and the history view carry the rest: render a spread (a box counts the cells as they render, its Cancel stops the spread, and the render player opens on the spread when the last cell is done), listen in the render player, load the winner in place, checkpoint.

On timing: the scanner's smooth movement lets a warp marker land essentially at the onset of an attack, so place against what you see and hear together.

## The reference project

`projects/550 - 1/` in the [projects repository](https://github.com/warptempo/warptempo_projects), the first movement of Symphony No. 40, is the worked example and exercises everything: owning and inheriting markers, disabled markers, label definitions and references, a tempo written as a main speed plus an adjustment term, tempos fine-tuned with a per-marker scale, phase resets placed under masking, and a complete settings block. See it wherever a number or a spelling matters. The audio is commercially licensed and not distributed: supply your own copy of the recording under the name its marker files expect ([`INSTALL.md`](INSTALL.md), First run).

A lossy copy is enough to follow the example and hear what the markers do; the lossless commercial release is the one to author against:

[Reference source](https://music.youtube.com/watch?v=f10ISOkJZuA&list=OLAK5uy_nMff2yJASrC9u9uf4b0uPZYoiDt-MdTh8)

Example output, also in lossy audio format:

[Symphony No. 40](https://www.youtube.com/playlist?list=PLm5sJJQZOLT1OkUITQ4vX2l20qzGylkqI)

## Where the rest lives

- [`INSTALL.md`](INSTALL.md): installing, building, the first run, the tablet, daily use, trouble, and migrating an older project folder.
- The tooltips: every button and its key, with the tooltip lamp lit.
- The menus: each title's and each row's underlined letter is its access key — Alt with a title's letter opens that menu, and a row's letter runs the row while its menu is open.
- The dialogs: a button's underlined letter is its access key — the letter alone answers a question, Alt with the letter while a text field has the focus; Enter is the focused button and Esc is Cancel.
- The source comments and `docs/engineering/closed_questions.md`: the engineering record of the rulings behind the behaviour, not user reading.
- [`README.md`](../README.md): credits and licence.
