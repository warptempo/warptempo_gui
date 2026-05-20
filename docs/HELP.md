# warptempo_gui

[ Section 1: Install / build / first run. Further sections follow. ]

## Dependencies

Build requires a C++17 toolchain (GCC 9+ or Clang 10+), CMake
3.10+, pkg-config, and the wayland-scanner tool plus the development
headers for cairo, fftw3, libsndfile, wayland-client, wayland-cursor,
wayland-protocols, libxkbcommon, and JACK. JACK can be the native
jackd2 server or a PipeWire-shimmed JACK provider (modern distros
default to the latter).

Arch Linux:

    sudo pacman -S base-devel cmake pkgconf \
        cairo fftw libsndfile \
        wayland wayland-protocols libxkbcommon \
        pipewire-jack

Debian / Ubuntu:

    sudo apt install build-essential cmake pkg-config \
        libcairo2-dev libfftw3-dev libsndfile1-dev \
        libwayland-dev wayland-protocols libxkbcommon-dev \
        libjack-jackd2-dev

The libwayland-dev package on Debian ships wayland-scanner and the
client / cursor headers in one piece.

Fedora:

    sudo dnf install gcc gcc-c++ cmake pkgconf-pkg-config \
        cairo-devel fftw-devel libsndfile-devel \
        wayland-devel wayland-protocols-devel libxkbcommon-devel \
        pipewire-jack-audio-connection-kit-devel

A Wayland compositor is required at runtime — there is no X11
backend. Tested compositors include labwc, sway, and Hyprland; GNOME
and KDE Wayland sessions should work but are not regularly exercised.

## Build

Standard out-of-tree CMake build from the repository root:

    cmake -B build -S .
    cmake --build build -j$(nproc)

This produces a single executable at `build/warptempo_gui`. The engine
sources under `src/engine/` link into the same target — there is no
separate engine library. `-O3 -march=native` is always on for GCC and
Clang, so the binary is tuned for the host CPU and is not portable
across machines; rebuild on the target host.

There is no `make install` target. The binary is self-contained:
copy it to anywhere on `$PATH` (typically `~/.local/bin/` or
`/usr/local/bin/`) to make it available system-wide.

    install -Dm755 build/warptempo_gui ~/.local/bin/warptempo_gui

A `.desktop` file is shipped in `packaging/warptempo_gui.desktop`. To
register warptempo_gui with the system's application launcher and as
a handler for audio files, install it to the user-level application
directory and refresh the desktop database:

    install -Dm644 packaging/warptempo_gui.desktop \
        ~/.local/share/applications/warptempo_gui.desktop
    update-desktop-database ~/.local/share/applications

The shipped `.desktop` file uses the freedesktop `audio-x-generic` icon
name, which resolves to whatever the user's active icon theme provides
— no icon file is bundled. Replace the `Icon=` line with a different
freedesktop name or an absolute path to a custom SVG to override.

## First run

warptempo_gui takes a single argument: the path to a source audio file
(typically `.wav` or `.flac`, though anything libsndfile reads will
load). Marker and settings files for that audio file live in the same
directory as the audio file itself, named by appending `.warpmarkers`,
`.phaseresetmarkers`, and `.settings` to the audio file's basename.

    warptempo_gui "path/to/source.wav"

On launch, warptempo_gui opens a window, reads any existing
`.warpmarkers`, `.phaseresetmarkers`, and `.settings` sidecar files
for the given audio file, and presents the source waveform across
the full window width. With no sidecar files present, the window
opens with a single warp marker at frame zero (the immovable anchor)
and no phase reset markers. With sidecar files present, the prior
session state is restored verbatim — viewport, zoom, playhead,
active view selectors, and all marker positions and flags.

The examples directory in the repository ships three reference
movements with complete sidecar files. The Symphony No. 40 mvt I
directory (`examples/550 - 1/`) is the recommended first load —
it exercises every form and syntax used in the project. The audio
file itself is not included (the corpus is commercially licensed);
the user supplies the source recording and places it in the example
directory under the basename the sidecar files expect.

The conceptual model and hotkey reference are in the sections below.

## Conceptual model

### Engine and timemap

warptempo_gui drives a phase vocoder. Internally this is a
Laroche-Dolson identity phase-locking phase vocoder. FFT size
N defaults to 4096 and is user-configurable through settings;
the synthesis hop R_s is derived as N/4 (75% overlap) and is not
independently settable. The engine inherits the source recording's
sample rate — 44100 Hz for the Krips reference material this project
was built around. The phase-locking keeps bins coherent within each
frame so steady-state content — held strings, sustained winds, room
tone — stretches without smearing into a chorused or phasey artifact.

Variable-rate time-stretching with user-placed warp markers exists in
other tools — DAWs that ship warp-marker editing on top of their
bundled time-stretch algorithm have offered this for years. What
separates this project is per-marker tempo locking through label
definitions and references. A marker can declare a named label; a
later marker can reference that label by name; the two markers and
every other reference to the same label render at the same effective
tempo, and editing the def's tempo updates every ref that cites it. The
mechanism is described in the warp marker model section below. The use
case it was built around is the sonata-form exposition/recapitulation
relationship in classical orchestral repertoire, where the same musical
material returns multiple times across a movement and should hold a
coherent tempo relationship across all occurrences regardless of how
the surrounding material is edited.

Identity phase-locking is what keeps steady-state content clean. It
is also exactly what does not work at transients. An attack — oboe
entry, pizzicato, timpani strike — is a moment where the spectrum
reorganizes within a few hops, and the phase relationships the vocoder
has been propagating no longer correspond to the acoustic event
arriving. The attack smears across surrounding frames. The mitigation
is a phase reset: at a user-chosen position, the synthesis phase is
reset rather than propagated, sacrificing local coherence in exchange
for a clean transient. Phase reset markers are the authoring surface
for this. They are a separate marker collection from warp markers,
visible and edited in their own view, because they parameterize a
distinct engine concern: warp markers shape the timemap, phase reset
markers protect transients.

### View axes

The bottom bar of the window shows three letter pairs: `[S/T] [W/P]
[A/B]`. Each pair is an independent toggle. All three combinations
are valid; the GUI's state is the product of all three selections
plus whatever modes happen to be active on top.

The first pair, **source vs target**, selects the audio domain of the
waveform under the playhead. Source view (S) shows the unprocessed
source recording with warp markers drawn at the times the user authored
them. Target view (T) shows the same audio after the engine has
applied the live timemap — the waveform is stretched and compressed
to reflect what the render will sound like, with markers drawn at
their post-stretch positions. The two views serve different stages of
the same authoring task. Source view is essential for the horizontal
placement of new markers, where the user is pinning the marker's
time against an acoustic event in the unprocessed recording. Once
a skeleton of markers is in place, target view becomes the more
efficient place to work for the rest of the session — modulating
tempos, enabling and disabling markers, and judging the warp by ear
directly. Trim narrowed to the section under revision keeps target
renders fast enough for this loop to feel immediate.

The second pair, **warp vs phase reset**, selects which marker
collection is visible and edited. Warp markers (W) drive the
timemap. Phase reset markers (P) protect transients. The two
collections are stored, authored, and rendered independently; toggling
between them swaps which collection the marker hotkeys operate on
but changes nothing else. Authoring proceeds in warp view first,
since phase resets have no useful position until the timemap exists.

Beyond the data-model difference, the two collections want different
placement discipline. Warp markers benefit from a lead-in: rather than
landing exactly on a transient, place the marker slightly earlier, in
the fading tail of the previous material. The lead-in matters most for
label-bearing markers and for markers adjacent to label-bearing ones,
where the tempo precision the label mechanism enforces only pays off
if the underlying timing is itself precise. It also matters for quiet,
slow, or legato onsets where the acoustic event itself has no sharp
attack to pin to — pinning against the louder fade of the prior
note is more reliable than guessing where the quiet onset begins. The
A/B tab pair (described below) is the natural way to set lead-ins
precisely, matching the lead-up across paired sections. Phase reset
markers are the opposite: they want approximate timing, placed close
to the transient itself. The `phase_reset_offset_hops` engine setting
(default `1`) gives the engine a small lead-in of one synthesis hop
— about 23 milliseconds at the default N — which keeps the hop
crossfade from smearing the reset across the transient. Phase resets
are also placed only where loud transient content provides masking;
quiet, exposed passages should carry one phase reset at their start
and nothing else, since a reset during exposed material has nothing
to hide behind.

The third pair, **tab A vs tab B**, selects between two snapshots of
viewport, zoom, playhead, trim region, and selection. Marker stores
and engine settings are shared across both tabs; only navigation state
differs. The two-tab layout gets used throughout authoring, not just
for aligning sections that exist already. Repeated phrases within
a single passage benefit from label-locked tempos, and laying those
labels down reliably means parking one tab at the first occurrence
and the other at the second, then walking the lead-ins forward
together to confirm the matching reference point. The same pattern
applies at every scale — phrase repeats, sectional repeats, and the
exposition-recapitulation relationship a sonata movement turns on. The
second tab earns its place from the first repeated phrase onward.

### Warp marker model

Each warp marker carries three independent pieces of state.

**Tempo source.** A marker either owns its own tempo or inherits
the tempo from the nearest earlier owning marker. An owning marker
presents an explicit numeric value; an inheriting marker (a "pass"
marker) carries no tempo of its own and resolves its presentation
tempo live by walking backward through the marker list.

**Label relationship.** A marker can declare itself a label origin
(a `label_def`) or cite an existing label by name (a `label_ref`). A
`label_ref` marker inherits its tempo from the named def rather than
from positional walk-back. Changing the def's tempo updates every
ref that cites it. This is the mechanism for tempo locking across
recapitulated material: a sonata-form exposition section receives
a `label_def`, the corresponding recapitulation section receives a
`label_ref` back to that def, and the two sections will always render
at the same tempo regardless of subsequent tempo edits. A marker
carries at most one of `label_def` or `label_ref`; neither is the
unlabeled default.

**Disabled flag.** A marker can be disabled, which silences its tempo
contribution as if the marker were not present. Disabling a `label_def`
cascades: every `label_ref` citing that def is treated as disabled
too. Disabling a non-def marker is local and does not propagate.

An owning marker's tempo is a base value times a scale. The base
value is two decimals in the range `0.01` to `9.99` and expresses
the headline stretch ratio relative to the source recording —
`1.00` is no stretch, `1.05` is 5% faster, `0.95` is 5% slower. The
base value's precision is deliberately coarse; the user authors
at the resolution they would set a metronome, not finer. The scale
is a separate four-decimal multiplier defaulting to `1.0000`. The
intended use is fine-tuning a marker's effective tempo to approximate
a neighboring section's effective tempo when that neighbor uses a
`label_ref` — the user reads off the neighbor's resolved `base*scale`
from its hover popup and applies a matching scale to the marker being
authored. The user can use scale for anything else they want; there
are no enforced rules. The reference example in `examples/550 - 1/`
shows both fields in use across a complete movement.

Phase reset markers carry only a position and a disabled flag. They
have no tempo, no labels, no scale. The engine concern they
parameterize is a single bit ("reset phase here") and the marker's data
model reflects that. The small per-marker lead-in the engine applies
before each reset is set globally through `phase_reset_offset_hops`
(default `1`, one synthesis hop), not per-marker.

The next sections walk through authoring an example movement end to
end and list the full hotkey reference.
