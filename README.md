# warptempo_gui

`warptempo_gui` is a phase-vocoder application for transparent time-warping of recorded music under full manual control. The engine is a faithful, centered implementation of Prusa–Holighaus phase-gradient heap integration (PGHI — "Phase Vocoder Done Right"), wrapped in a Cairo GUI for placing warp markers, specifying tempos, and auditioning the result — over Wayland and JACK on the desktop.

It was built for one job: warping classical orchestral recordings toward historically informed tempos. Commercial DAWs assume a metronome-driven timebase with a click track; classical recording is free-tempo, with no underlying grid. Existing time-stretch libraries automate transient handling; here both concerns stay in the operator's hands. The stretch is applied locally through warp markers to follow a continuously varying tempo, and phase-reset markers are placed one by one, so the operator chooses the tradeoff between transient impact and the discontinuity each reset introduces. Because the stretch is local and section-scoped, sections can be tempo-locked to one another by name: a marker defines a label, later markers reference it, and every occurrence renders at the same effective tempo — recapitulated material across a sonata-form movement stays tied to its exposition counterpart through every subsequent edit.

The simplest use — one constant stretch ratio for a whole file — needs no marker work at all: the ratio is set through the `scale` setting (a full-precision double in `[0.5, 2]`), and warp markers enter the picture only when you want phrase-level tempo variation.

Renders are finished 24-bit PCM WAV deliverables.

## The working method

A movement starts at the tempo. Find the introduction measures that define it, set warp markers there with their phase resets, and run BPM iterations: the program renders the passage once at each candidate BPM, you audition the results in the built-in player, and the one that sits right is loaded in place as the baseline. From there the work runs down the piece in chronological order, cycling through three views — source audio with warp markers to place the markers against the unprocessed recording, target audio with phase resets to protect the transients, and target audio with warp markers to adjust tempos by ear. Phase resets are dropped with one chord (`Shift+S`) and nudged only when the phase alignment causes an audible dip, pop, or crackle.

Once a section has taken shape, its labels carry the rest of the movement: copy the exposition's label definitions into references at the repeat and the recapitulation, and paste its phase resets across with the propagate commands. The A/B audition (`Shift+Space`) — the same short span played from each of the two tabs, back to back — is how a reference is confirmed against its definition, and that confirmation is the crux of the tempo-locking mechanism: it is what lets precise timing be replicated in the sonata-form fashion of repeated rhythmic motifs and phrases, at both large and small scale. Iteration sweeps, trimmed target previews, and the built-in git history carry the rest of the loop: render a spread, listen, load the winner in place, checkpoint.

## Building and running

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
./build/warptempo_gui
```

The program always opens a project — a folder under the per-device `projects_path`, holding the source WAV and the program-written sidecar files beside it. With no argument it opens the project it had open last, or the first valid project folder it finds; with one argument — a project's source WAV, inside its own project folder — it opens that project, and anything else refuses with the reason on stderr. `Ctrl+O` switches projects from inside.

The GUI targets Linux with a Wayland compositor and JACK audio. The same GUI also builds as an Android APK, over the Android framework and AAudio rather than Wayland and JACK — the build road is in the runbook, [`docs/INSTALL.md`](docs/INSTALL.md). `scripts/warptempo_sync` moves the source audio from the laptop to the tablet and the renders back (the runbook has the detail). A headless render CLI (`-DWARPTEMPO_BUILD_CLI=ON`) builds and runs without Wayland or JACK, including under WSL2, and renders byte-identically to the GUI.

Two documents carry the rest: [`docs/HELP.md`](docs/HELP.md), the concepts and the working method, and [`docs/INSTALL.md`](docs/INSTALL.md), the runbook for installing, building, the first run, the tablet and upkeep. The interface is documented by its own tooltips.

## Projects

The working corpus's sidecar sets live in their own repository, [warptempo_projects](https://github.com/warptempo/warptempo_projects), under its `projects/` directory: the 1972 Krips / Royal Concertgebouw Mozart symphony recordings (Christopher Bernauer's 2024 remaster for Decca Eloquence), warped toward the metronome marks Hummel published. The audio itself is commercially licensed and not distributed; you supply the source recording, as described in the runbook's First run. The Symphony No. 40 first movement (`projects/550 - 1/` there) is the reference project: it exercises every form and syntax the project uses — owning and inheriting markers, label definitions and references, two-decimal tempos fine-tuned with the full-precision per-marker scale, phase resets placed under masking, measures, and a complete settings block — and it demonstrates the timing the program was built to reach, warp markers landed essentially at the onset of an attack.

Example output, in lossy audio format:

[Symphony No. 40](https://www.youtube.com/playlist?list=PLm5sJJQZOLT1OkUITQ4vX2l20qzGylkqI)

## License

GPL v3. See `LICENSE`.

## Third-party work and design credits

**Icons — Tango.** The glyphs the interface draws are the [Tango Desktop Project](https://en.wikipedia.org/wiki/Tango_Desktop_Project)'s icon theme 0.8.90, its scalable drawings copied byte for byte under `assets/icons/tango/` (one SVG per icon, named by its role; five drawings serve more than one role, the folder's README lists them) and rendered at every scale by [resvg](https://github.com/linebender/resvg) (Apache-2.0 OR MIT), compiled into both builds from one pinned source (`src/gui/svg_icon.h`; the statically linked libraries and their licences are listed in `THIRD_PARTY.md`). Tango released the icons into the public domain (0.8.90's `COPYING`) and relicensed them under CC0 1.0 (0.9.0); both texts and the authors' list are in that folder, whose `README.md` records each icon's Tango file and its checksum.

**Icons — Mist.** One more set ships beside Tango, one SVG per icon: `assets/icons/mist/`, gnome-icon-theme 2.30's drawings as Debian 6's Mist theme shows them (a choice in the Settings menu under every chrome). Its `README.md` records the provenance, the licence texts and every icon's source file.

**Program icon — GNOME's audio file.** The program's icon outside the window is `assets/icons/mist/AppIcon.svg` (gnome-icon-theme 2.30's audio-x-generic, the Mist set's; since 2026-10-07), on the ground #EDECEB: the Android launcher icon, rasterized into `android/app/res/mipmap-<density>/` over that ground as the adaptive icon's two layers, and the Linux desktop icon `packaging/warptempo_gui.svg` — both generated by `tools/app_icon/gen_app_icon.sh`, never hand-edited.

**Cursors — not distributed.** The pointer shapes the desktop GUI uses (`left_ptr`, `grab`, `zoom-in`, `ew-resize`, `left_side`, `right_side`, and `text`, with the older `xterm` as its fallback name) are looked up **by name** in whatever XCursor theme the user has installed, and their pixels are supplied at runtime by that theme. No cursor artwork is included in this repository or in the binary, and nothing is redistributed. A theme missing one of the other names falls back to `left_ptr` for that cue; a theme missing `left_ptr` itself has no arrow to fall back to, and the pointer goes without an image.

**Concept — Ableton Live.** Warp markers as a way of working come from Ableton Live, which is where the author first encountered them; the label definitions and references this project adds exist because Live has no equivalent. Two smaller interactions were taken as ideas: auditioning by clicking the waveform's lower half, and the zoom-and-pan drag on the ruler. Nothing of Ableton's is included in any form.

**Interface design — kdenlive.** The interface (the button rows, the tabs, the dropdown menus, the Breeze Dark palette, the trim bar and ruler) was reproduced from observation of [kdenlive](https://kdenlive.org), with colors sampled and metrics measured from screenshots. No kdenlive code or artwork is included, and the reference screenshots are not distributed. kdenlive is a KDE project, licensed GPL-2.0-or-later; no license obligation follows from reproducing a layout, and the credit is given because the design is theirs.
