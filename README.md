# warptempo_gui

`warptempo_gui` is a phase-vocoder application for transparent time-warping of recorded music under full manual control. The engine is a faithful, centered implementation of Prusa–Holighaus phase-gradient heap integration (PGHI — "Phase Vocoder Done Right"), wrapped in a Cairo GUI for placing warp markers, specifying tempos, and auditioning the result — an Android app on one tablet.

It was built for one job: warping classical orchestral recordings toward historically informed tempos. Commercial DAWs assume a metronome-driven timebase with a click track; classical recording is free-tempo, with no underlying grid. Existing time-stretch libraries automate transient handling; here both concerns stay in the operator's hands. The stretch is applied locally through warp markers to follow a continuously varying tempo, and phase-reset markers are placed one by one, so the operator chooses the tradeoff between transient impact and the discontinuity each reset introduces. Because the stretch is local and section-scoped, sections can be tempo-locked to one another by name: a marker defines a label, later markers reference it, and every occurrence renders at the same effective tempo — recapitulated material across a sonata-form movement stays tied to its exposition counterpart through every subsequent edit.

The simplest use — one constant stretch ratio for a whole file — needs no marker work at all: the ratio is set through the `scale` setting (a full-precision double in `[0.5, 2]`), and warp markers enter the picture only when you want phrase-level tempo variation.

Renders are finished 24-bit PCM WAV deliverables.

## The working method

A movement starts at the tempo. Find the introduction measures that define it, set warp markers there with their phase resets, and run BPM iterations: the program renders the passage once at each candidate BPM, you audition the results in the built-in player, and the one that sits right is loaded in place as the baseline. From there the work runs down the piece in chronological order, cycling through three views — source audio with warp markers to place the markers against the unprocessed recording, target audio with phase resets to protect the transients, and target audio with warp markers to adjust tempos by ear. Phase resets are dropped with one gesture (a long press on DROP) and nudged only when the phase alignment causes an audible dip, pop, or crackle.

Once a section has taken shape, its labels carry the rest of the movement: copy the exposition's label definitions into references at the repeat and the recapitulation, and paste its phase resets across with the propagate commands. The A/B audition (a long press on PLAY) — the same short span played from each of the two tabs, back to back — is how a reference is confirmed against its definition, and that confirmation is the crux of the tempo-locking mechanism: it is what lets precise timing be replicated in the sonata-form fashion of repeated rhythmic motifs and phrases, at both large and small scale. Iteration sweeps, trimmed target previews, and the built-in git history carry the rest of the loop: render a spread, listen, load the winner in place, checkpoint.

## Building and running

The GUI runs on one Android tablet, over the Android framework and AAudio. It is built on a Linux laptop, `bash apk/build_apk.sh` against cross-built libraries; the road, from the toolchain to the install, is in the runbook, [`docs/INSTALL.md`](docs/INSTALL.md). The same laptop renders with the headless CLI, which a plain configure builds alone, needing fftw and nothing of the GUI's (it builds under WSL2 too), and which renders what the tablet renders through the same stages:

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
./build/warptempo_cli "path/to/project/source.wav"
```

The program always opens a project — a folder under the tablet's `projects_path`, holding the source WAV and the program-written sidecar files beside it. It opens the project it had open last, or the first valid project folder it finds; Open Project (the OPEN key) switches projects from inside. `scripts/warptempo_sync` moves the source audio and the scores from the laptop to the tablet, and nothing back: the tablet commits the sidecars, and the laptop pulls them and renders again (the runbook has the detail).

Two documents carry the rest: [`docs/HELP.md`](docs/HELP.md), the concepts and the working method, and [`docs/INSTALL.md`](docs/INSTALL.md), the runbook for installing, building, rendering, the tablet and upkeep. The interface is documented by its own tooltips.

## Projects

The working corpus's sidecar sets live in their own repository, [warptempo_projects](https://github.com/warptempo/warptempo_projects), under its `projects/` directory: the 1972 Krips / Royal Concertgebouw Mozart symphony recordings (Christopher Bernauer's 2024 remaster for Decca Eloquence), warped toward the metronome marks Hummel published. The audio itself is commercially licensed and not distributed; you supply the source recording, as described in the runbook's Rendering. The Symphony No. 40 first movement (`projects/550 - 1/` there) is the reference project: it exercises every form and syntax the project uses — owning and inheriting markers, label definitions and references, two-decimal tempos fine-tuned with the full-precision per-marker scale, phase resets placed under masking, measures, and a complete settings block — and it demonstrates the timing the program was built to reach, warp markers landed essentially at the onset of an attack.

Example output, in lossy audio format:

[Symphony No. 40](https://www.youtube.com/playlist?list=PLm5sJJQZOLT1OkUITQ4vX2l20qzGylkqI)

## License

GPL v3. See `LICENSE`.

## Third-party work and design credits

**Icons — the project's own four.** The toolbar's keys are blank, each with its act's name printed under it, so the only drawings on screen are four small SVGs in `assets/icons/`, the project's own: the quaver (the logo lane and the wav rows; Gemini Argon's one-path conversion of a bitmap drawn for it, at the architect's prompt), the folder (the folder rows), and the cards' i and X. They are licensed under the GNU LGPL v3 (the text in that folder) and rendered at every scale by [resvg](https://github.com/linebender/resvg) (Apache-2.0 OR MIT), compiled into the APK from one pinned source (`src/gui/svg_icon.h`; the statically linked libraries and their licences are listed in `THIRD_PARTY.md`).

**Launcher icon — the quaver.** The program's one icon outside the window is `assets/icons/AppIcon.svg` (architect 2026-10-23), a symlink to `Quaver.svg`: the quaver the logo lane wears, Gemini Argon's one-path conversion of a bitmap drawn for it, at the architect's prompt, under the GNU LGPL v3. It is the Android launcher icon, the note over one flat dark ground (#141415), rendered as the adaptive icon's two layers at every APK build (`apk/build_apk.sh`).

**Concept — Ableton Live.** Warp markers as a way of working come from Ableton Live, which is where the author first encountered them; the label definitions and references this project adds exist because Live has no equivalent. Two smaller interactions were taken as ideas: auditioning by clicking the waveform's lower half, and the zoom-and-pan drag on the ruler. Nothing of Ableton's is included in any form.

**Interface design — kdenlive.** The interface (the button rows, the tabs, the dropdown menus, the Breeze Dark palette, the trim bar and ruler) was reproduced from observation of [kdenlive](https://kdenlive.org), with colors sampled and metrics measured from screenshots. No kdenlive code or artwork is included, and the reference screenshots are not distributed. kdenlive is a KDE project, licensed GPL-2.0-or-later; no license obligation follows from reproducing a layout, and the credit is given because the design is theirs.
