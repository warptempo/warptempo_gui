# warptempo_gui

`warptempo_gui` is a phase-vocoder application for transparent time-warping of recorded music under full manual control. The engine is a faithful, centered implementation of Prusa–Holighaus phase-gradient heap integration (PGHI — "Phase Vocoder Done Right"), wrapped in a Cairo/Wayland GUI for placing warp markers, specifying tempos, and auditioning the result over JACK.

It was built for one job: warping classical orchestral recordings toward historically informed tempos. Commercial DAWs assume a metronome-driven timebase with a click track; classical recording is free-tempo, with no underlying grid. Existing time-stretch libraries automate transient handling; here both concerns stay in the operator's hands. The stretch is applied locally through warp markers to follow a continuously varying tempo, and phase-reset markers are placed one by one, so the operator chooses the tradeoff between transient impact and the discontinuity each reset introduces. Because the stretch is local and section-scoped, sections can be tempo-locked to one another by name: a marker defines a label, later markers reference it, and every occurrence renders at the same effective tempo — recapitulated material across a sonata-form movement stays tied to its exposition counterpart through every subsequent edit.

The simplest use — one constant stretch ratio for a whole file — needs no marker work at all: the ratio is set through the `scale` setting (a full-precision double in `[0.5, 2]`), and warp markers enter the picture only when you want phrase-level tempo variation.

Renders are finished 24-bit PCM WAV deliverables.

## The working method

A movement starts at the tempo. Find the introduction measures that define it, set warp markers there with their phase resets, and run a BPM series: the program renders the passage once at each candidate BPM, you audition the results in the built-in player, and the one that sits right is loaded in place as the baseline. From there the work runs down the piece in chronological order, cycling through three views — source audio with warp markers to place the markers against the unprocessed recording, target audio with phase resets to protect the transients, and target audio with warp markers to adjust tempos by ear. Phase resets are dropped with one chord (`Shift+S`) and nudged only when the phase alignment causes an audible dip, pop, or crackle.

Once a section has taken shape, its labels carry the rest of the movement: copy the exposition's label definitions into references at the repeat and the recapitulation, and paste its phase resets and measure numbers across with the propagate commands. The A/B audition (`Shift+Space`) — the same short span played from each of the two tabs, back to back — is how a reference is confirmed against its definition, and that confirmation is the crux of the tempo-locking mechanism: it is what lets precise timing be replicated in the sonata-form fashion of repeated rhythmic motifs and phrases, at both large and small scale. Iteration sweeps, trimmed target previews, and the built-in git history carry the rest of the loop: render a spread, listen, load the winner in place, checkpoint.

## Building and running

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
./build/warptempo_gui
```

The program always opens a project — a folder under the per-device `projects_path`, holding the source WAV and the program-written sidecar files beside it. With no argument it opens the project it had open last, or the first valid project folder it finds; with one argument — a project's source WAV, inside its own project folder — it opens that project, and anything else refuses with the reason on stderr. `Ctrl+O` switches projects from inside.

The GUI targets Linux with a Wayland compositor and JACK audio. The same GUI also builds as an Android APK, over the Android framework and AAudio rather than Wayland and JACK — the build road is in the guide. A headless render CLI (`-DWARPTEMPO_BUILD_CLI=ON`) builds and runs without Wayland or JACK, including under WSL2, and renders byte-identically to the GUI.

Dependencies, build options, first run, the conceptual model, the output contract, and the complete interface reference are all in [`docs/HELP.md`](docs/HELP.md) — the user guide, and the only one you need.

## Projects

The `projects/` directory ships the working corpus's sidecar sets: the 1972 Krips / Royal Concertgebouw Mozart symphony recordings (Christopher Bernauer's 2024 remaster for Decca Eloquence), warped toward the metronome marks Hummel published. The audio itself is commercially licensed and not distributed; you supply the source recording, as described in the guide's First run. The Symphony No. 40 first movement (`projects/550 - 1/`) is the reference project: it exercises every form and syntax the project uses — owning and inheriting markers, label definitions and references, two-decimal tempos fine-tuned with the full-precision per-marker scale, phase resets placed under masking, measures, and a complete settings block — and it demonstrates the timing the program was built to reach, warp markers landed essentially at the onset of an attack.

Example output, in lossy audio format:

[Symphony No. 40](https://www.youtube.com/playlist?list=PLm5sJJQZOLT1OkUITQ4vX2l20qzGylkqI)

## License

GPL v3. See `LICENSE`.

## Third-party work and design credits

**Icons — KDE Breeze, LGPL-3.0-or-later.** The glyphs the interface draws come from the KDE [Breeze icon theme](https://invent.kde.org/frameworks/breeze-icons). Their source SVGs are committed verbatim under `assets/icons/breeze/`, and `src/gui/icons.cpp` carries each file's path data transcribed byte-for-byte (the project interprets the paths directly rather than linking an SVG library, so a diff between the table and the file is a transcription bug and nothing else). Breeze is licensed LGPL-3.0-or-later; LGPLv3 grants permission to convey a covered work under the plain GNU GPL, and that is how the glyphs are conveyed here — under this project's GPL v3, whose text is `LICENSE`. The upstream license text is at <https://www.gnu.org/licenses/lgpl-3.0.txt>.

**Cursors — not distributed.** The pointer shapes the GUI uses (`left_ptr`, `grab`, `zoom-in`, `ew-resize`, `left_side`, `right_side`, and `text`, with the older `xterm` as its fallback name) are looked up **by name** in whatever XCursor theme the user has installed, and their pixels are supplied at runtime by that theme. No cursor artwork is included in this repository or in the binary, and nothing is redistributed. A theme missing one of the other names falls back to `left_ptr` for that cue; a theme missing `left_ptr` itself has no arrow to fall back to, and the pointer goes without an image.

**Concept — Ableton Live.** Warp markers as a way of working come from Ableton Live, which is where the author first encountered them; the label definitions and references this project adds exist because Live has no equivalent. Two smaller interactions were taken as ideas: auditioning by clicking the waveform's lower half, and the zoom-and-pan drag on the ruler. Nothing of Ableton's is included in any form.

**Interface design — kdenlive.** The interface (the button rows, the tabs, the dropdown menus, the Breeze Dark palette, the trim bar and ruler) was reproduced from observation of [kdenlive](https://kdenlive.org), with colors sampled and metrics measured from screenshots. No kdenlive code or artwork is included, and the reference screenshots are not distributed. kdenlive is a KDE project, licensed GPL-2.0-or-later; no license obligation follows from reproducing a layout, and the credit is given because the design is theirs.
