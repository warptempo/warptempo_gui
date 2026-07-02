# warptempo_gui

[ Install, build, first run, the conceptual model, output formats and the limiter, and the hotkey reference. ]

## Dependencies

Build requires a C++23 toolchain (GCC 12+ or Clang 16+), CMake 3.20+, pkg-config, and the development headers for fftw3 and libsndfile. These are the complete dependency set for the two libraries and the headless binaries — the two standalone module binaries (`warptempo_parser`, `warptempo_engine`) plus the headless render interface (`warptempo_cli`); none of the GUI packages below are needed for them.

The default GUI build additionally requires the wayland-scanner tool and the development headers for cairo, wayland-client, wayland-cursor, wayland-protocols, libxkbcommon, and JACK. JACK can be the native jackd2 server or a PipeWire-shimmed JACK provider (modern distros default to the latter).

The `std::expected`-based parser pins the compiler floor at GCC 12 / Clang 16. On distributions whose default compiler is older — Ubuntu 22.04 ships GCC 11, for example — install a newer toolchain (`g++-12` or later) and select it with `-DCMAKE_CXX_COMPILER=g++-12`.

Arch Linux:

```bash
sudo pacman -S base-devel cmake pkgconf \
    cairo fftw libsndfile \
    wayland wayland-protocols libxkbcommon \
    pipewire-jack
```

Debian / Ubuntu:

```bash
sudo apt install build-essential cmake pkg-config \
    libcairo2-dev libfftw3-dev libsndfile1-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev \
    libjack-jackd2-dev
```

The libwayland-dev package on Debian ships wayland-scanner and the client / cursor headers in one piece.

Fedora:

```bash
sudo dnf install gcc gcc-c++ cmake pkgconf-pkg-config \
    cairo-devel fftw-devel libsndfile-devel \
    wayland-devel wayland-protocols-devel libxkbcommon-devel \
    pipewire-jack-audio-connection-kit-devel
```

For a headless build — the two libraries plus any of the CLI tools, with no GUI — only the core dependencies are needed. Omit the cairo / wayland / wayland-protocols / libxkbcommon / JACK packages from the lists above and configure with `-DWARPTEMPO_BUILD_GUI=OFF` (see the Build section). On Debian / Ubuntu the dependency set reduces to:

```bash
sudo apt install build-essential cmake pkg-config \
    libfftw3-dev libsndfile1-dev
```

A Wayland compositor is required at runtime — there is no X11 backend. Tested compositors include labwc, sway, and Hyprland; GNOME and KDE Wayland sessions should work but are not regularly exercised. The headless CLI tools need no compositor and no audio server; they read and write files only.

Windows (WSL2): the project builds and runs under WSL2 with a Debian or Ubuntu userland — install the Debian / Ubuntu dependencies above inside the WSL distribution and build normally. The headless CLI tools behave as on any Linux host. The GUI is not supported under WSL: WSLg provides a Wayland compositor but routes audio through PulseAudio, not JACK, and the application has no non-JACK audio path.

macOS: the GUI is not supported — it is a Wayland-native client and macOS has no Wayland. The headless CLI tools are portable C++ and should build with Homebrew dependencies (`brew install cmake pkg-config fftw libsndfile`) and `-DWARPTEMPO_BUILD_GUI=OFF`, though this path is not regularly tested.

## Build

Standard out-of-tree CMake build from the repository root:

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
```

The build always produces two static archives — `libwarptempo_parser` (the `.warpmarkers` / `.phaseresetmarkers` / `.settings` readers and the frame-map / tempomap build; depends on sndfile and the standard library only) and `libwarptempo_engine` (the PGHI synthesis core; depends on sndfile and fftw3) — and, by default, the `warptempo_gui` application that links both behind the Cairo/Wayland front end. `-O3 -march=native` is always on for GCC and Clang, so every binary is tuned for the host CPU and is not portable across machines; rebuild on the target host.

Four executables can be built from those two libraries, each a different slice of the pipeline, selected by CMake options:

`warptempo_gui` (default, `-DWARPTEMPO_BUILD_GUI=ON`) is the interactive authoring application — parser plus engine plus the Cairo/Wayland GUI. This is the normal build.

`warptempo_parser` (`-DWARPTEMPO_BUILD_PARSER=ON`) is parser-only. It reads a project's `.warpmarkers` and `.settings` beside the source audio and writes the framemap, tempomap, or resetmap — the portable map artifacts — without the engine or the GUI. Resetmap files contain undisplaced source-frame phase-reset onsets.

`warptempo_cli` (`-DWARPTEMPO_BUILD_CLI=ON`) is parser plus engine. It reads a full project and writes the warped wav the GUI would render for it, headless — no display, no GUI toolchain.

`warptempo_engine` (`-DWARPTEMPO_BUILD_ENGINE=ON`) is engine-only. It reads a prebuilt framemap (plus an optional resetmap) and the source wav and runs the PGHI engine directly, with no parser involvement — the entry point for driving the engine from maps produced by any source, not just this project's authoring files. Phase-reset anticipation is applied during render dispatch in target/output frames after temporal warping, then the anticipated target frame is inverse-mapped through the engine map and corrected by `N/2` into the engine's origin-centered source-query domain; marker files, GUI display positions, resetmaps, and render-view sidecars remain at the authored onset positions.

To build the libraries and any CLI tools on a headless host that has none of the GUI's Wayland / Cairo / JACK packages, add `-DWARPTEMPO_BUILD_GUI=OFF`; the two archives and the enabled CLI tools build without touching the GUI dependency discovery.

There is no `make install` target. The binary is self-contained: copy it to anywhere on `$PATH` (typically `~/.local/bin/` or `/usr/local/bin/`) to make it available system-wide.

```bash
install -Dm755 build/warptempo_gui ~/.local/bin/warptempo_gui
```

A `.desktop` file is shipped in `packaging/warptempo_gui.desktop`. To register `warptempo_gui` with the system's application launcher and as a handler for audio files, install it to the user-level application directory and refresh the desktop database:

```bash
install -Dm644 packaging/warptempo_gui.desktop \
    ~/.local/share/applications/warptempo_gui.desktop
update-desktop-database ~/.local/share/applications
```

The shipped `.desktop` file uses the freedesktop `audio-x-generic` icon name, which resolves to whatever the user's active icon theme provides — no icon file is bundled. Replace the `Icon=` line with a different freedesktop name or an absolute path to a custom SVG to override.

## First run

`warptempo_gui` takes a single argument: the path to a source audio file (typically `.wav` or `.flac`, though anything libsndfile reads will load). Marker and settings files for that audio file live in the same directory as the audio file itself, named by appending `.warpmarkers`, `.phaseresetmarkers`, and `.settings` to the audio file's basename.

```bash
warptempo_gui "path/to/source.wav"
```

On launch, `warptempo_gui` opens a window, reads any existing `.warpmarkers`, `.phaseresetmarkers`, and `.settings` sidecar files for the given audio file, and presents the source waveform across the full window width. With no sidecar files present, the window opens with a single warp marker at frame zero (the immovable anchor) and no phase reset markers. With sidecar files present, the prior session state is restored verbatim — viewport, zoom, playhead, active view selectors, and all marker positions and flags.

The examples directory in the repository ships reference movements with complete sidecar files. The Symphony No. 40 mvt I directory (`examples/550 - 1/`) is the recommended first load — it exercises every form and syntax used in the project. The audio file itself is not included (the corpus is commercially licensed); the user supplies the source recording and places it in the example directory under the basename the sidecar files expect.

The conceptual model and hotkey reference are in the sections below.

## Conceptual model

### Engine and frame map

`warptempo_gui` drives a phase vocoder. Internally it is built on a faithful, centered implementation of Prusa-Holighaus phase-gradient heap integration (PGHI) — one phase model. The gradient method enforces phase coherence in both time and frequency without peak-picking or transient detection, which is what lets a high stretch ratio preserve upper-band detail and transient impact rather than dulling them.

FFT size `N` is fixed at `4096`, the canonical PGHI window length, and is no longer a settings field; the synthesis hop `R_s` is derived as `N/4` (75% overlap). At this size the engine's parameters match the authors' released PGHI implementation exactly (`M=2N=8192`, `R_s=1024`, window length 4096; the paper's text states a 4092-sample window, a figure that appears nowhere in the authors' released code), giving the gradient integration its finest frequency resolution and the fullest low-frequency body. The engine inherits the source recording's sample rate. Per-frame phase correction keeps bins coherent within each frame so steady-state content — held strings, sustained winds, room tone — stretches without smearing into a chorused or phasey artifact.

### Relation to the published algorithm

The engine implements Prusa-Holighaus phase-gradient heap integration (ltfatnote050, "Phase Vocoder Done Right") faithfully: origin-centered zero-padded analysis (`M=2N`, matching the paper's ~2x padding), centered finite differences for both phase derivatives, trapezoidal integration in all three propagation directions, the max-heap significance ordering, and the published tolerance (`1e-6`). Where the implementation deviates from or extends the paper, the differences are deliberate and listed here.

- Window length. This engine's defaults match the authors' released reference implementation (github.com/ltfat/pvdoneright) parameter-for-parameter: window length 4096, `M=8192`, synthesis step 1024, tolerance `1e-6`. The paper's text states a 4092-sample window; that figure appears nowhere in the authors' released code, and the discrepancy between the paper and its own implementation is of unknown origin. This engine follows the implementation.
- Variable analysis hop. The paper fixes one analysis step for the whole file (constant stretch ratio). This engine derives the analysis hop per frame from the warp-marker frame map; each half of the centered time-derivative is normalized by its own actual hop, and the frequency-direction step uses the per-frame ratio. This is the extension that makes free-tempo warping possible and is the project's reason to exist.
- Phase resets. The paper has no mid-stream re-grounding; transient handling is left to detection-based methods it cites. This engine's manual phase-reset markers re-seat the synthesis phase to the analysis phase at user-chosen frames — operator judgment in place of detection.
- Output timing contract. The paper does not specify output alignment; this engine pins it. Synthesis trims exactly `N/2` samples from the head — the alignment latency the origin-centered analysis contributes — so source frame 0 maps to output frame 0, and a feature at source frame `S` lands at output sample `tgt(S)` from the frame map. Total output length is the target-frame position of the last source sample. The overlap-add ramps at head and tail are amplitude-only fades that move no alignment. The contract this establishes: any future engine consuming the same warp markers (and, if it supports them, the same phase-reset markers) can match this engine's output 1:1 in frame timing at the start and through the body. The tail's final incomplete-overlap ramp is deliberately outside the contract.
- Remaining boundary choices, unspecified by the paper: the first frame (and the frame after each phase reset) seeds synthesis phase from analysis phase — the seed that serves the timing contract and the reset model above; the first frame's time-derivative uses the forward difference only; the frequency-derivative is one-sided at DC and Nyquist.
- Quiet-bin randomness. The paper assigns unspecified "random values" to sub-tolerance bins. This engine pins the choice: uniform phase in (-pi, pi) from a per-channel deterministically seeded generator, so identical renders are byte-identical — the property the project's verification protocol depends on.
- Real-signal realization. The spectrum is held as the Hermitian half (`M/2+1` bins) rather than the paper's full-`M` complex formulation — mathematically identical for real input and output.

Variable-rate time-stretching with user-placed warp markers exists in other tools — DAWs that ship warp-marker editing on top of their bundled time-stretch algorithm have offered this for years. What separates this project is per-marker tempo locking through label definitions and references. A marker can declare a named label; a later marker can reference that label by name; the two markers and every other reference to the same label render at the same effective tempo, and editing the def's tempo updates every ref that cites it. The mechanism is described in the warp marker model section below. The use case it was built around is the sonata-form exposition/recapitulation relationship in classical orchestral repertoire, where the same musical material returns multiple times across a movement and should hold a coherent tempo relationship across all occurrences regardless of how the surrounding material is edited.

Per-frame phase coherence is what keeps steady-state content clean. It is also exactly what a transient defeats. An attack — oboe entry, pizzicato, timpani strike — is a moment where the spectrum reorganizes within a few hops, and the phase relationships the vocoder has been propagating no longer correspond to the acoustic event arriving. The attack smears across surrounding frames. The mitigation is a phase reset: at a user-chosen position, the synthesis phase is reset rather than propagated, sacrificing local coherence in exchange for a clean transient. Phase reset markers are the authoring surface for this. They are a separate marker collection from warp markers, visible and edited in their own view, because they parameterize a distinct engine concern: warp markers shape the frame map, phase reset markers protect transients.

### View axes

The bottom bar of the window shows three letter pairs: `[S/T] [W/P] [A/B]`. Each pair is an independent toggle. All three combinations are valid; the GUI's state is the product of all three selections plus whatever modes happen to be active on top.

The first pair, **source vs target**, selects the audio domain of the waveform under the playhead. Source view (S) shows the unprocessed source recording with warp markers drawn at the times the user authored them. Target view (T) shows the same audio after the engine has applied the live frame map — the waveform is stretched and compressed to reflect what the render will sound like, with markers drawn at their post-stretch positions. The two views serve different stages of the same authoring task. Source view is essential for the horizontal placement of new markers, where the user is pinning the marker's time against an acoustic event in the unprocessed recording. Once a skeleton of markers is in place, target view becomes the more efficient place to work for the rest of the session — modulating tempos, enabling and disabling markers, and judging the warp by ear directly. Target view is available only when `output_format=wav`; with `framemap` or `tempomap`, `t` from source view is a silent no-op, and a settings file that asks for target view with a non-wav output format is invalid. Trim narrowed to the section under revision keeps target renders fast enough for this loop to feel immediate.

The second pair, **warp vs phase reset**, selects which marker collection is visible and edited. Warp markers (W) drive the frame map. Phase reset markers (P) protect transients. The two collections are stored, authored, and rendered independently; toggling between them swaps which collection the marker hotkeys operate on but changes nothing else. Authoring proceeds in warp view first, since phase resets have no useful position until the frame map exists.

Beyond the data-model difference, the two collections want different placement discipline. Warp markers benefit from a lead-in: rather than landing exactly on a transient, place the marker slightly earlier, in the fading tail of the previous material. The lead-in matters most for label-bearing markers and for markers adjacent to label-bearing ones, where the tempo precision the label mechanism enforces only pays off if the underlying timing is itself precise. It also matters for quiet, slow, or legato onsets where the acoustic event itself has no sharp attack to pin to — pinning against the louder fade of the prior note is more reliable than guessing where the quiet onset begins. The A/B tab pair (described below) is the natural way to set lead-ins precisely, matching the lead-up across paired sections. Phase reset markers are the opposite: they want approximate timing, placed close to the transient itself. The engine anticipates the authored marker by two synthesis hops before each reset. At `N=4096`, `R_s=1024`, and 44100 Hz, that is 2048 samples, about 46.4 milliseconds. This lands the reset before the authored attack so the attack falls nearer the Hann window center / peak rather than being smeared by the hop crossfade. Phase resets are also placed only where loud transient content provides masking; quiet, exposed passages should carry one phase reset at their start and nothing else, since a reset during exposed material has nothing to hide behind.

The third pair, **tab A vs tab B**, selects between two snapshots of viewport, zoom, playhead, trim region, and selection. Marker stores and engine settings are shared across both tabs; only navigation state differs. The two-tab layout gets used throughout authoring, not just for aligning sections that exist already. Repeated phrases within a single passage benefit from label-locked tempos, and laying those labels down reliably means parking one tab at the first occurrence and the other at the second, then walking the lead-ins forward together to confirm the matching reference point. The same pattern applies at every scale — phrase repeats, sectional repeats, and the exposition-recapitulation relationship a sonata movement turns on. The second tab earns its place from the first repeated phrase onward.

### Warp marker model

A mandatory warp marker is present at the file's first frame, carrying tempo `1.00` by default. The classical phase-vocoder use case — one constant stretch ratio for the whole file — requires no further warp-marker authoring: the global stretch ratio is set entirely through the settings `scale` field (six-decimal precision), and the frame map remains a single constant. Warp markers as an authoring surface come into play only when the operator wants phrase-level tempo variation. The model below describes that authoring surface.

Each warp marker carries three independent pieces of state.

**Tempo source.** A marker either owns its own tempo or inherits the tempo from the nearest earlier owning marker. An owning marker presents an explicit numeric value; an inheriting marker (a "pass" marker) carries no tempo of its own and resolves its presentation tempo live by walking backward through the marker list.

**Label relationship.** A marker can declare itself a label origin (a `label_def`) or cite an existing label by name (a `label_ref`). A `label_ref` marker inherits its tempo from the named def rather than from positional walk-back. Changing the def's tempo updates every ref that cites it. This is the mechanism for tempo locking across recapitulated material: a sonata-form exposition section receives a `label_def`, the corresponding recapitulation section receives a `label_ref` back to that def, and the two sections will always render at the same tempo regardless of subsequent tempo edits. A marker carries at most one of `label_def` or `label_ref`; neither is the unlabeled default.

**Disabled flag.** A marker can be disabled, which silences its tempo contribution as if the marker were not present. Disabling a `label_def` cascades: every `label_ref` citing that def is treated as disabled too. Disabling a non-def marker is local and does not propagate.

An owning marker's tempo is a base value times a scale. The base value is two decimals in the range `0.01` to `9.99` and expresses the headline stretch ratio relative to the source recording — `1.00` is no stretch, `1.05` is 5% faster, `0.95` is 5% slower. The base value's precision is deliberately coarse; the user authors at the resolution they would set a metronome, not finer. The scale is a separate four-decimal multiplier defaulting to `1.0000`. The intended use is fine-tuning a marker's effective tempo to approximate a neighboring section's effective tempo when that neighbor uses a `label_ref` — the user reads off the neighbor's resolved `base*scale` from its hover popup and applies a matching scale to the marker being authored. The user can use scale for anything else they want; there are no enforced rules. The reference example in `examples/550 - 1/` shows both fields in use across a complete movement.

Phase reset markers carry two pieces of state: a position and a disabled flag. They have no tempo, no labels, no scale, no mode. The engine concern they parameterize is *where to reset* — re-grounding the synthesis phase at the transient that opens the segment. The small lead-in the engine applies before each reset is a fixed two synthesis hops, applied uniformly to every reset rather than being a per-marker setting.

## Output formats and the limiter

The `output_format` setting is one of `wav` (the default, finished audio), `framemap`, or `tempomap`; the latter two write warp and tempo maps for external stretch engines rather than audio, and the limiter does not apply to them.

The default wav output is brought to a delivery ceiling by a built-in limiter — a transparent spectral stage at -0.3 dBFS, then a 0 dBFS lookahead peak limiter with a hard-clip backstop — and written as 24-bit PCM. Setting `limiter=false` bypasses it entirely, writing the clean phase-vocoder output as 32-bit float for null-checking or for finishing in an external limiter.

## Hotkey reference

The reference is grouped by function. Within the prose of this document a grouped list is the one place a table-like layout earns its place; everything is keyboard unless marked as a mouse gesture. Bindings are case-described: `Ctrl+S` means Control held with the `s` key, `Shift+B` means Shift held with `b`. Where a key does different things in warp view versus phase-reset view, both are given.

**Files and session.** `Ctrl+S` saves all sidecar files. `Ctrl+Q` closes the window, and `Ctrl+W` reverts to the blank single-anchor state; both route through an unsaved-changes prompt when the session is dirty, and both also work while the settings prompt or a paste-confirmation prompt is open (they abandon that prompt first, without committing it).

**Playback and playhead.** `Space`, `Return`, or keypad `Enter` start and stop playback from the cursor to the trim end. `Left` and `Right` move the playhead one pixel and clear the marker selection; `Home` and `End` jump it to the trim begin and trim end. Any playhead move stops playback. `f` toggles follow mode, which scrolls the viewport to keep the playhead in view during playback. `Shift` plus a digit sets the playback speed: `Shift+0` is 1.00× (normal), and `Shift+1` through `Shift+9` set 0.10× through 0.90×; the change applies immediately whether or not playback is running.

**Zoom and view.** `Up` and `Down` (equivalently `=` and `-`) zoom in and out. `0` toggles between fit-to-file and the snap zoom level. `c` jumps to the snap zoom level and centers the viewport on the playhead. Ordinary zooming can go one numeric level deeper than the snap level. `PageUp` and `PageDown` page the viewport back and forward by one screen, retaining a small overlap so the edge of the previous view stays visible.

**View toggles.** `t` switches the audio domain between source and target (S/T) when `output_format=wav`; target view is unavailable for `framemap` and `tempomap`. `p` switches the marker collection between warp and phase reset (W/P). `o` toggles read-only on the active tab, which locks out marker, tempo, and phase-reset authoring while leaving navigation, playback, trim gestures, view toggles, `Ctrl+P` copy, and `Ctrl+S` available. `Ctrl+Tab` switches the active A/B navigation tab; `Ctrl+Shift+Tab` advances both tabs' marker focus in lockstep and lands on the opposite tab.

**Marker focus and selection.** `Tab` and `Shift+Tab` (the latter also reachable as the ISO left-tab key some layouts send) cycle marker focus forward and backward, recentering the current zoom on the focused marker; the focus walk skips disabled markers and operates on whichever collection the W/P toggle has active.

**Warp marker authoring (W view).** `s` drops a marker at the playhead copying the previous marker's tempo; `Shift+S` drops one at tempo 1.00. `n` drops an inheriting ("pass") marker; `Shift+N` toggles inherit on the focused marker. `Ctrl+D` toggles the disabled flag (this works in both W and P views). `Delete` deletes the selected marker, and `Shift+Delete` force-deletes it. `Ctrl+Up` and `Ctrl+Down` nudge the focused marker's tempo by ±0.01. `Ctrl+Left` and `Ctrl+Right` nudge the focused marker's position by one pixel.

**Phase reset authoring (P view).** `s` drops a phase reset at the playhead. `Ctrl+D` toggles its disabled flag. `Delete` deletes the selected phase reset. `Ctrl+Left` and `Ctrl+Right` nudge the focused phase reset by one pixel.

**Phase-reset propagation (W view, anchored on a warp-marker selection).** These copy a labeled region's phase-reset layout from one occurrence of a label to another. `Ctrl+P` copies: with exactly two warp markers selected, it captures the phase-reset layout of the region they bound into the session clipboard. `Ctrl+Alt+P` pastes: anchored on one selected warp marker, it materializes the clipboard's resets onto the destination region, scaled to the destination's durations, after a confirmation prompt. `Ctrl+Alt+Shift+P` pastes state only: it aligns the enabled/disabled state of the destination region's resets to the clipboard, in order, without moving anything and without a prompt. All three apply a 100 ms tolerance when deciding which block a phase reset belongs to: a reset falling within 100 ms before a block's owning warp marker counts as part of that block rather than the one before it. Copy captures such a reset with the block, and paste reproduces it at the same offset before the destination marker (clamped to the start of the file if that offset would fall before time zero). The tolerance is a fixed value, independent of the engine window size. All three are warp-view only, switch to phase-reset view on completion, and silently do nothing on an empty clipboard or the wrong selection count.

**Trim.** Bare `x` sets the begin trim at the playhead and autosets end half of the visible span later. `Shift+x` clears both bounds. The end bound is reachable by mouse: `Ctrl` with a left-drag on either bound repositions it; `Ctrl+Shift` with a left-drag on either bound moves the pair together as a fixed-width window, preserving the gap between them, clamped so neither bound leaves the clip. Selecting a bound and pressing `Delete` unsets it. Trim is per-tab and excluded from undo and redo.

**Modes (W view).** `i` toggles iteration mode; `Shift+I` clears all iteration values and exits the mode. `m` opens BPM mode on a two-marker span (re-pressing `m` or pressing `Esc` exits BPM mode). Iteration and BPM modes are mutually exclusive. Both are front ends for the same workflow: render a spread of variants, audition them in render view, and commit the one you want as the new baseline (render view, below, covers choosing and committing). They differ only in what they vary and how the variant is expressed.

Iteration mode varies a marker's base tempo by a small additive offset. With the mode on, the popup on a warp marker takes a bracketed range of signed two-decimal offsets, `[<start>,<end>]` with `start <= end` — for example `[-0.02,-0.00]`, which enumerates the offsets −0.02, −0.01, and 0.00 (the range is stepped in 0.01 increments, inclusive of both ends). Several markers can each carry a range; `Ctrl+Alt+I` then renders the full Cartesian product, one file per combination, each marker's offset added to its own base tempo for that cell, into a batch folder under `renders/` ready to audition. To sweep a marker across a single value rather than a range, give a degenerate range like `[-0.02,-0.02]` — it is still an iteration cell and still renders through `Ctrl+Alt+I`. The sign is mandatory and exactly two decimal places are required on every value, so `+0.00`, not `0` or `0.0`. Separately, typing a bare signed offset with no brackets — `-0.02` — is not an iteration cell at all: it immediately adds that offset to the marker's base tempo (clamped to the `0.01`–`9.99` range and snapped to two decimals) and clears any iteration range on the marker. That bare form is a direct tempo edit, the one iteration-popup entry that marks the document dirty; the bracketed forms are session-only and never serialized. An empty popup or `[]` clears the marker's iteration range.

BPM mode varies the rendered tempo of an explicit span to hit a range of target BPMs. Select exactly two warp markers and press `m`: the earlier marker owns the span and the later marker closes it (the closing marker's own section is not part of the span). Any markers between the two are span-internal. None of the markers in the span may be a `label_ref` — the span's tempo is rewritten on commit and a reference takes its tempo from its definition, so `m` is a silent no-op if a `label_ref` falls anywhere in the selected range (a selection that is not exactly two markers is also a no-op). With the mode on, the owner's popup is authored as `<beats>@[<lo>,<hi>]` — for example `36@[210,220]`, meaning the span spans 36 beats and should be rendered once at each integer BPM from 210 to 220 inclusive. The beat count and the span's measured duration (owner marker to closing marker) fix the source tempo, so each target BPM resolves to a concrete base-tempo-and-scale pair. `Enter` renders the sweep — one file per integer BPM in the range — into a batch folder under `renders/` ready to audition. Committing a BPM cell with `Ctrl+Alt+C` rewrites the span's tempo: the owner takes the computed base tempo, every span-internal marker (including disabled ones) is set to pass so the span renders at one tempo, the closing marker is left unchanged, and the computed scale is written to the settings as the global scale. The settings `bpm` field records the committed render as a descriptor — for example `36 beats @ 220 bpm from 00:32.008 to 00:46.562` — for provenance only; it has no engine or GUI effect.

**Settings.** `:` (that is, `Shift+;`) opens the settings prompt in the bottom strip, where a line is typed as `key=value`. The prompt accepts any printable character, and the line is split on the first `=`, so the value may itself contain `=` — a `url=` carrying a `?v=` query parameter, for instance, commits intact. The key must be whitespace-free; keys that require a restricted value (such as `scale` or `output_format`) still reject an out-of-range value at commit with a red outline. With an engine key typed and the value left empty (`notes=`), `Tab` fills in that key's current value for recall and editing, and leaves a value you have already started typing untouched. `Enter` commits, `Esc` cancels, and `Ctrl+Q` / `Ctrl+W` abandon the edit and close or revert.

Four settings fields are inert provenance: they are carried in `.settings` but never read by the engine or acted on by the GUI. `bpm` is written automatically when a BPM-sweep cell is committed (described under the render dispatches above); `url`, `notes`, and `cover` are free-text fields the operator types through the settings prompt. None of the four is validated — any value is accepted verbatim, including empty — and none is required, so a `.settings` file may omit them. They annotate a render with its source, its target, working notes, or a cover-art reference. Each holds a single line, since the `.settings` format is line-based.

**Render queue and dispatch.** `Ctrl+Alt+R` is the pipeline's destination: a single one-shot render of the current authoring state, written next to the source as a sibling named by the `title` setting (`<title>.wav`, or `<title>.warpframemap` / `.tempomap` for the non-wav output formats). When `limiter=false`, the sibling wav is written with a `limiter=false;<title>.wav` filename prefix that marks the clean render, so a bypassed output never collides with the limited deliverable of the same title. It is the only render that lands beside the source rather than inside a `renders/` batch folder — the final deliverable, as opposed to the audition outputs the other dispatches produce. It defensively rejects an overlapping submission while a render is already in flight; it does not check the target path first, so a render whose `title` matches an existing render silently overwrites it. The one exception is the source audio itself: an output path that resolves to the source file is refused with an error rather than overwritten (change the `title` setting). The remaining dispatches stage candidates for auditioning rather than producing the final file: `Ctrl+E` snapshots the current authoring state into the in-memory render queue without writing to disk, and `Ctrl+Alt+E` renders the whole queue into a batch folder under `renders/`, auto-enqueuing the current state if the queue is empty (pressing `Esc` between entries drops the remainder). The iteration and BPM sweeps are dispatched from their own modes, documented in the Modes group above.

**Render view.** `r` toggles the render-analysis view, which enumerates the `renders/` folder (source audio must be loaded). In render view, `Shift+Left` and `Shift+Right` step through the render list with wraparound, and `Shift+Home` / `Shift+End` jump to the first and last entries without wraparound. `Ctrl+Alt+C` commits the displayed render as the new authoring baseline, then exits render view and wipes the `renders/` folder (the chosen render's parameters are now the baseline, so the prior batch outputs are stale). What it pulls in depends on which kind of render is displayed, which the batch folder name makes visible: for a regular render or an iteration cell it commits the warp markers and phase reset markers only; for a BPM-sweep cell it commits those (the span markers already carry the rewritten tempo — owner to the computed base tempo, span-internal markers to pass) and additionally writes the cell's `scale` as the global scale and a `bpm` descriptor string into the settings (the `bpm` field is informational only — it records what the chosen render was targeting, as text like `36 beats @ 220 bpm from 00:32.008 to 00:46.562`, and has no effect on the engine or the GUI). `Tab` and `Shift+Tab` cycle marker focus through the displayed render's markers, and `p` switches the displayed collection between warp and phase reset. Render view is a read-only printout of the target, so it has no source/target axis and no A/B tabs: `t`, `Ctrl+Tab`, and `Ctrl+Shift+Tab` do nothing there.

**General.** `Ctrl+Z` undoes and `Ctrl+Shift+Z` redoes; settings edits and marker edits participate in the same undo history. Trim changes are excluded from undo and redo. `Esc` cancels an in-flight async render, queue run, or playhead drag, and dismisses an open editor or prompt; with nothing pending it is a no-op.

**Mouse.** The scroll wheel zooms; held with `Alt` it pans the viewport. Held with `Ctrl`, the wheel nudges the focused warp marker's base tempo by ±0.01 per detent — wheel up lowers, wheel down raises — the mouse equivalent of `Ctrl+Up` / `Ctrl+Down`; it acts only when a warp marker is selected and does nothing in read-only or render view. When the begin trim bound is the last-selected item, `Ctrl+wheel` moves the end trim bound instead (wheel down moves end later, wheel up moves end earlier), in finer steps than `Alt+wheel` pan. A single left-click selects the marker or sets the playhead at the click point. `Ctrl` with a left-drag on a marker repositions it, the mouse equivalent of the `Ctrl+Left` / `Ctrl+Right` nudge; `Alt` with a left-drag pans the viewport.

## Examples

The repository's `examples/` directory ships reference sidecar files. The recommended set is `examples/550 - 1/` — Symphony No. 40, first movement — which is fully realized: it carries `.warpmarkers`, `.phaseresetmarkers`, `.settings`, and `.notes`, and exercises every form and syntax the project uses (owning and inheriting markers, label definitions and references, scale fine-tuning, phase resets placed under masking, and a complete settings block). Load it as described in the First run section, supplying your own copy of the source recording under the basename the sidecar files expect.

The source recording is commercially licensed and is not distributed with the project. A lossy reference copy is available so a reader can hear the example without owning the release:

[Reference source](https://music.youtube.com/watch?v=f10ISOkJZuA&list=OLAK5uy_nMff2yJASrC9u9uf4b0uPZYoiDt-MdTh8)

That copy is a lossy reference only — enough to follow the example and hear what the markers do. The lossless commercial release is the real source, and the one to author against for any serious work; the audible limits of the copy are part of the point.

Example output, also in lossy audio format:

[Symphony No. 40](https://www.youtube.com/playlist?list=PLm5sJJQZOLT1OkUITQ4vX2l20qzGylkqI)
