# warptempo_gui

warptempo_gui time-warps a recording toward the tempos you choose. It was built for taking commercial orchestral recordings to historically informed tempos, phrase by phrase, by ear.

The engine is a PGHI phase vocoder (Prusa and Holighaus's phase-gradient heap integration, "Phase Vocoder Done Right") run at the authors' own parameters, N = 4096 with 75 % overlap, at the source's own sample rate, and extended in two ways: the analysis hop varies frame by frame with the warp map, and phase resets are placed by hand. Those two and its other departures from the paper (the output timing contract, the pinned randomness of quiet bins) are deliberate and recorded in the engine's own comments. A render is a finished 24-bit WAV through an unconditional limiter, and it is deterministic: the same project on the same machine and build renders the same bytes every time.

There are two products over one interface: the desktop GUI (Wayland and JACK) and the same GUI as an Android APK; a headless CLI renders the same bytes as the GUI. Installing, building, the first run, the tablet and all upkeep are in [`INSTALL.md`](INSTALL.md).

The interface documents itself. With the tooltip lamp lit (Toggle Tooltips, the bare `\`, in the icon row), resting the pointer over any button, or hovering the S Pen over it, names its act and its key. This guide therefore names acts, never keys, and holds only what the tooltips cannot show: what the markers mean and how a movement is worked.

## How the program says no

When a key or a click refuses, the program says why in a card at the top right of the window. Silence means the press was unbound, or a command that acts in one place was already at its state there. So this guide lists no refusals: the program tells you at the moment it matters.

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

The two tabs, A and B, are two viewpoints (viewport, zoom, playhead and trim) over one shared set of markers, so one can sit at a definition while the other sits at its reference. The active tab's letter leads the clock at the bottom left (`A | 00:45.115`), and Switch Tab in the bottom row flips to the other one. The three views are source + warp, where warp markers are placed; target + warp, where tempos are judged; and target + phase reset, where the transients are protected.

### History

A piece's checkpoints are git commits in the projects repository. The tablet authors and commits (Save and Commit, in the history view), GitHub is the hub, and the laptop pulls. The history view walks the checkpoints and shows each as a diff of flags, from which a difference can be reverted or a whole checkpoint loaded in place; the mechanics are in [`INSTALL.md`](INSTALL.md)'s Daily use.

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
- The source comments and `docs/engineering/closed_questions.md`: the engineering record of the rulings behind the behaviour, not user reading.
- [`README.md`](../README.md): credits and licence.
