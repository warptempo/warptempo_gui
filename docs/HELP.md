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

Phase resets are a separate collection of markers, authored in target view alone. The vocoder's frame-to-frame phase coherence keeps sustained sound clean but smears an attack; at a phase reset it re-seats its phase instead, so the attack (an entry, a pizzicato, a timpani stroke) arrives clean. A reset belongs a lead-in ahead of its transient, and the drop does that arithmetic for you. Each one trades a little local coherence for the clean attack, so they are placed one by one, and nudged only when a dip, pop or crackle is heard.

### Two tabs, three views

The two tabs, A and B, are two viewpoints (viewport, zoom, playhead and trim) over one shared set of markers, so one can sit at a definition while the other sits at its reference. The active tab's letter ends the clock at the bottom left (`00:45.115 | A`), and Switch Tab in the bottom row flips to the other one. The three views are source + warp, where warp markers are placed; target + warp, where tempos are judged; and target + phase reset, where the transients are protected.

Save is the unsaved-work mark: it lights when the markers or the piece's settings block (the scale, the BPM and the rest) change, and is dimmed again once saved or undone back to the saved state. The tabs' viewpoints, their trims and their read-only locks never light it; they are written with the next save of such a change, and a trim or lock changed on its own is not kept.

### History

A piece's checkpoints are git commits in the projects repository. The tablet authors and commits (Save and Commit, in the history view), GitHub is the hub, and the laptop pulls. The history view walks the checkpoints and shows each as a diff of flags, from which a difference can be reverted or a whole checkpoint loaded in place; the mechanics are in [`INSTALL.md`](INSTALL.md)'s Daily use.

### Colors

The window wears two sets of colors. The title bar wears a scheme: by default Windows 2000's own ("Windows Standard", built into the program). A scheme picks the title bar's six colors: Title, Title End and Title Text, and the same three for the title bar of a window without the focus (Inactive Title, Inactive Title End, Inactive Title Text, which follow the active three until you pick them). The first title bar color you pick over the chrome's own scheme takes the other title bar colors from the scheme on screen. Everything else follows the program's palette, as Cool Edit Pro's own windows followed its color scheme: the menus, the buttons, the dialogs and the cards, drawn as Windows 2000, stand on the Panel Face with Cool Edit's own light and dark edges derived from it, their text Cool Edit's light text (a dark text on a light Panel Face, where Cool Edit turns its own text dark), the fields Cool Edit's dark field under the light text, a selection the waveform's color under a darkened text, and the tooltip and the cards the Panel Face framed in its dark line. The program's own elements wear a palette of twelve colors: Canvas, Ink and Waveform Outline, the waveform's; Grid and Center Line, Cool Edit's lines on the waveform — the grid, under the waveform, quartering each channel's half in horizontal lines (Cool Edit's vertical lines are left out, so they do not compete with the markers' dots), and each channel's center line on its zero, drawn over the waveform so it shows even through a recording's hiss, all one screen pixel thin at every scale, as everything on the waveform is; Cue and Range, Cool Edit's two marker colors, red and blue — a warp marker or a phase reset is a small red triangle with dots down the waveform alternating red and blue (which of the two it is reads from its label), an invalid one the red triangle with red dots alone, and in the history view an added marker blue with blue dots, a removed one red with red dots; Invalid Label and Invalid Label Selected, the invalid marker's label, a dim red at rest and a bright red when selected (every other label wears the panel's light tone and, when selected, the program's text color, which is white on a dark Panel Face and dark on a light one, with no highlight behind it — the highlight appears when the label is opened for editing, its whole text selected — and a triangle keeps its color when selected); Playhead, the playhead's yellow triangle in the ruler and its dots down the waveform, the one yellow line; Scanner, the white of the lines that belong to the controls — the line that moves during playback and the zoom's anchor line; and Panel Face, the ground of the toolbar band, the bottom row, the three lanes above the waveform — the view bar, the ruler and the marker lane — and the margins and frame round the waveform, which are drawn as Cool Edit Pro drew its own under every chrome, every line and shade of the panel following that one color, and the view bar's span following the waveform's ink. Both are picked live with Pick Colors at the foot of the Settings menu. The first drop-down beside the color wheel chooses what you are picking, Chrome or Waveform, and the picker opens on Waveform every time. Choose an element by name in the drop-down beside it, then set its color on the hue ring and its triangle, on the Hue, Saturation, Value, Red, Green and Blue sliders, or in the hex field; the window repaints as you go, NEW shows the color beside OLD, the one the element had when you chose it, and a tap on OLD takes it back. Copy and Paste carry a color from one element to another, across the two as well. While the picker is open, a tap on a cue selects it, so the selected label can be judged under the colors being picked; it moves nothing, and nothing else under the picker answers except the two rows of buttons. Schemes and palettes are kept as named presets in the menu at the foot of the picker, which shows the schemes while you pick Chrome and the palettes while you pick Waveform: Save, Save As, Rename and Delete at the top, then your presets of that kind, then the built-in ones. Choosing a name loads it, Save writes the colors into the current preset, Save As keeps them under a new name, Rename renames the current preset, and Delete removes it after asking; each touches that kind alone, so loading a scheme changes the title bar alone and leaves the palette as it is, while loading a palette brings its Panel Face and colors to the whole window, the menus, buttons and dialogs included, and leaves the title bar's scheme as it is. The built-in schemes are the chrome's own (Windows 2000 Standard) and the program's own, Cool Edit Pro ME (Windows Me's title bars), then, after a separator, every Windows, Plus!, KDE 3 and CDE color scheme of the period, each its title bars alone; choosing the chrome's own brings its built-in title bar back. A scheme carries colors alone, never a font. The built-in palettes are Cool Edit Pro 2.1's own color presets, in its own order: Default — the palette the program wears under every chrome until you choose another — then XP Blue, XP Silver, XP Olive, Fire and Brick, Classic Cool, Lipstick and Grapes, Seattle Blues, Midnight, Periwinkle, CEPro 1.2, 3D, Stealth, "Grape, Lime, and Tangerine", Dusty Rose, Nature Calls, Arctic Freeze, Midnight Blues, Safari So Good and Easy on the Eyes; each sets Cool Edit's canvas, waveform, grid, center line, cue, range, playhead and Panel Face colors, the Waveform Outline a darker shade of its waveform color, and the invalid label's reds and the white scanner the same in all of them; choosing Default brings the program's own colors back. Built-ins cannot be changed or renamed, and their names cannot be taken by a preset of the same kind (a scheme and a palette may share a name). Closing the picker keeps the colors on screen without asking, but only what was saved is kept: the next launch returns to the chosen scheme and the chosen palette as last saved. True Colors, below Pick Colors, is checked at every launch and shows every color on the tablet as a web browser shows the same hex; uncheck it only to take a screenshot whose bytes are the colors as typed (the tablet's screen reads oversaturated meanwhile, and on the laptop it changes nothing). The icons wear their own colors, never the scheme's or the palette's: the Settings menu's Icons row chooses the icon set, at the next launch, between Tango and Mist (left alone, the chrome's own).

### Font

The Settings menu's Font row, after Icons, chooses the face every text is set in, whatever scheme is on screen, and the window changes face the moment you press OK. The faces are listed alphabetically: FreeSans, standing in for MS Sans Serif, the face of Windows before 2000; Liberation Sans, a stand-in for Arial, the fallback should FreeSans's shapes trouble you at a large scale; and Tahoma, Windows 2000's own and the face until you choose another. Each is named by the font the program actually draws, never by the Windows face it stands in for. All three are set to the same Windows text cell, so no row or lane moves when the face changes; only the letters do.

## A working method

How a movement actually gets authored, in the order that works:

1. Find the introduction measures that define the tempo. Set warp markers there, set the corresponding phase resets, then run BPM iterations and pick the tempo that sits right.
2. Work down the piece in chronological order, moving between the three views: source + warp to place markers, target + warp for tempo adjustments by ear, and target + phase reset to protect the transients.
3. Once some headway is made, copy label definitions into references (the exposition into the repeat and the recapitulation), and paste the definition's phase resets across with the propagate commands in the Edit menu.
4. Phase resets are set with the phase-reset drop and nudged only when the phase alignment causes an audible dip, pop or crackle.
5. The A/B audition is crucial for confirming that label references match their definitions: it is the crux of the tempo-locking mechanism, which lets precise timing be replicated in the sonata-form fashion of repeated rhythmic motifs and phrases, at both large and small scale.
6. Grid and BPM iterations, trimmed target previews and the history view carry the rest: render a spread, listen in the render player, load the winner in place, checkpoint.

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
