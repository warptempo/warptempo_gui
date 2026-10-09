# Statically linked dependencies

Every library compiled INTO a binary of this project, with its licence. The repository distributes no binary: the
laptop builds its own (`CMakeLists.txt`), the tablet's libraries are cross-built into the gitignored
`android/prebuilt/` (`android/deps/`, each source tarball pinned by sha256 in `android/deps/common.sh`, provenance in
`android/NOTES.md` §6) and the APK is installed on one tablet only. The licence texts live in each pinned source; this
file is the list. Libraries the laptop's binary loads from the system at run time (cairo, HarfBuzz, FreeType, fftw,
Wayland, xkbcommon, JACK, libgit2) are the distribution's and are not listed. The faces and the icons are data, not
code: `fonts/README.md`, `assets/icons/tango/README.md` and `assets/icons/mist/README.md` carry their provenance and
licence texts. The faces are compiled into `warptempo_gui` (and packed into the APK), among
them GNU FreeFont's FreeSans and FreeSans Bold (20120503; GPL-3.0-or-later with the font exception, the texts in
`fonts/LICENSE-FreeFont.txt`, since 2026-10-09) and Liberation Sans and Liberation Sans Bold (2.1.5; SIL Open Font
License 1.1, the text in `fonts/LICENSE-Liberation-OFL.txt`, since 2026-10-09); the repository distributes no binary.

## Both devices

| Library | Version | Licence |
|---|---|---|
| resvg's C API (`resvg-capi`, the icon renderer, `src/gui/svg_icon.cpp`), with resvg and usvg | 0.48.1 | Apache-2.0 OR MIT (`LICENSE-APACHE`, `LICENSE-MIT`; © 2017 the Resvg Authors) |
| its 37 crates (below) | Cargo.lock at the pin | MIT, Apache-2.0, BSD-2-Clause, BSD-3-Clause, Zlib, 0BSD, Unlicense, each crate's own |
| the Rust standard library inside `libresvg.a` (core, alloc, std and their crates) | rustc 1.99.0 | MIT OR Apache-2.0 |

## The tablet's APK only

| Library | Version | Licence |
|---|---|---|
| fftw (with fftw_threads) | 3.3.11 | GPL-2.0-or-later |
| FreeType | 2.14.3 | FTL OR GPL-2.0-or-later |
| HarfBuzz | 14.3.1 | MIT (HarfBuzz's "Old MIT" `COPYING`) |
| pixman | 0.46.4 | MIT |
| cairo | 1.18.4 | LGPL-2.1-only OR MPL-1.1 |
| OpenSSL (libcrypto) | 3.6.4 | Apache-2.0 |
| libssh2 | 1.11.1 | BSD-3-Clause |
| libgit2 (with its bundled zlib, llhttp, SHA-1 collision detection) | 1.9.7 | GPL-2.0-only with the linking exception (its `COPYING`, which also states each bundled piece's licence) |
| the NDK's libc++, libc++abi and libunwind (`-static-libstdc++`), and `android_native_app_glue` | NDK r29 | Apache-2.0 WITH LLVM-exception; the glue Apache-2.0 |

## resvg's crates

The packages of `cargo tree -p resvg-capi --no-default-features --features raster-images -e normal` at the pin, the
same on both targets, with each crate's declared licence and the copyright line of its licence file (a dash where
the file names no holder). Regenerate from the tarball's `Cargo.lock` when the pin moves.

| Crate | Version | Licence | Copyright |
|---|---|---|---|
| adler2 | 2.0.1 | 0BSD OR MIT OR Apache-2.0 | Jonas Schievink |
| arrayref | 0.3.9 | BSD-2-Clause | 2015 David Roundy |
| arrayvec | 0.7.8 | MIT OR Apache-2.0 | Ulrik Sverdrup "bluss" 2015-2023 |
| bitflags | 2.13.1 | MIT OR Apache-2.0 | 2014 The Rust Project Developers |
| bytemuck | 1.25.2 | Zlib OR Apache-2.0 OR MIT | 2019 Daniel "Lokathor" Gee |
| byteorder-lite | 0.1.0 | Unlicense OR MIT | 2015 Andrew Gallant |
| cfg-if | 1.0.4 | MIT OR Apache-2.0 | 2014 Alex Crichton |
| color_quant | 1.1.0 | MIT | 2016 PistonDevelopers |
| crc32fast | 1.5.0 | MIT OR Apache-2.0 | 2018 Sam Rijs, Alex Crichton and contributors |
| data-url | 0.3.2 | MIT OR Apache-2.0 | 2013-2025 The rust-url developers |
| fdeflate | 0.3.7 | MIT OR Apache-2.0 | – |
| flate2 | 1.1.9 | MIT OR Apache-2.0 | 2014-2026 Alex Crichton |
| float-cmp | 0.9.0 | MIT | 2014-2020 Optimal Computing (NZ) Ltd |
| gif | 0.14.2 | MIT OR Apache-2.0 | 2015 nwin |
| image-webp | 0.2.4 | MIT OR Apache-2.0 | – |
| imagesize | 0.15.0 | MIT | 2017 Maiddog |
| kurbo | 0.13.1 | Apache-2.0 OR MIT | 2018 Raph Levien |
| log | 0.4.33 | MIT OR Apache-2.0 | 2014 The Rust Project Developers |
| memchr | 2.8.3 | Unlicense OR MIT | 2015 Andrew Gallant |
| miniz_oxide | 0.8.9 | MIT OR Zlib OR Apache-2.0 | 2013-2014 RAD Game Tools and Valve Software; 2010-2014 Rich Geldreich and Tenacious Software LLC; 2017-2024 oyvindln; 2017, 2020 Frommi |
| pico-args | 0.5.0 | MIT | 2019 Yevhenii Reizner |
| png | 0.18.1 | MIT OR Apache-2.0 | 2015 nwin |
| polycool | 0.4.0 | MIT OR Apache-2.0 | 2018 Raph Levien |
| quick-error | 2.0.1 | MIT/Apache-2.0 | 2015 The quick-error Developers |
| rgb | 0.8.53 | MIT | 2019 Kornel |
| roxmltree | 0.21.1 | MIT OR Apache-2.0 | 2018 Yevhenii Reizner |
| simd-adler32 | 0.3.10 | MIT | 2021 Marvin Countryman |
| simplecss | 0.2.2 | Apache-2.0 OR MIT | 2018 Reizner Evgeniy |
| siphasher | 1.0.3 | MIT/Apache-2.0 | 2012-2016 The Rust Project Developers; 2016-2026 Frank Denis |
| smallvec | 1.15.2 | MIT OR Apache-2.0 | 2018 The Servo Project Developers |
| strict-num | 0.1.1 | MIT | 2022 Yevhenii Reizner |
| svgtypes | 0.16.1 | Apache-2.0 OR MIT | 2018 Yevhenii Reizner |
| tiny-skia | 0.12.0 | BSD-3-Clause | 2011 Google Inc.; 2020 Yevhenii Reizner |
| tiny-skia-path | 0.12.0 | BSD-3-Clause | 2011 Google Inc.; 2020 Yevhenii Reizner |
| weezl | 0.1.12 | MIT OR Apache-2.0 | HeroicKatora 2020 |
| zune-core | 0.5.1 | MIT OR Apache-2.0 OR Zlib | zune-image developers |
| zune-jpeg | 0.5.15 | MIT OR Apache-2.0 OR Zlib | zune-image developers |
