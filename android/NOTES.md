# Android bootstrap — toolchain + dependency sysroot (M1, 2026-08-23)

What this directory is: the host-side Android toolchain bootstrap and the
cross-compiled dependency sysroot for `android-arm64`, ready for the M2 spike
APK. **Nothing here touches the product build.** No file under `src/`,
`tools/`, `docs/`, `projects/` or the root `CMakeLists.txt` was created or
edited; `android/` is a new top-level directory and the desktop build is
bit-for-bit what it was.

Everything is reproducible from these scripts alone: the host installs are
checksum-pinned downloads under `~/.local/android`, and every dependency
tarball is pinned by SHA-256.

---

## 1. What is installed, and where

Host installs live **outside the repo**, under `$WT_ANDROID_ROOT`
(default `~/.local/android`). Nothing was installed system-wide; no pacman/AUR
package was used, so no system-wide config changed.

| Piece | Exact version | Path |
|---|---|---|
| NDK | **r29 = 29.0.14206865**, clang 21.0.0 (Android 13989888, based on r563880c) | `~/.local/android/ndk/android-ndk-r29` |
| SDK build-tools | **36.0.0** (aapt2 2.20-13193326, zipalign, apksigner, d8) | `~/.local/android/sdk/build-tools/36.0.0` |
| Platform (android.jar) | **android-35** (`platform-35_r02`) | `~/.local/android/sdk/platforms/android-35` |
| JDK | **Temurin 21.0.12.1+1 (LTS)** | `~/.local/android/jdk` |
| Download cache | — | `~/.local/android/cache` |
| Source + build trees | — | `~/.local/android/work` |
| Generated meson cross file | — | `~/.local/android/meson/android-aarch64.ini` |
| Generated pkg-config wrapper | — | `~/.local/android/bin/pkg-config-android` |
| **Dependency sysroot** | — | **`android/prebuilt/arm64-v8a` (in-repo, 11 MB)** |

**NDK r29 is the newest STABLE NDK.** The SDK repository manifest lists
`ndk;30.0.14904198` and `ndk;30.0.15729638` under a "stable" channel ref, but
their archives are `android-ndk-r30-beta1-linux.zip` and
`android-ndk-r30-beta2-linux.zip` — the channel ref is wrong, the filenames are
not. r28+ is the hard floor (16 KB alignment by default); r29 is what the
research doc and termux-packages both use.

**The JDK is a private install because this host had no Java at all** — no
`java`, no `javac`, no `keytool` on `PATH` (checked first, as the brief
directs). `apksigner`, `d8` and `keytool` are Java programs, so M2 needs one.
`30_fetch_jdk.sh` no-ops if a system JDK ever appears.

**One host tool is still missing for M2, deliberately not installed:** `zip`.
The packaging step (`zip -u -0 -X` to store `libwarptempo.so` uncompressed) needs
it and build-tools does not ship one. Installing it is a pacman action on the
system, i.e. an architect call, so it is left for M2 — as is `android-tools`
(`adb`) and `android-udev`.

## 2. Script inventory

Everything is idempotent and safe to re-invoke after a partial failure: fetches
are cached and checksum-verified, unpacks are staged then moved into place, and
every build wipes its own build tree first so a re-run means the same thing as
a first run. (Verified: `10_fftw3.sh` was re-run end to end after the sysroot
was already populated.)

```
android/toolchain/
  00_env.sh              sourced by everything; versions, paths, flags, helpers.
                         ONE knob (WT_API) sets the API level of the whole sysroot.
  10_fetch_ndk.sh        NDK r29, sha1-verified against the SDK manifest
  20_fetch_build_tools.sh build-tools 36.0.0 + android-35 platform, sha1-verified
  30_fetch_jdk.sh        Temurin 21 (skips itself if a system JDK exists)
  40_gen_cross_file.sh   writes the meson cross file + the pkg-config wrapper
  bootstrap.sh           all four, in order

android/deps/
  common.sh              pinned source URLs/SHA-256, fetch/unpack/verify helpers,
                         the shared `meson setup` argument list
  10_fftw3.sh            fftw 3.3.11   (autotools)
  20_freetype.sh         freetype 2.14.3
  30_harfbuzz.sh         harfbuzz 14.3.1
  40_pixman.sh           pixman 0.46.4
  50_cairo.sh            cairo 1.18.4
  build_all.sh           the five in dependency order, then the smoke TU
  smoke/smoke.cpp        one TU including all five headers
  smoke/build_smoke.sh   compile+link at two API levels, then the 16 KB check

android/prebuilt/arm64-v8a/   include/ lib/ lib/pkgconfig/ lib/cmake/  (the output)
```

The cross file and the pkg-config wrapper are **generated**, not committed: they
carry absolute paths, the NDK version and the API level. Re-run
`40_gen_cross_file.sh` after changing either.

## 3. API levels — and the minSdk choice

**targetSdk 35, not 36** (given): Android 16 silently revokes `screenOrientation`
and `resizableActivity` on displays ≥ 600dp, which the Tab S10 FE is. `android.jar`
is therefore the android-35 one.

**minSdk 30 (Android 11)** — this is the choice the brief asked me to make and
justify. The floor and the ceiling both leave it free:

- The **install-time floor** is targetSdk ≥ 24 on Android 15 and 16 (Android 16
  did not raise it). Nothing about minSdk is constrained by it.
- The **only target device** is a Tab S10 FE on Android 15/16 — API 35/36. The
  whole range 21…35 installs; there is no compatibility to buy.
- So the question is only "what does a higher minSdk make unconditional?", and
  30 is where the list this port actually needs runs out: **AAudio** (26) with
  `setUsage`/`setContentType` (28), **`AChoreographer_postFrameCallback64`** (29,
  the non-deprecated form), **`ANativeWindow_setFrameRate`** (30). At minSdk 30
  none of those needs a version guard or a `dlsym` dance — and the frame-pacing
  pair is exactly what the ~8 ms scanner tick will want if the playhead judders.
- Above 30 the only prize is `AChoreographer_postVsyncCallback` (33), which the
  research doc rates "if you ever cared". Raising to 33 stays free if it is ever
  wanted — it is one line in `00_env.sh` plus a rebuild.

The research doc recommends 28; 30 is the same reasoning carried two levels
further, for the two frame-pacing entry points 28 does not reach.

**DEVIATION — the deps are built at API 30 too, not at the doc's 24.** The doc's
24 is termux's default and exists to serve a wide device population; this port
has exactly one device. Building the deps and the app at the same level means
one set of bionic headers across the whole sysroot and no mixed-header question
at all, and every configure probe the doc cites for 24 (`newlocale`, `strtod_l`,
64-bit `off_t`, DT_RUNPATH) is satisfied a fortiori at 30. `WT_API` in
`00_env.sh` is the single knob if that judgement is ever reversed.

## 4. Library build results

All five built clean for `aarch64-linux-android`, **static only**, **no source
patches**, no forced-shared exceptions.

| Library | Version | Result | The flag that mattered |
|---|---|---|---|
| fftw3 | 3.3.11 | `libfftw3.a` (2.4 MB) + `libfftw3_threads.a` (48 KB) | `--enable-neon` — verified `#define HAVE_NEON 1` in `config.h` and **52 NEON codelet objects** (`neon.o`, `n1fv_*`, `n2fv_*`, …) inside the archive. Double precision throughout; no `--enable-float`. |
| freetype | 2.14.3 | `libfreetype.a` (1.1 MB) | `-Dharfbuzz=disabled` — the trap is real and was verified avoided: installed `ftoption.h` has both `FT_CONFIG_OPTION_USE_HARFBUZZ` and `..._DYNAMIC` commented out, and the archive has **zero `dlopen` references**. `-Dzlib=internal`, so no external zlib anywhere. |
| harfbuzz | 14.3.1 | `libharfbuzz.a` (3.1 MB) | `-Dfreetype=enabled -Dcairo=disabled`; `hb-ft.h` installed. raster/vector/gpu/subset/utilities all explicitly disabled. |
| pixman | 0.46.4 | `libpixman-1.a` (966 KB) | `-Da64-neon=enabled` — "NEON A64 Intrinsic Support: YES", and `pixman-arma64-neon-asm{,-bilinear}.S.o` are inside the installed archive. No cpu-features patch needed, exactly as the doc says. |
| cairo | 1.18.4 | `libcairo.a` (1.4 MB) | `-Dfontconfig=disabled -Dfreetype=enabled`. Verified in the installed `cairo-features.h`: `CAIRO_HAS_FT_FONT 1`, `CAIRO_HAS_IMAGE_SURFACE 1`, and `CAIRO_HAS_FC_FONT` **absent**. |

Build order is the doc's straight line — freetype (harfbuzz off) → harfbuzz;
pixman → cairo; fftw3 independent. **FreeType pass 2 is skipped** (it only
improves autohinting for complex scripts; this product paints 12pt Latin).

`fftw3_threads` is built rather than dropped **because `src/engine/` is frozen**:
`fftw_init_threads` / `fftw_plan_with_nthreads(1)` exist there as a determinism
invariant, so the library is provided and no frozen source is touched. There is
no `fftw3_threads.pc` upstream — the product's existing `find_library` pattern
ports unchanged, and the smoke link names `-lfftw3_threads` by hand for the same
reason.

Housekeeping in the prefix: `make install` for fftw also drops aarch64
`fftw-wisdom` binaries + man pages and libtool `.la` files with absolute host
paths into the prefix; `10_fftw3.sh` removes all three. The `lib/cmake/` configs
from fftw and harfbuzz are kept — the M2 CMake build may want them.

## 5. Smoke compile-and-link

`android/deps/smoke/smoke.cpp` includes `fftw3.h`, `cairo.h`, `cairo-ft.h`,
`hb.h`, `hb-ft.h`, `ft2build.h`/`FT_FREETYPE_H`, and calls one real function from
each library (`fftw_init_threads` + `fftw_plan_with_nthreads` + `fftw_malloc`,
`FT_Init_FreeType`, `hb_buffer_create`, `cairo_image_surface_create` + a real
paint). It additionally takes the **address** of
`cairo_ft_font_face_create_for_ft_face` and `hb_ft_font_create_referenced`, so
the linker must resolve the two entry points the whole no-fontconfig design
rests on.

It is built as a **shared object**, twice — at API 30 (the sysroot's own level /
minSdk) and at API 35 (targetSdk) — because a static archive has no LOAD
segments and the alignment is only observable on a linked image.

```
==> linking smoke TU at API 30
==> ok: ~/.local/android/work/smoke/libwtsmoke-api30.so
  Type:                              DYN (Shared object file)
  Machine:                           AArch64
  LOAD alignment: align 2**14
==> 16 KB page alignment confirmed at API 30
==> linking smoke TU at API 35
==> ok: ~/.local/android/work/smoke/libwtsmoke-api35.so
  Type:                              DYN (Shared object file)
  Machine:                           AArch64
  LOAD alignment: align 2**14
==> 16 KB page alignment confirmed at API 35
==> smoke compile+link clean at API 30 and API 35
```

Both link with **no undefined symbols**, `-std=c++23`, `-ffp-contract=off`,
`-static-libstdc++`. `DT_NEEDED` is exactly `libdl.so`, `libm.so`, `libc.so` —
no `libc++_shared.so`, no `libharfbuzz.so`, nothing that would have to be shipped
beside the app.

### The 16 KB alignment check (the research doc's command, verbatim output)

```
$ llvm-objdump -p libwtsmoke-api30.so | grep -A2 LOAD
    LOAD off 0x0000000000000000 vaddr 0x0000000000000000 paddr 0x0000000000000000 align 2**14
         filesz 0x00000000000d85c0 memsz 0x00000000000d85c0 flags r--
    LOAD off 0x00000000000d85c0 vaddr 0x00000000000dc5c0 paddr 0x00000000000dc5c0 align 2**14
         filesz 0x00000000003226a0 memsz 0x00000000003226a0 flags r-x
    LOAD off 0x00000000003fac60 vaddr 0x0000000000402c60 paddr 0x0000000000402c60 align 2**14
         filesz 0x0000000000019488 memsz 0x000000000001a3a0 flags rw-
    LOAD off 0x00000000004140e8 vaddr 0x00000000004200e8 paddr 0x00000000004200e8 align 2**14
         filesz 0x0000000000000790 memsz 0x0000000000002310 flags rw-
```

`align 2**14` = 16384 on **every** LOAD segment, at both API levels, with no
`-Wl,-z,max-page-size` flag passed anywhere — this is r28+'s default doing its
job, confirmed on a real link that pulls in all five static libraries. The
`zipalign -c -P 16 -v 4` half of the check belongs to M2 (there is no APK yet).

## 6. Source pins and their provenance

| Tarball | SHA-256 | Fetched from | Independently cross-checked against |
|---|---|---|---|
| `fftw-3.3.11.tar.gz` | `5630c24c…39a1` | fftw.org | Arch `fftw` PKGBUILD sha512 ✔ |
| `freetype-2.14.3.tar.xz` | `36bc4f1c…5a5f` | download.savannah.gnu.org | Arch `freetype2` PKGBUILD b2sum ✔ |
| `harfbuzz-14.3.1.tar.xz` | `9dae9538…94f7` | github.com/harfbuzz/harfbuzz releases | **none available** (see below) |
| `pixman-0.46.4.tar.gz` | `d09c44eb…591c` | deb.debian.org pool | Debian `pixman_0.46.4-1.dsc` sha256 ✔ |
| `cairo-1.18.4.tar.xz` | `445ed820…2ccb` | deb.debian.org pool | Debian `cairo_1.18.4-3.dsc` sha256 ✔ |

**DEVIATION — cairo and pixman do not come from cairographics.org.** That host
is unreachable from this machine (DNS resolves to 131.252.210.176, every HTTPS
connect times out, IPv4 forced, retried), and it is also where the freedesktop
GitLab release pages point their "Release archive" links. The Debian pool copies
are the pristine upstream `.orig` tarballs and Debian publishes their SHA-256 in
the `.dsc`, so the pin is verified against a publisher independent of the
download. If cairographics.org comes back, the URLs in `common.sh` can be
switched with the same checksums.

**harfbuzz's pin is self-recorded.** Upstream ships no checksum file with the
release, Debian is still on 12.3.2, and Arch builds harfbuzz from git (its b2sum
is of a different artifact, so it does not cross-check). The tarball came over
HTTPS from the project's own GitHub release; the SHA-256 above is what was
downloaded and is what every later run verifies against.

## 7. What in the research doc's §1/§2 proved wrong, or needed changing

Mostly it held up unusually well. The five recipes needed **no source patches**
and the two "critical questions" (cairo-ft without fontconfig; pixman
cpu-features on arm64) were both answered correctly. What did not match:

1. **`ndk;30.*` in the SDK manifest is labelled stable but is r30-beta1/beta2.**
   §1.1's sdkmanager route would happily install it. Read the archive filename,
   not the channel ref.
2. **cairographics.org is unreachable** (§2.1 assumes the upstream tarballs).
   See §6 above for the substitution.
3. **§2.1's shared-environment block sets `API=24`.** Kept the mechanism,
   changed the value to 30 — with reasons, in §3 above.
4. **§2.1(e)'s warning that `-Dpng=disabled` is "the one flag with a nonzero
   chance of friction" did not materialise.** cairo 1.18.4 configures and builds
   clean with `png` and `zlib` both disabled; `CAIRO_HAS_IMAGE_SURFACE` is set
   (it is gated on pixman alone, as the doc says), and the build simply skips
   `boilerplate/` and `test/`. No libpng was needed.
5. **§2.1(c)'s harfbuzz option list is complete but only for hb ≥ 12.** Checked
   `meson_options.txt` in 14.3.1 before passing it — `graphite`, `fontations`,
   `harfrust`, `kbts`, `raster`, `vector`, `gpu`, `gpu_demo`, `subset`,
   `utilities` all exist there. (Also worth knowing: harfbuzz still uses
   `meson_options.txt`, while cairo uses `meson.options` — the doc is right
   about cairo but the two are not the same.)
6. **§1.1's "AUR route" was not taken at all** and should not be: every piece
   downloads and unpacks into `~/.local` with no root, no multilib, and no
   system-wide state. The `lib32-*`/multilib caveat never arises.
7. Small correction to the doc's verification recipe: `llvm-objdump -p x.so |
   grep LOAD` shows the `align` field on the LOAD line itself, but the
   `filesz/memsz/flags` continuation is on the next line — use `grep -A1`/`-A2`
   if you want to read them together. And a plain `| grep -q` inside a
   `set -o pipefail` script turns objdump's SIGPIPE into a false build failure
   (this bit once, in `wt_check_lib`; fixed there).

Nothing in §1 was exercised beyond the toolchain installs — the manifest,
aapt2/zipalign/apksigner pipeline, keystore and `adb` loop are all M2.

## 8. Loose ends for M2 (not done here, by scope)

- `zip` and `android-tools` (`adb`) are not installed; both are system packages.
- No debug keystore was created (`~/.android/debug.keystore` does not exist).
  `keytool` is now available at `~/.local/android/jdk/bin/keytool`.
- **`android/prebuilt/` is 11 MB of build output sitting in the repo.** It is
  there because the brief names it a deliverable, but whether it should be
  committed or `.gitignore`d is an architect call — the whole tree is
  reproducible from `android/deps/build_all.sh` in about ten minutes. I did not
  touch `.gitignore`.
