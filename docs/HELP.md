# warptempo_gui

[ Section 1: Install / build / first run. Further sections follow. ]

## Dependencies

Build requires a C++17 toolchain (GCC 9+ or Clang 10+), CMake 3.10+,
pkg-config, and the wayland-scanner tool plus the development headers
for cairo, fftw3, libsndfile, wayland-client, wayland-cursor,
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
backend. Tested compositors include labwc, sway, and Hyprland;
GNOME and KDE Wayland sessions should work but are not regularly
exercised.

## Build

Standard out-of-tree CMake build from the repository root:

    cmake -B build -S .
    cmake --build build -j$(nproc)

This produces a single executable at `build/warptempo_gui`. The
engine sources under `src/engine/` link into the same target — there
is no separate engine library. `-O3 -march=native` is always on for
GCC and Clang, so the binary is tuned for the host CPU and is not
portable across machines; rebuild on the target host.

There is no `make install` target. The binary is self-contained:
copy it to anywhere on `$PATH` (typically `~/.local/bin/` or
`/usr/local/bin/`) to make it available system-wide.

    install -Dm755 build/warptempo_gui ~/.local/bin/warptempo_gui

A `.desktop` file is shipped in `packaging/warptempo_gui.desktop`.
To register warptempo_gui with the system's application launcher
and as a handler for audio files, install it to the user-level
application directory and refresh the desktop database:

    install -Dm644 packaging/warptempo_gui.desktop \
        ~/.local/share/applications/warptempo_gui.desktop
    update-desktop-database ~/.local/share/applications

The shipped `.desktop` file uses the freedesktop `audio-x-generic`
icon name, which resolves to whatever the user's active icon theme
provides — no icon file is bundled. Replace the `Icon=` line with
a different freedesktop name or an absolute path to a custom SVG
to override.

## First run

warptempo_gui takes a single argument: the path to a source audio
file (typically `.wav` or `.flac`, though anything libsndfile reads
will load). Marker and settings files for that audio file live in
the same directory as the audio file itself, named by appending
`.warpmarkers`, `.phaseresetmarkers`, and `.settings` to the audio
file's basename.

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
directory (`examples/550 - 1/`) is the recommended first load — it
exercises every form and syntax used in the project. The audio
file itself is not included (the corpus is commercially licensed);
the user supplies the source recording and places it in the
example directory under the basename the sidecar files expect.

The conceptual model and hotkey reference are in the sections
below.
