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

**Not 36** (given): Android 16 silently revokes `screenOrientation`
and `resizableActivity` on displays ≥ 600dp, which the Tab S10 FE is. `android.jar`
is therefore the android-35 one, and the DECLARED TARGET was 35 with it until
2026-08-27.

> **targetSdk is 34 since 2026-08-27** (§12's note): 35 turned out to carry
> Android 15's edge-to-edge enforcement, which lays the window over the system
> bars whatever the app asks for, and the opt-out at 35 is a theme attribute
> needing a `res/` this APK has never had. 36 is still out for the reason above.
> The `android.jar` is STILL the android-35 one — the compile platform and the
> declared target are two numbers now (`WT_PLATFORM_SDK` / `WT_TARGET_SDK`,
> `android/toolchain/00_env.sh`), because the runtime gates behaviour on the
> stamped target and never on the jar.

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

It is built as a **shared object**, twice — the script loops `$WT_API`
`$WT_TARGET_SDK`, today **30** (the sysroot's own level / minSdk) and **34**
(the declared target) — because a static archive has no LOAD segments and the
alignment is only observable on a linked image. Those are two of the THREE
numbers this build carries: the third is the COMPILE platform `$WT_PLATFORM_SDK`
= 35, the only `android.jar` installed, which the smoke link never touches.

The captured run below PREDATES the 35 → 34 target step (§12), so its second
level reads 35; it is kept verbatim as the record of that run.

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

---

## 9. The M2 spike APK (2026-08-23)

A throwaway APK that proves the whole Android substrate on the real device before
any product code is ported. **No product source is involved** — nothing under
`src/`, `tools/`, `docs/`, `projects/` or the root `CMakeLists.txt` was read into
it or edited. Everything lives under `android/spike/`.

`bash android/spike/build_apk.sh` goes from a clean tree to a signed, aligned,
verified APK in one command. It wipes `android/spike/build/` first, so a re-run
means exactly what a first run means.

### 9.1 What it puts on one screen

1. **A cairo test card** — eight labelled colour swatches (RED/GREEN/BLUE/CYAN/
   MAGENTA/YELLOW/WHITE/GREY, each captioned with the colour it is supposed to
   be, so an R/B swap is legible rather than inferred), a comb of 1-px rules at
   gaps 1…6 px plus eight 1-px horizontals (any scaling between backbuffer and
   panel shows up here long before it shows up in text), and two harfbuzz-shaped
   lines at a fixed 48 px — one Liberation Sans, one Liberation Mono.
2. **Live touch echo** — a ring + dot per active finger labelled with its POINTER
   ID and coordinates, tracked by id (never by index).
3. **PLAY WAV / STOP WAV** — a tap region running the bundled 3 s 44.1 kHz stereo
   WAV through AAudio, with the granted stream facts printed live.
4. **OTG PROBE** — a tap region running the storage probe (§9.5) and printing
   every result, errno by name.
5. **A frame counter and fps**, plus the negotiated window format, stride and
   which copy path the blit took.

### 9.2 File inventory

```
android/spike/
  AndroidManifest.xml     NativeActivity, landscape, hasCode="false",
                          extractNativeLibs="false", MANAGE_EXTERNAL_STORAGE
  build_apk.sh            clean -> keystore -> assets -> compile -> link ->
                          aapt2 -> zip -0 -> zipalign -P 16 -> apksigner -> verify
  src/spike_main.cpp      glue main, lifecycle, ALooper loop, blit+swizzle,
                          the test card, touch echo, the two tap regions
  src/spike_text.{h,cpp}  freetype on APK-asset bytes + harfbuzz + cairo-ft
  src/spike_audio.{h,cpp} AAudio open/callback/error path
  src/spike_wav.h         the spike's OWN minimal WAV reader (16/24-bit stereo)
  src/spike_storage.{h,cpp} the OTG write probe + the isExternalStorageManager read
  src/spike_log.h         logcat macros
  tools/gen_assets.sh     copies the two Liberation faces, generates the WAV
  tools/gen_wav.py        deterministic 44.1 kHz stereo 16-bit WAV writer
  build/                  ALL output, incl. assets/ (gitignored by `build*/`)
```

The assets land under `build/assets/` deliberately: `.gitignore` already ignores
`build*/`, so the generated WAV and the copied third-party font binaries stay out
of the repo with no `.gitignore` edit, on the same reasoning that keeps
`android/prebuilt/` out.

### 9.3 The keystore

`~/.android/debug.keystore`, created by `build_apk.sh` on first run with the
private JDK's `keytool` (`~/.local/android/jdk/bin/keytool`): RSA 4096, 20000
days, the AOSP `android` / `androiddebugkey` / `android` triple, DN
`CN=Android Debug, O=Android, C=US`. **Back it up.** Android identifies an app by
(package name, signing certificate); losing this key means uninstall-and-lose-data
rather than upgrade-in-place. `WT_KEYSTORE` overrides the path.

The spike's package is `com.warptempo.spike`, deliberately NOT the product's
`com.warptempo.gui`, so the throwaway can be installed and removed without
disturbing anything the product later owns.

### 9.4 Verification (all device-free, all inside `build_apk.sh`)

- `apksigner verify` passes. It reports **v3 true, v2 false** — that is the
  default verify running at the APK's own minSdk 30, where only v3 is exercised.
  The v2 block IS present; the script re-asks at `--min-sdk-version 24` to show
  it. v1 is absent and correctly so at minSdk 30.
- `zipalign -c -P 16 -v 4` → `lib/arm64-v8a/libwarptempo_spike.so (OK)` /
  `Verification successful`.
- Every LOAD segment `align 2**14`, on the linked `.so`, with no
  `-Wl,-z,max-page-size` flag anywhere — r29's default, confirmed on an image that
  pulls in cairo, pixman, harfbuzz and freetype statically.
- `-Wl,--no-undefined` is passed at link, so a missing symbol is a BUILD failure
  rather than something deferred to the device loader. `DT_NEEDED` comes out as
  exactly `libdl libm libaaudio libandroid liblog libc` — nothing that would have
  to ship beside the app, no `libc++_shared.so` (`-static-libstdc++`).
- `aapt2 dump badging` / `dump xmltree` confirm: `minSdkVersion 30`,
  `targetSdkVersion 35`, `android.app.NativeActivity` launchable,
  `android.app.lib_name = warptempo_spike`, `screenOrientation=0` (landscape),
  `hasCode=false`, `extractNativeLibs=false`, `resizeableActivity=false`,
  `uses-permission MANAGE_EXTERNAL_STORAGE`, `usb.host` not required,
  `native-code: arm64-v8a`.
- The APK's own listing shows the `.so` and all three assets **STORED**
  (`-0 ttf -0 wav`, `zip -0` for the library), which is what `-P 16` and
  `extractNativeLibs="false"` both require.

### 9.5 The choices the spike made, and why

**AAudio: opened UNSPECIFIED, resampled in the app.** The brief allowed either
requesting 44.1 k or letting the OS resample. The spike does neither: it opens
with `AAUDIO_UNSPECIFIED` sample rate, reads back what was granted, and linearly
resamples 44.1 k into that rate itself. Requesting 44.1 k teaches nothing and
costs the documented 8× round-trip latency; letting the OS resample hides the one
number M4 actually needs, which is **what rate the device grants**. The screen
prints granted rate, channels, format, burst, buffer size, deviceId, sharing mode,
performance mode, `getHardwareSampleRate()` and the live SRC ratio. `SHARED` +
`PERFORMANCE_MODE_NONE`, never `EXCLUSIVE`/`LOW_LATENCY` (refused on USB). The
error callback ignores the code entirely and tears down from a **detached** thread,
per Google's own recipe and the TIMEOUT-instead-of-DISCONNECTED bug. The linear
interpolator is a placeholder for the M4 shim, not a draft of it.

**Blit: persistent ARGB32 backbuffer + a per-frame swizzle chosen off the format
actually negotiated.** `setBuffersGeometry(win, 0, 0, WINDOW_FORMAT_RGBA_8888)`,
then `buf.format` is re-read at every lock: RGBA/RGBX take a NEON `vld4q_u8` R↔B
swap, a BGRA window would take a plain `memcpy`, and the screen says which
happened. `buf.stride` is treated as **pixels** (the header says so explicitly).
The spike repaints everything every frame and passes a NULL dirty rect; the damage
union with the rect `lock` hands back is the product port's job, not the spike's.

**Text: `CAIRO_ANTIALIAS_GRAY` is pinned.** Subpixel AA is channel-ASYMMETRIC, so
it would fringe through the R↔B swizzle. Also **two `FT_Face` objects per font
from one buffer** — cairo sets the face size when it renders and harfbuzz reads
the face size when it shapes; two faces over the same immutable bytes removes the
whole class of fight for nothing. The `AAsset`s stay open for the process's life
because `FT_New_Memory_Face` does not copy.

**Run loop: research §3.4 Model 1, no `AChoreographer`.** The first
`ALooper_pollOnce` of an iteration blocks for one tick period (5 ms since
2026-08-27, 8 ms when this was written) and the rest drain with a 0 timeout,
so a busy event source can never starve the repaint; `unlockAndPost`'s blocking on
buffer availability is the real pacer. `ANativeActivity_setWindowFlags(…
AWINDOW_FLAG_KEEP_SCREEN_ON, 0)` is the whole kiosk stay-awake implementation.

**`android_main` state is a LOCAL, not a global.** `android_main` runs again when
the activity is destroyed and remade (USB attach is a documented trigger), so a
second entry gets a fresh state and the first one's cairo surfaces, FT faces and
AAudio stream are already released. The window is **borrowed and never
`ANativeWindow_acquire`d**, and is dropped at `APP_CMD_TERM_WINDOW`.

**The OTG probe, and NO JAVA SLIVER.** The probe reads
`Environment.isExternalStorageManager()`, writes a control file into
`activity->internalDataPath` (if that fails the probe itself is broken),
`opendir("/storage")` and lists what is visible, then for every entry whose name
is an FS-UUID (`XXXX-XXXX`) does `open() + write() + fsync() + close()` of
`warptempo_spike_probe.txt` and reports the errno of whichever step failed first,
by name, on screen. **`hasCode="false"` survives:**
`Environment.isExternalStorageManager` is a *static* method on a *framework* class
on the boot classpath, so plain JNI (`AttachCurrentThread` → `FindClass` →
`GetStaticMethodID`) reaches it from a zero-Java APK. The Java sliver the research
doc plans for is owed to `onActivityResult` (SAF, M6), immersive mode and the
clipboard — none of which the spike touches. **So no `javac`, no `d8`, no
`classes.dex`.** The `MANAGE_EXTERNAL_STORAGE` toggle is granted by hand in
Settings; the spike only reports the live state.

### 9.6 What §1/§3/§4 of the research doc got wrong in practice

1. **`-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__` is mandatory and is mentioned
   nowhere.** Without it bionic marks every symbol newer than minSdk `strict`, and
   a strict-unavailable symbol is a hard **build error** that
   `__builtin_available(android N, *)` **cannot** open — the guard is silently
   useless. This bit on `AAudioStream_getHardwareSampleRate` (API 34) from a
   minSdk-30 build. `android/spike/build_apk.sh` passes the define; any later
   milestone that touches a newer-than-minSdk API needs it too.
2. **§4.1's `AAudioStream_getDeviceIds` (claimed API 36) DOES NOT EXIST in NDK
   r29's `AAudio.h`, and neither does `AAudioStreamBuilder_setDeviceIds`.** The
   §4 epistemic note was right that the summariser fabricated part of that table
   (`setPartialDataCallback` is indeed absent) but the *corrected* table still
   carries a row that the headers do not support. What r29 actually introduces at
   API 36 is the offload family: `setPresentationEndCallback`,
   `setOffloadDelayPadding`, `getOffloadDelay`, `getOffloadPadding`. Everything
   else §4 claims **is** confirmed verbatim: `setDeviceId` 26, `setSampleRate` 26,
   `setUsage` 28, `setContentType` 28, `getHardwareSampleRate` 34, and there is
   still no native enumeration API at any level.
3. **§3.7's "patch native_app_glue, its `process_input` causes ANRs" is stale.**
   The bug is already fixed upstream in r29 — the shipped `process_input` drains
   with `while (AInputQueue_getEvent(...) >= 0)` and `continue`. There is nothing
   to patch and no fork to maintain; the spike compiles the stock glue in place.
4. **§1's `zip` availability.** NOTES §8 recorded `zip` as missing and left it as
   a pacman call for M2; `/usr/bin/zip` (Info-ZIP 3.0) is in fact present, so no
   system package was needed and none was installed.
5. **`apksigner verify`'s scheme report is min-sdk-relative** (§1.7 does not say
   so). At minSdk 30 it prints v2 = false even when the v2 block is present.
6. Confirmed and unchanged: `stride` is in PIXELS (`native_window.h` says it in
   so many words); cairo ARGB32 is B,G,R,A on this target while
   `WINDOW_FORMAT_RGBA_8888` is R,G,B,A; `zipalign` must run **before**
   `apksigner`; `aapt2 link` with no `res/` at all still emits a valid binary
   manifest plus `resources.arsc`; `-0` is required for `-P 16` to mean anything.

### 9.7 Known limits of the spike (deliberate, not defects)

- **The navigation bar will be visible.** `Theme.NoTitleBar.Fullscreen` hides the
  status bar only, and hiding the nav bar is Java-only (§3.6). Seeing it is itself
  a useful data point for the immersive-mode decision.
- Touch **history samples** are not replayed — the echo reads the current position
  only. The product's sweep and grab-pan will need `AMotionEvent_getHistorical*`;
  the spike does not, and pretending otherwise would hide the cost.
- The OTG probe runs **synchronously on the glue thread**. A pathologically slow
  volume could stall the loop; at spike scale that is a feature (the stall is
  visible) rather than a risk worth engineering away.
- Every tap region acts **at the press**. A spike tap can only mean one thing, so
  there is nothing to defer to a lift.
- The APK is not byte-reproducible run to run (zip and signature timestamps); the
  *inputs* are — the WAV generator is deterministic and the fonts are copies.

---

## 10. The product APK — the real GUI on the tablet (M3b-2, 2026-08-26)

The M3 seam's Android half: `src/gui/platform_android.{h,cpp}` (the backend),
`src/gui/playback_stub.cpp` (silent; §11 replaced it), the root `CMakeLists.txt`'s
`if(ANDROID)` branch, and `android/app/` (configure + package + manifest).
Unlike §9 this **is** product code — the same `src/gui`, `src/parser`,
`src/engine`, `src/audio_io` and `src/prepost` the desktop binary compiles,
minus three files and plus three.

### 10.1 The commands

```bash
bash android/app/build_apk.sh                 # clean-to-signed-APK, one command
adb install -r android/app/build-android/warptempo.apk
adb shell am start -n com.warptempo.gui/.MainActivity
adb logcat -s warptempo:*                     # the GUI's own stderr
```

`build_apk.sh` calls `configure.sh` itself, so the two-step form is only for
incremental work:

```bash
bash android/app/configure.sh
cmake --build android/app/build-android -j$(nproc)
```

`rm -rf android/app/build-android` is the clean-build gesture; the packaging
tree under it is wiped on every run, the CMake tree is not.

Installing a project is a push into the app's own external files dir, which
needs **no permission at all**. Since 2026-08-28 the app no longer reads a
`current` file at all: it opens the project model's own `last_project` (a
device config key, `src/gui/device_config.h` — the config lives in the app's
internal files dir, readable with `adb shell run-as com.warptempo.gui cat
files/warptempo_gui/config`), or, when that name is empty or its folder is
gone or invalid, the first valid project folder under `<externalDataPath>/
projects` in name order. Opening a DIFFERENT project is done from inside the
running app — File → Open, the on-screen keyboard's symbol page carrying the
`Tab` key the prompt's autocomplete needs (docs/HELP.md's Opening another
project). The layout under `<externalDataPath>` =
`/sdcard/Android/data/com.warptempo.gui/files` is the laptop's `projects/`
mirrored:

```
projects/<name>/         the source .wav, its three sidecars sharing that
                         stem, render/ (the deliverable) and tmp/ (the
                         disposable batch cells) beside them
```

`resolve_project` (`src/gui/project_model.h`) names a folder's source by its
sidecar stem, or, for a folder with no sidecar at all, the one `.wav` it
holds — the whole rule is in `src/gui/project_model.h`'s head comment, shared
verbatim with the laptop. There is no longer a `current`-shaped grammar to
refuse on this device: at STARTUP an invalid folder is discarded silently
whether or not `last_project` named it — `startup_source` tries the remembered
name first, drops a failed resolve without a word, and walks on to the first
valid project in name order, exiting with `No project under <projects path>`
only when the walk finds none. A folder's own refusal reason reaches the
status line through File → Open alone, where the typed name is the one
candidate and there is nothing to fall through to. Either way a filesystem
refusal — including the mode-770 one below — carries the system's own words.

The producer is `~/.pc/bash/wts` (personal tooling, outside the repo): `wts tp`
from a project folder pushes it, `wts fp` brings the sidecars and renders home
and commits them. **`wts` still writes a `current` file on every push, and
nothing in the app reads it any more** — as of 2026-08-28 the script still
emits it; it is personal tooling outside the repo and is updated on its own
schedule, and the file sits harmlessly in `<externalDataPath>` until then.
Placing one by hand is the same four pushes plus the folder name:

```bash
FAR=/sdcard/Android/data/com.warptempo.gui/files
adb push "<src>.wav"              "$FAR/projects/<name>/"
adb push "<src>.warpmarkers"      "$FAR/projects/<name>/"
adb push "<src>.phaseresetmarkers" "$FAR/projects/<name>/"
adb push "<src>.settings"         "$FAR/projects/<name>/"
adb shell "find '$FAR/projects' -type d -exec chmod 777 {} +"
```

**The chmod is load-bearing.** `files/` belongs to the app, so a file pushed
straight into it is readable — which is why the old flat convention never met
this. But a DIRECTORY that `adb push` creates below it belongs to `shell` with
mode 770, and the app's uid is neither its owner nor in its group: `opendir()`
answers EACCES. Before 2026-08-28 the app read the `current` file and that
EACCES made it die at launch saying `current` names no folder (measured on the
device 2026-08-27); since the project model landed the same permission failure
surfaces as `resolve_project`'s own "Permission denied" on whichever folder
it hit. `chmod` does take on this device's external storage, so one pass over
the directories is the whole fix; the files under them are already
world-readable. `wts tp` runs it after every push.

**THE SIDECAR TRAVELS VERBATIM since 2026-08-27.** `gui_scale` left the
`.settings` for the per-device config that day (`$XDG_CONFIG_HOME/warptempo_gui/
config`, which `android_main` points at the app's internal dir), so the pushed
copy carries no opinion about the tablet's screen and `wts tp` no longer rewrites
it on the way over — nor `wts fp` on the way back. The tablet's scale is written
once, by the app itself, on its first launch after an install onto a clean
internal dir: `gui_scale=225`, from the backend's own first-run template. Editing
it later is `:gui_scale=` in the settings prompt on the device, which rewrites
that file at the commit.

### 10.2 What the backend is, and what it stubs

The seam's class A only. `GuiPlatform`'s public API is identical to
`platform_wayland.h`'s member for member (proved by diffing the two headers'
declaration lines with comments stripped). *(This paragraph once recorded ONE
addition — `synthesize_key`, unused and reserved for an owned on-screen
keyboard. SUPERSEDED 2026-08-27: both backends now declare `synthesize_key` and
`wants_onscreen_keyboard`, the on-screen keyboard's press router is their one
consumer, and the seam has no addition at all — §13.3.)*

- **Run loop**: a periodic `timerfd` at 5 ms (8 ms until 2026-08-27; §10.4
  carries the pin that moved it) is the ONE wakeup, added to the
  glue's own `ALooper` alongside the four worker eventfds (idents
  `LOOPER_ID_USER`..`+4`) and the glue's cmd/input sources.
  `ALooper_pollOnce(-1)` blocks, then the pass drains everything pending. The
  dispatch order is the Wayland loop's: window-system sources → `on_tick_` then
  `GuiInputCore::tick()` → the four worker completions in registration order →
  the loop-settled hook → paint-if-dirty. The timer and worker fds are FLAGGED
  during the drain and acted on after it, because the looper hands events back
  in readiness order and the order above is policy, not protocol.
  *(SUPERSEDED 2026-08-28: there are FIVE worker eventfds now — the Synchronize
  to external storage act's joined the four — plus ONE MORE SOURCE, the car's
  media-command eventfd under its own ident after the worker range, drained
  into its own flag and dispatched after the worker completions. The live
  account is platform-seam.md's loop contract and car section.)*
  `drain_events()` — whose live caller is a blocking LOAD's progress callback —
  **is the paint and nothing else**. Its Wayland counterpart is
  `wl_display_dispatch_pending`, which dispatches already-read events and reads
  no socket, so a load observes no new input, no focus change and no close —
  only the pending frame-done callback, which paints. Here there is no such
  split: the looper's window-system sources hold the glue's own `process()`
  bodies, and stepping one would deliver a touch or a lifecycle command into a
  half-built `AppState`. So NO source is processed (neither `LOOPER_ID_MAIN`
  nor `LOOPER_ID_INPUT`), and the tick and the worker completions likewise wait
  for the next `pump()`. **The accepted cost** is the ANR watchdog, which fires
  after ~5 s of an unserviced input event; the product's loads measure ~0.5 s
  for a 101 MB source, and a touch inside a load an order of magnitude longer
  than that is the user interrupting his own load.
- **Present**: one persistent cairo ARGB32 backbuffer, damage coalesced exactly
  as on Wayland, the damaged rects painted through `on_redraw_` and their
  BOUNDING BOX posted through `ANativeWindow_lock` + the spike's NEON R↔B
  swizzle, honoring the buffer's pixel stride and the (possibly widened) dirty
  rect `lock` hands back. The backbuffer is persistent rather than pooled
  precisely so a widened rect is always answerable. **The locked format is a
  hard gate**: exactly `WINDOW_FORMAT_RGBA_8888` and `RGBX_8888` are accepted —
  the two the swizzle is written for — and anything else logs the format and
  `abort()`s, a 16-bit buffer under a 32-bit row copy being memory corruption
  rather than a wrong picture (there is no producer for a conversion arm). The
  `setBuffersGeometry` request's refusal is reported but not fatal there; the
  lock is where the truth is. **The damage lives until the post succeeds**: a
  failed lock or a failed `unlockAndPost` keeps the rectangle, which is the
  only record of what the window has not been shown.
- **Touch**: `AMotionEvent` by pointer id straight into the core —
  DOWN/POINTER_DOWN → `touch_motion` for every ALREADY-LIVE pointer then
  `touch_down`, MOVE → `touch_motion` for every pointer in the event,
  UP/POINTER_UP → `touch_motion` for every pointer INCLUDING the lifting one
  then `touch_up`, CANCEL → `touch_cancel` — with
  `touch_frame()` closing every translated event, one AMotionEvent being one
  logical touch batch. Coordinates are pixel doubles, unscaled — a
  surface pixel is a panel pixel; since 2026-08-27 they are also translated by
  the content rect's origin on the way in (§12's note), so what the core sees is
  CONTENT pixels. **History samples are not replayed**: the core
  coalesces motion to the frame boundary anyway.
- **Keys**: none translated. `AINPUT_EVENT_TYPE_KEY` returns 0 so BACK still
  leaves the app. Hardware keyboards are out of scope (touch.md).
- **Mouse**: consumed, not routed and not returned — a click that fell through
  would act on whatever is behind the activity.
- **Key repeat**: `set_repeat_info(30, kHoldBeatMs)`, hard-coded (architect
  ruling 2026-08-23) — Android advertises no cadence to a native activity, so
  the numbers are labwc's by convention.

Stubs, each with its Wayland twin named at the site: the **clipboard** is one
stored string (ClipboardManager is a Java surface with no NDK door); **pointer
capture** is a pair of no-ops (no cursor to hide, no pointer to lock, no
relative stream — the same degraded path a compositor without the two optional
protocols takes); **`set_cursor_kind`** still runs the core's policy and applies
nothing; **the title setters** store nothing (fullscreen, no titlebar);
**playback** was `playback_stub.cpp`, which answered `init() = true`, parked
the cursor where `play()` was asked to start, reported `is_playing() = false`
and gave the bound buffer's real domain extent (those are RANGE POLICY, not
audio facts, so zeros would silently collapse every range that consults them).
That file is DELETED — §11 replaced it with the real AAudio backend, and the
Android arm of the playback device is now `playback_aaudio.cpp`.

Two things the backend does that are not on the Wayland side at all:

- **stderr and stdout are redirected onto a pipe** whose reader thread turns
  each line into `__android_log_write` under the tag `warptempo`. The fix is at
  the file descriptor because the ~200 diagnostic sites across the GUI, the
  parser and the engine do not know what platform they are on and should not.
  The sink is **process-lifetime**, installed once behind a static guard: a
  second `android_main` (activity destroyed and remade) reuses the pipe and the
  thread already blocked in `read()` rather than stranding the first one's read
  fd. If the thread cannot be created the redirection is **unwound** — the
  original descriptors go back — so no diagnostic ever writes into a pipe with
  no reader.
- **`android_main` normalizes the environment before `gui_main`**:
  `XDG_CACHE_HOME`, `HOME` and `TMPDIR` to the app's private directory (the
  render cache is `XDG_CACHE_HOME`'s tenant), and `setlocale(LC_ALL, "C")` —
  see 10.4.

Font install failure is a **hard abort**, not a fallback: `install_fonts_or_die`
opens both Liberation assets, installs them, and then ASKS whether selecting a
family actually put an FT-backed face on a probe context. A missing asset is a
build defect with no runtime producer, and painting silently in cairo's default
face is worse than not starting. `gui_font_bundled.cpp`'s own error arms are
untouched — the abort is the caller's.

### 10.3 The build branch

`if(WARPTEMPO_BUILD_GUI AND ANDROID)` in the root `CMakeLists.txt`. It
discovers no Wayland, xkb, JACK or wayland-scanner, and finds cairo / cairo-ft
/ harfbuzz / freetype2 through the M1 sysroot's own `.pc` files. **The branch
names no path**: `android/app/configure.sh` passes `PKG_CONFIG_EXECUTABLE` (the
toolchain wrapper, which hard-forces `PKG_CONFIG_LIBDIR`) and
`FFTW3_THREADS_LIB` (the one dependency with no upstream `.pc`, whose
top-level `find_library` cannot search a sysroot the NDK toolchain file has
re-rooted). Target: `add_library(warptempo_gui SHARED …)`, static deps inside
`--start-group` (the freetype↔harfbuzz cycle) plus `aaudio android log m dl`,
`-Wl,--no-undefined`, and `-D__ANDROID_UNAVAILABLE_SYMBOLS_ARE_WEAK__`.

**THE LINUX TARGET IS BYTE-UNAFFECTED, and it was proved rather than asserted.**
The GUI's source list moved into one shared `WARPTEMPO_GUI_SOURCES` variable
both targets draw (the three per-backend arms — platform, face owner, playback
device — are named at each target), so the two builds cannot drift into two
rosters. Re-configuring the desktop build across that change:
`CMakeFiles/warptempo_gui.dir/flags.make` is **byte-identical**, the object set
is the **same 70 objects**, and the link line's library list is identical in
content AND order. Only the ORDER of the object files on the link line changed
(the three arm objects moved to the tail), which cannot affect an executable
link of a complete object set. `cmake --build build -j$(nproc)` is green.

### 10.4 Device facts measured, and what they cost

| Fact | Value |
|---|---|
| Device | SM-X520 (Galaxy Tab S10 FE), Android 16 |
| Window handed to the activity | **2304 x 1440**, landscape, 1:1 with the panel (the SURFACE is always this, at every setting tried; since 2026-08-27 the WINDOW is the content rect the framework reports, which on an AWAKE panel EXCLUDES BOTH BARS — 60 px status bar, 96 px taskbar — LESS the 14 px of air added under the bar that evening, so **2304x1270 at (0,74)** under the architect's Screen zoom (override density 320); §12's note and §12.6) |
| Panel refresh (active mode) | **90 Hz** (60 and 30 also supported), 280 dpi bucket / 248.8 real dpi |
| Backend tick | **5 ms** since 2026-08-27 (was 8 ms; see below) |
| Presented frame cadence during a continuous one-finger pan | **median 11.09 ms = 90.2 fps**, p10 11.06, p90 22.14 (≈1 frame in 10 doubles) |
| WAV load (101 MB, 25.4 M frames) + 14-level peaks pyramid | 479 ms cold, **235–247 ms** with the peaks cache warm |
| Full PGHI target render, 7:36 output | **~8.7 s** |
| Stripped `.so` | 6.7 MB; APK 7.4 MB |
| DT_NEEDED | `libdl libm libaaudio libandroid libnativewindow liblog libc` — nothing to ship beside the app, no `libc++_shared` (`libnativewindow` since the 90 Hz pin below: `ANativeWindow_setFrameRate` lives there, not in libandroid) |

**The tick is 5 ms and the panel is pinned to 90 Hz (architect 2026-08-27),
which closes the mismatch this section used to record.** The Wayland rule is
"half the refresh period", which at 90 Hz is 5 ms; the backend now takes that
number outright instead of the 60 Hz fallback's 8, and `adopt_window` asks the
window for 90 Hz outright:

```c
ANativeWindow_setFrameRate(window, 90.0f,
                           ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
```

The two halves are one decision — the loop's cadence is the pinned panel's
half-period — which is what makes a hard-coded number honest here: **there is
one device and it is pinned.** `FIXED_SOURCE` is the compatibility that asks
the display to run AT the rate rather than at a multiple it finds convenient;
the panel is variable-refresh, and a program that paints only on damage
otherwise reads to the system as "hardly ever" and lands on 60. The call's
return is LOGGED and nothing else: it is a request, the system may refuse it
(battery saver, thermal cap, another window), and a refusal costs frames rather
than correctness — so there is no error arm and no fallback, exactly as the
buffers-geometry refusal beside it has none. `ANativeWindow_setFrameRate` landed
in API 30, which is minSdk, so no `__builtin_available` guard is owed — but it
lives in **libnativewindow**, not in libandroid, so the root `CMakeLists.txt`'s
Android link list grew `nativewindow` beside `android` (most of the
`ANativeWindow_*` surface this backend uses is re-exported by libandroid, which
is why the line was not needed until now, and `--no-undefined` made the absence
a build failure rather than a launch-time surprise).

**Observed:** `ANativeWindow_setFrameRate(90, FIXED_SOURCE) -> 0` in the log on
every window adoption (three per launch: init's, plus the two the glue's
INIT_WINDOW / CONFIG_CHANGED pair produce), and `window 2304x1440, tick 5 ms`.
The panel-side effect — whether SurfaceFlinger actually holds the mode at 90 —
is NOT verified: the tablet's book cover was shut for this session's whole
drive, which dozes the display, so `dumpsys SurfaceFlinger` reports the doze
mode and no screenshot is obtainable. The measured pan cadence in the table
above (median 11.09 ms) was already 90 fps before the pin, so the pin's value is
in what it prevents rather than in a number this session could re-measure.

**`C.UTF-8` vs `C`.** bionic starts a process in `C.UTF-8`, so
`verify_c_numeric_locale` (locale_check.h) — a pure tripwire on Linux, where a
process starts in `C` — refused to run at all. This was the first thing the port
hit on the device. The two locales are the same locale numerically (bionic
supports only C, POSIX and C.UTF-8), so `android_main` calls
`setlocale(LC_ALL, "C")` before `gui_main`: the platform's way of SPELLING the
invariant the desktop gets for free, not a relaxation of it. The tripwire's real
job is intact — a library that moves the locale after that line is still caught.
**locale_check.h's comment still says "Nothing in this program calls
setlocale", which is now stale by one caller.** It was left alone deliberately
(out of this brief's edit ceiling) and is owed a one-line amendment.

### 10.5 What was driven on the device, and what worked

All through `adb shell input touchscreen …`, screencapped before and after and
compared pixel-for-pixel.

- **The seven-lane top strip, the waveform and the bottom row all paint**, at
  `gui_scale=175`, correctly coloured — the R↔B swizzle is right (the waveform
  is kdenlive's teal, not orange), the marker flags are the right purple, the
  lane text is black on the flags and #fcfcfc elsewhere, the Breeze icon row
  renders whole, and the monospace clock is monospace. The overview strip, its
  viewport box and its playhead tick are all live.
- **A chrome button acts at the LIFT** — the row-1 view selector switched
  T+P → S+W.
- **A plain tap on the waveform's UPPER half places the playhead** (clock and
  playhead line both moved, the selection dropped).
- **A plain tap on a marker flag selects it and lands the playhead** at the
  press — the flag brightened, the clock moved to the marker, and the tab row's
  resolved readout appeared.
- **A one-finger drag on the waveform pans** (the phone model): 800 px of drag
  moved the viewport ~8 s and the overview box with it; the playhead and the
  selection stayed put, the camera not being a movement.
- **A 700 ms motionless hold converts to the region gesture** and its begin
  seats the playhead — which also proves the disambiguation deadline
  (`kTouchRegionHoldMs`) is being polled off the timerfd through
  `GuiInputCore::tick()`.
- **The overview strip's outside-the-box press teleports at the press.**
- **BACK leaves the activity cleanly** (no crash, no ANR) and a relaunch runs
  `android_main` a SECOND time in the same process and comes up identical —
  the destroyed-and-remade path exercised for real.
- **The target-view preview render ran the full PGHI engine on the tablet**
  unprompted at startup (the pushed `.settings` carries `active_audio_view=T`),
  all three passes, `[success]`.

Not verified, and why: **the two-finger pinch zoom could not be injected.**
`adb shell input` has no multi-touch form, and raw `sendevent` on
`/dev/input/event10` is `Permission denied` on this unrooted device. The Nav
phase itself IS proved by the single-finger pan, which runs the same
`deliver_touch_nav_frame` with the pinch arm structurally false; only the ratio
arm is unexercised. It belongs to the architect's own glass pass (M3c) anyway.
Also unexercised for lack of a producer: audio (M4), keys (M5), git (there is no
`git` on the device, so the history prefetch reports unavailable on its worker
and nothing else notices).

### 10.6 Deliberate limits, not defects

- **The system taskbar overlaps the bottom row's right half.** The activity is
  fullscreen under `Theme.NoTitleBar.Fullscreen`, which hides the status bar
  only; hiding the navigation/taskbar is Java-only (immersive mode), which is
  out of M3's scope and on the same list as SAF and the clipboard.
- **The `.settings` of a pre-2026-08-26 project is load-fatal** — it carries no
  `waveform_magnification_level=` line and every key is required. The pushed
  copy for this pass had the line hand-added at its `kSettingsOrder` position
  alongside `gui_scale=175`; `projects/` was not touched. This is the standing
  no-migration convention, not an Android problem.
- **`libaaudio.so` is in `DT_NEEDED` and nothing calls it yet.** It is linked
  ahead of M4 deliberately; `--as-needed` is not passed, so the reference
  stands rather than silently disappearing and reappearing between milestones.
- **A backgrounded activity keeps ticking.** The timerfd is the loop's one
  wakeup and it does not stop at `APP_CMD_TERM_WINDOW`, so a task-switched-away
  app wakes 125 times a second to paint nothing (`paint_one_frame` no-ops with
  no window) until Android's own cached-app freezer stops the process. Left as
  it is for M3 — suspending and re-arming the timer on the window edges is a
  behaviour change to the ONE wakeup and wants the architect's word.
- **The NDK's stock `android_native_app_glue.c` is compiled in place** (r29's
  `process_input` already drains with a while loop, so there is no fork to
  maintain) with `-Wno-unused-parameter` scoped to that one file — it is
  third-party source this project does not own and must not edit, and the
  product's own warnings should be the only ones in the log.


## 11. Sound — the AAudio playback backend (M4, 2026-08-26)

The stub is gone. `GuiPlayback` now has two real implementations over one
engine, and the tablet plays.

### 11.1 The shape: one engine, two devices

`playback.cpp` was one class holding two layers, exactly as `GuiPlatform` was
before §10 split it. M4 split it the same way:

- **`src/gui/playback_common.{h,cpp}` (NEW, portable)** — `GuiPlaybackState`
  (the borrowed buffer, the domain offset, the range, the cursor, the predictor
  anchor, the speed word, the `playing` flag, the audio-thread fractional
  cursor, the pending-start handoff) plus the AUDIO-THREAD RENDER BODY and
  every main-thread helper the public methods were: bind-and-validate, publish-
  play, resync, set-speed, cursor, cursor-precise, the domain accessors,
  rebind. This is the engine, and it is the same object code on both platforms.
- **`src/gui/playback.cpp`** keeps the JACK client alone: open/close, the
  process and sample-rate callbacks, `stop()`'s cycle-counting fence.
- **`src/gui/playback_aaudio.cpp` (NEW)** is the second device half: one AAudio
  output stream, its data and error callbacks, `stop()`'s state-machine fence,
  and the disconnect rule.

`playback.h` — THE CONTRACT — declares exactly what it declared before, and so
does every consumer (its head and `stop()` comments were reworded afterwards to
name BOTH devices where they had named JACK alone; no clause changed meaning).
`playback_lifecycle.*`, `ab_audition.*` and the ~30 `playback.` call sites
needed no edit at all: the split is entirely below the public API.

### 11.2 The rate: one multiply, and it was already there

The render body already advanced its fractional source position by
`speed * source_rate / device_rate` per output frame — the JACK graph rate had
been in that increment since the graph was allowed to differ from the source.
Extraction therefore renamed `jack_rate` to `output_rate` and changed no
arithmetic: on JACK it is the graph's rate, on AAudio the rate the stream was
GRANTED. The fractional read the body already performs IS the resampler; no
second one was added, and none is wanted (the architect's standing ruling: the
audition resample is preview-inaudible, nothing vendor-dependent).

`output_rate == 0` is the SUSPENDED device on both backends — silence and a
held position in the render body, a held cursor in `cursor()` — which is
exactly what `playback.h`'s graph-suspension clause already promised, and what
the AAudio error callback now sets.

### 11.3 The numbers the device grants

Logged once per open, and better than §10's spike measured with
`PERFORMANCE_MODE_NONE`:

```
Audio backend: AAudio, output_sample_rate=48000, source_sample_rate=44100, channels=2
AAudio granted: burst=96, buffer=192, perf=LOW_LATENCY, sharing=SHARED, deviceId=2, hwRate=48000
```

**LOW_LATENCY IS GRANTED on the speaker**: burst 96 (2 ms) and a two-burst
buffer of 192 (4 ms), against the spike's 960/1920 (20/40 ms) under
`PERFORMANCE_MODE_NONE`. `hwRate == getSampleRate() == 48000` says the
framework is doing NO resampling under us: the 44100→48000 ratio in the render
body's own increment is the only one in the chain. `dumpsys media.audio_flinger`
shows the stream as `MMAP_PLAYBACK` / `AUDIO_OUTPUT_FLAG_MMAP_NOIRQ`, which is
the low-latency path being real rather than nominal.

Every setter is a REQUEST and the granted value is read back. Only the FORMAT
and the CHANNEL COUNT are refused over (`PCM_FLOAT`, 2) — the render body
writes interleaved stereo floats and has no other shape. The rate is asked for
as `AAUDIO_UNSPECIFIED` deliberately: asking for 44100 on 48 k hardware only
moves the resample into the framework.

### 11.4 The stop fence, per backend

`playback.h` says `stop()` "returns only once the callback has quiesced", with
no deadline. The two devices prove that differently, and each states its choice
at its own site:

- **JACK** counts `process_cycles`. That works because an active JACK client's
  process callback keeps running (silent) forever.
- **AAudio** counts too, since 2026-08-27, and for the JACK reason: THE STREAM
  NOW STAYS STARTED between plays (the click ruling below), so its data callback
  keeps running — silent, reading no sample — and `callback_cycles` keeps
  advancing. Two increments after `playing` is lowered are the proof, exactly as
  on JACK.

  **THE ESCAPE** is what is left of the old state test: a stream that is dead or
  positively terminal (DISCONNECTED / CLOSING / CLOSED, or the error callback's
  `stream_dead` latch) has no callback left to count — the framework retires the
  callback thread before the error callback runs — so the wait ends there, that
  state being a proof of quiescence in its own right. Otherwise there is no
  deadline and no iteration cap. **So a stalled device hangs the main thread
  here, and that is the contract's safe failure mode**: the alternative is a
  rebind, a buffer replacement or a shutdown freeing samples out from under a
  live audio thread, silently.

  UNTIL THAT DAY the fence drove the STREAM STATE MACHINE instead — `requestStop`
  then `waitForStateChange` until STOPPED or a terminal state, no result code
  ever read as quiescence — because a stream stopped between plays calls no
  callback and a counter would never have advanced. That `requestStop` at every
  stop, with the matching `requestStart` at every play, is exactly what made the
  click.

### 11.4a The click at every play (architect 2026-08-27, on glass)

"When I click the lower half of the waveform I hear a little click sound, like a
keyboard's haptic click, behind the waveform." Android's own touch and haptic
sounds were off on the device, so it was ours: STARTING AN AAUDIO STREAM IS
AUDIBLE — the framework brings the output path up and the device's unmute
transient rides out with the first frames — and the backend started the stream
at every `play()` and stopped it at every `stop()`.

THE STREAM IS NOW OPENED ONCE, STARTED ONCE AND NEVER STOPPED BETWEEN PLAYS. It
starts at open (`init`, and the reopen after a disconnect) and is stopped only
where it is about to be CLOSED — shutdown and the dead-stream reopen, both in
`close_stream`, which holds the file's one remaining `requestStop`. Between
plays the callback's `playing` gate writes silence and never reaches the render
body. The cost is a running stream on an already screen-on tablet (a few mW, no
wakelock change), accepted; the benefit is that the one start transient of a
session happens at launch, with no audition under it. A step-shaped click at a
play START — the render body beginning mid-waveform at whatever sample sits
there, with no ramp — is a different thing and is untouched.

### 11.5 The disconnect (UNTESTED ON HARDWARE)

The KA17-unplugged case. The error callback writes three atomics and logs —
NO AAudio call, which is Google's own rule for that thread. It marks the stream
dead, clears `output_rate` (so the cursor holds, the graph-suspension clause)
and clears `playing` (so the run loop's next tick reads a natural end and tears
the scanner down through the product's one stop body). The NEXT `play()` closes
the dead stream on the main thread and opens a new one — at the NEW device's
granted rate, so the rate ratio is recomputed per open and a 44.1 k DAC would
simply make the ratio 1.0 with no other edit.

NOTHING RECOVERS BY ITSELF: no auto-resume, no reconnect timer, no retry
machinery. The user presses play again. That is the wind-down policy's answer
for a rare, loud fault, and it is also why this backend needs no detached
thread where the spike had one — it never closes from a callback thread
because it never closes there at all.

**This path could not be exercised**: one USB-C port, and the cable is the adb
link. It is reasoned, not observed.

### 11.6 What was driven on the device

APK installed over the §10 project (`/sdcard/Android/data/com.warptempo.gui/
files/`, `active_audio_view=T`, `playback_speed=0.7`), the target preview
rendered by the on-device engine at startup as before.

- **The transport row's PLAY button, tapped by coordinate**, starts playback:
  the glyph swaps to STOP and the clock advances (03:48.278 → 03:50.681 across
  a screenshot burst, ~0.7× wall clock as the speed setting asks).
- **`dumpsys media.audio_flinger` shows the live stream**: `MMAP_PLAYBACK`
  thread, `Sample rate: 48000 Hz`, `Standby: no`, `Attributes: content type 2
  usage 1` (MUSIC/MEDIA, the two we asked for), `Track: com.warptempo.gui`.
- **A second tap stops it**: the glyph returns to Play and the clock returns to
  the RESTING cursor, which the audition never moved.
- **THE NATURAL END STOPS PLAYBACK.** Started 2.3 s before the target buffer's
  end, the scanner ran 07:33.823 → 07:35.765 (exactly `domain_end`), then the
  transport fell back to Play and the clock to the resting 07:33.445 with no
  further input. NOTHING LOOPS, on the second platform.
- **A cold start after a 20 s idle works**: the service puts an idle MMAP
  stream into standby and unmaps its memory; the next tap starts it clean.
- **No error line from the product** across any of it.

**NOBODY HEARD IT.** These are screenshots, logs and audio-server state — the
architect listens, on the panel, and that check is his.

### 11.7 Two findings from the drive

- **THE SYSTEM TASKBAR EATS THE WHOLE BOTTOM ROW, not its right half.**
  §10.6 recorded the taskbar as OVERLAPPING the bottom row's right half. It is
  worse: the taskbar's window takes input across the FULL width, so an injected
  tap on the transport buttons at the row's far LEFT — where the taskbar paints
  nothing and the app's own glyphs are plainly visible — never reaches the app.
  Hiding the taskbar is not enough on its own; the navigation bar that replaces
  it captures the same band. Only forcing immersive mode from outside
  (`settings put global policy_control immersive.full=com.warptempo.gui`, set
  and then DELETED again for this pass, with `task_bar` restored to 1) freed
  the row. **The bottom row is the modal surface and the transport**, so this
  is not cosmetic — it makes the immersive-mode Java sliver load-bearing rather
  than nice-to-have, and it is the strongest argument yet on M6's list.
- **`AAudioServiceStreamBase: start_l() the stream is standby, return
  ERROR_STANDBY`** appears once in the SERVICE's log when a long-idle stream is
  restarted. The client library handles it itself (`exitStandby` and retry) and
  playback ran correctly through it, so no code answers it here: adding an
  `AAudioStream_exitStandby` call would be backstop machinery for a fault the
  platform already absorbs, and a start that genuinely failed already closes
  the stream so the next press reopens it — one silent lost press, then sound.

### 11.8 The Linux path is untouched

The extraction was proved rather than asserted: every moved body was extracted
from the pre-M4 `playback.cpp` and diffed statement-for-statement against
`playback_common.cpp` under a declared rename list. The engine bodies — the
render body, the predictor, `cursor`/`cursor_precise`, rebind, speed, resync —
come out EMPTY. The deviations are five, all declared: the two output-layout
reconciliations (the silence memsets become one helper and the sample write
gains a stride, so one body serves JACK's de-interleaved ports and AAudio's
interleaved buffer), the JACK-only fence/port fields staying in the JACK
backend, `play()` becoming a bool so a backend knows whether to start its
device, the `unique_ptr` null guards staying in the public methods, and one
OBSERVABLE diagnostic change: the rejected-channel message dropped the word
"JACK" ("Unsupported channel count for playback"), the refusal being the shared
engine's on both devices rather than any one device's — recorded at the site in
`playback_common.cpp`.

The Linux target's `flags.make` is byte-identical and its object set gains
exactly one entry, `playback_common.cpp.o`. `libaaudio.so` stays the only new
`DT_NEEDED` — it was linked ahead of M4 in §10 and now something calls it.


---

## 12. The Java sliver — immersive mode (2026-08-26)

> **IMMERSIVE MODE WAS RETIRED THE NEXT DAY** (architect 2026-08-27) as a
> reversible experiment — one commit a later `git revert` undoes whole. The
> status bar and the taskbar now SHOW permanently, like any ordinary app: on
> the glass a swipe brought the taskbar's icons up OVER the app with no
> background of their own (§12.5's "the transient bars behave" — Android's
> behaviour, not ours) and it read as a bug, and the waveform is plenty tall
> enough to lose the bars' height. THE STATUS BAR'S COLOUR WAS THEN CLAIMED
> LATER THE SAME DAY (§12.6 below): showing permanently, it reads as this
> window's title bar, so the sliver paints it `kRedesignRowGround` from the
> architect's labwc theme; the taskbar's colors stay the system's. What changed: `hideSystemBars()` and both
> its call sites are gone with the `onWindowFocusChanged` override,
> `setDecorFitsSystemWindows(false)` became `(true)`, and the activity's theme
> is `Theme.NoTitleBar` rather than `.Fullscreen`, and **`targetSdk` stepped 35
> → 34**. That last one is the load-bearing half and it was measured, not
> reasoned: with `setDecorFitsSystemWindows(true)` installed and NO
> `policy_control` override on the device (`task_bar` = 1), the startup log
> still said `window 2304x1440` — Android 15 lays a target-35 window out
> EDGE-TO-EDGE whatever the app asks for, and `NativeActivity` hands the native
> side the WINDOW's own surface, so the taskbar sat over the bottom row exactly
> as §11.7 measured. At 34 fitting the insets is the platform's own default. The
> 35-era opt-out is the `windowOptOutEdgeToEdgeEnforcement` THEME attribute,
> which needs a `res/values` style and an `aapt2 compile` step this APK has
> never had; 36 was never a candidate (Android 16 revokes `screenOrientation` on
> a screen this size). The COMPILE platform stays `android-35` — the only jar
> installed — because the runtime gates on the stamped target, not on the jar:
> `WT_TARGET_SDK` and `WT_PLATFORM_SDK` are two numbers in
> `android/toolchain/00_env.sh` now.
>
> **AND 34 DID NOT SHRINK THE WINDOW EITHER** — the log still said
> `window 2304x1440` — which is where `dumpsys window` settled the design. The
> app window's frame is the whole display BY CONSTRUCTION: `mAttrs fl=1810180`
> = `FLAG_LAYOUT_IN_SCREEN | FLAG_LAYOUT_INSET_DECOR | HARDWARE_ACCELERATED |
> SPLIT_TOUCH | KEEP_SCREEN_ON`, `Frames: frame=[0,0][2304,1440]`. "Fitting the
> system windows" is DecorView PADDING, and a `NativeActivity` never sees it
> because it takes the WINDOW's own surface (`Window#takeSurface`). The inset
> area reaches native code as the CONTENT RECT instead. THE DUMPSYS RECORD OF
> THAT MEASUREMENT: `mAppBounds=(0,0-2304,1356)`, `InsetsSource statusBars
> frame=[0,0][2304,53] visible=true`, `InsetsSource type=navigationBars
> frame=[0,1356][2304,1440] visible=false` (= the taskbar's 84 px band), and a
> `tappableElement` source on that SAME frame with visible=true. So THE
> CONTENT RECT IS THE WINDOW, implemented in the backend: `APP_CMD_CONTENT_-
> RECT_CHANGED` is handled, `adopt_window` re-reads the rect on all four window
> commands, `width()`/`height()` are the rect's, the origin is ADDED at the one
> blit (`present`, which also fills the two bands with `kRedesignContentGround`
> — the TOP band takes `kRedesignRowGround` since §12.6) and SUBTRACTED at the
> one touch decode, and nothing above the seam learns it exists. The startup
> line carries both now:
> `window 2304x1268 at (0,76) of surface 2304x1440, tick 5 ms`.
>
> **WHAT THE RECT ACTUALLY CONTAINS.** The rect is WHATEVER THE FRAMEWORK
> REPORTS — the backend measures no bar and subtracts no inset of its own,
> §12.6's air being the one thing it takes off — and ON AN AWAKE PANEL IT
> EXCLUDES BOTH BARS. Measured with the architect's own Screen zoom (override
> density 320): `window 2304x1268 at (0,76) of surface 2304x1440` = a 60 px
> status bar plus §12.6's 16 px of air above, and a 96 px taskbar below
> (1440 − 76 − 1268 = 96). Nothing has to be subtracted in the backend, and
> §11.7's escalation does not arise.
>
> *History, one line:* the first reading was taken with the panel DOZING (the
> cover shut) and came back 2304x1387 at (0,53), the STATUS BAR ALONE — the
> taskbar reported no inset at that moment (a `tappableElement` and
> `mAppBounds`, with `type=navigationBars ... visible=false`). THE NEXT STEP
> RECORDED FROM THAT READING — a `native` method on `MainActivity` handing the
> backend `WindowInsets.Type.tappableElement()`'s bottom to subtract — IS
> RETIRED: on an awake panel it would subtract the taskbar twice.
>
> **THE SLIVER STAYS**: `setDecorFitsSystemWindows(true)` plus target 34 is what
> makes the framework report an inset content rect at all, and the SAF /
> clipboard / key-repeat needs still join it. Everything below is the record of
> what 2026-08-26 landed; `docs/engineering/architecture/platform-seam.md` and
> the class's own head comment are authoritative for what stands.
>
> ### 12.6 The title strip (architect 2026-08-27, evening, on glass)
>
> Two changes, one picture. **THE AIR**: "the clock and everything else looks
> too close to the bottom — the distance between the top of the screen and the
> battery icon is much greater than between the battery's bottom and our first
> row; we have plenty of waveform, give some to the top." `kStatusBarAirPx` = 16
> DEVICE pixels (the retune knob, and device pixels because it pairs with the
> bar's own density-scaled geometry, not with `gui_scale`) is added to the
> content rect's TOP inset inside `resolve_content_rect`, and only when the
> framework reports a top inset at all — a fullscreen future gets no blank band.
> Origin, size, damage and touch all follow from that one function.
> **THE COLOUR**: the status bar takes the labwc title bar's, PROVENANCE BY
> CHAIN rather than derivation — `~/.config/labwc/themerc-override`'s
> `window.active.title.bg.color: #292c30` IS `kRedesignRowGround`, and its
> `window.active.label.text.color: #fcfcfc` is `kRedesignLabel`, which is why
> the bar's icons stay light (`APPEARANCE_LIGHT_STATUS_BARS` CLEARED — the flag
> means dark icons for a light bar). The system had been painting it ~#212326,
> near the CONTENT ground. `MainActivity` calls `setStatusBarColor(0xFF292C30)`
> and sets `FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS` by hand, because that flag is
> `setStatusBarColor`'s documented precondition and `Theme.NoTitleBar` — the
> legacy theme this activity uses — does not set it the way the Material /
> DeviceDefault themes do. Both are deprecated at API 35 and honoured at the 34
> the manifest targets. THE TASKBAR'S ICONS ARE THE LAUNCHER'S and nothing
> touches them ("the taskbar looks great, it's already the correct color"), but
> THE BAND UNDER THEM IS OURS the moment that flag is set — the window then
> draws BOTH bars' backgrounds, and the band took the inherited
> `navigationBarColor`, measured on the device at (33,35,38): the system
> default, and the same #202326 we would have picked, so nothing visibly
> changed by luck. It is stated on purpose now:
> `setNavigationBarColor(0xFF202326)` = `kRedesignContentGround`, the ground
> the taskbar's icons already sit on (provenance: the palette block in
> `src/gui/render.h`; NOT a labwc colour). `APPEARANCE_LIGHT_NAVIGATION_BARS`
> is left alone, so the icons are exactly as they were, and the setter carries
> the status colour's own deprecation note. Because the bar and the menu row are now the same
> ground, the air between them would read as a darker stripe if it took the
> content ground, so `present` picks the band word per row: TOP band =
> `kRedesignRowGround`, everything else `kRedesignContentGround`. Bar, air and
> menu row read as one title strip — the clock at its top, the menus beneath —
> which is kdenlive's own arrangement.

The APK has Java in it now: **one class**, `com.warptempo.gui.MainActivity`,
`android/app/java/com/warptempo/gui/MainActivity.java`, ~30 lines of body. It is
the port's ONE Java home and **every later Java need joins it as a method, never
as a second class** — the SAF picker's `onActivityResult` (which is the reason a
subclass is structurally required at all: `NativeActivity` never forwards it,
§5.2 of the research doc) and the system clipboard are the two already known.

**No product source changed.** Nothing under `src/` knows this class exists: the
manifest's `android.app.lib_name` still names `warptempo_gui`, `NativeActivity`'s
own `onCreate` still `dlopen`s the library, and `android_main` runs exactly as it
did.

### 12.1 Why it is load-bearing

§11.7's escalation: the system taskbar takes the input of the **whole bottom
row's** band, not merely its right half, and the navigation bar that replaces it
captures the same band. The bottom row is the product's transport **and** its
modal surface, so the app is undrivable on glass without immersive mode — and
hiding the navigation bar is Java-only (`WindowInsetsController`, API 30+; §3.6).
The interim `settings put global policy_control immersive.full=com.warptempo.gui`
was a device-wide setting standing in for this class; it is **deleted** now.

### 12.2 What the class does

`onCreate` (after `super`) and every `onWindowFocusChanged(true)`:

- `getWindow().setDecorFitsSystemWindows(false)` — the native window is the whole
  panel. It compiles with ONE deprecation warning (deprecated in API 35, where
  edge-to-edge is the default for `targetSdk >= 35` anyway); the call is kept
  because it states the intent rather than inheriting it.
- `hide(statusBars() | navigationBars())` +
  `setSystemBarsBehavior(BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE)`.

The re-apply at focus gain is not belt-and-braces: the system restores the bars
whenever the activity loses focus, so `onCreate` alone would hold only until the
first task switch. No deprecated `setSystemUiVisibility` path — minSdk is 30 and
`WindowInsetsController` is API 30.

`getWindow().getInsetsController()` is null-guarded and returns early; there is
no producer for that arm on this platform (the decor view exists by the time
`NativeActivity.onCreate` has returned), and a hidden window that painted no bars
is not worth an abort.

**The `System.loadLibrary` gotcha is NOT needed yet and WILL be**: the first
`native` method declared on this class must add
`static { System.loadLibrary("warptempo_gui"); }`, because `NativeActivity`'s own
`dlopen` does not register the library for name-based JNI resolution and the
method would throw `UnsatisfiedLinkError` (§5.4).

### 12.3 The build steps

`build_apk.sh` step 4, between the cmake build and `aapt2 link`:

```bash
javac -source 11 -target 11 -Xlint:-options -classpath $WT_ANDROID_JAR \
      -d $PKGDIR/classes  $(find android/app/java -name '*.java')
d8 --release --lib $WT_ANDROID_JAR --min-api 30 --output $PKGDIR/dex  <classes>
zip -u -j -X -q $PKGDIR/unaligned.apk $PKGDIR/dex/classes.dex
```

- **`-classpath`, not `-bootclasspath`** — since JDK 9 the latter is refused
  unless `-source/-target` is 8 or lower, and the JDK here is 21 (§5.5's
  correction to §1.9).
- `d8 --output` takes a **directory**, into which it writes `classes.dex`.
- `--min-api` is `$WT_API` (30), the manifest's own minSdk.
- `classes.dex` must sit at the **APK root** — hence `zip -j`. It takes ordinary
  deflate: nothing mmaps it and nothing aligns it, unlike the `.so`'s `-0`/`-P 16`
  pair. It is 1436 bytes.
- The step compiles a **tree**, not a named file, so a second class costs no
  script edit.

`VERIFY 5/5` was added at the tail: `aapt2 dump badging`'s launchable-activity
line plus the APK listing of `classes.dex` and the `.so`.

### 12.4 The component name CHANGED

`android.app.NativeActivity` → `.MainActivity`. The launch command is now

```bash
adb shell am start -n com.warptempo.gui/.MainActivity
```

and `am start -n com.warptempo.gui/android.app.NativeActivity` no longer
resolves. `build_apk.sh` prints the new line at the end. §10.1's command block
carries the new component; the scoping doc's is still stale by that one word.

### 12.5 What was driven on the device (policy_control **null** throughout)

The stopgap was deleted BEFORE the install, so every check below ran with the
platform's own defaults.

- **`aapt2 dump badging`**:
  `launchable-activity: name='com.warptempo.gui.MainActivity' label='Warptempo'`.
- **No status bar, no navigation bar, no taskbar** on the launch screenshot; the
  native window is `2304x1440` (logcat's own line) and the bottom row sits at the
  panel's bottom edge with nothing over it.
- **The bottom row RECEIVES input.** A tap at `(101, 1397)` — the transport's
  far-left PLAY glyph, in the exact band the taskbar used to eat — swapped the
  glyph to STOP and the clock ran `00:11.848 → 00:21.338 → 00:23.852`; a second
  tap at the same point returned the glyph to Play and the clock to the resting
  `00:00.000`. This is the check §11.7 could only pass under `policy_control`.
- **The transient bars behave.** A swipe up from the bottom edge showed the
  taskbar; ~8 s later it was gone on its own, the app still focused and still
  running, and a tap on PLAY immediately after still reached the app.
- **BACK and relaunch.** BACK returned to the launcher without killing the
  process (pid 30177 throughout); `am start` on the new component came back up
  identical and immersive, and `window 2304x1440, tick 8 ms` appears **twice** in
  the one process's log — `android_main` runs a second time, as §10.5 recorded.

### 12.6 Deliberate limits

- **A swipe from the bottom edge is the system's, not the app's.** Under
  `BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE` that gesture summons the bars instead
  of reaching the product, so a nav-surface drag that STARTS on the panel's very
  bottom edge is unavailable. The bottom row is chrome (buttons act at the lift),
  not a drag surface, so nothing the product does is lost — but the trade is real
  and it is the price of the row being reachable at all.
- **No `System.loadLibrary`, no JNI, no callbacks** — see 12.2. The class is
  deliberately the smallest thing that solves the input problem.

## 13. The on-screen keyboard (M5, 2026-08-27)

The glass had no way to type. This is the surface that gives it one, and it is
PRODUCT code in `src/gui/` rather than anything Android-specific: the only
platform-shaped parts are the two seam members that gate it.

> **Two parts of this section were superseded LATER THE SAME DAY**, on the
> architect's first glass drive: the band's 1px TOP SEAM is deleted (its ground
> is the bottom row's, so the two lanes read as one block), and every FUNCTION
> KEY now says its word on the sans face instead of wearing a Breeze glyph —
> Shift, Backspace, Space, Cancel, Enter — which took §13.5's five icons out
> whole (`kIconCount` 51 → 46, `dialog-cancel` untouched, Render's mid-render
> face being its own reader) and left shift's one-shot arm on the ARMED FACE
> alone. `docs/engineering/architecture/platform-seam.md` and
> `src/gui/onscreen_keyboard.h` are authoritative; what follows is the record of
> what M5 landed.

### 13.1 The shape

A four-row Maliit-shaped keyboard (reference: Plasma Mobile's own, Breeze Dark),
**full window width, directly above the bottom row**, painting over the waveform
area's lower part. It stands while **any of the seven text editors** stands and
leaves with it. It replaces nothing — the flag editor still paints in the marker
lane, a dialog editor still paints in the bottom row with its own buttons — and
there is **no second text buffer anywhere**: the live editor's own
`text_editor::State` is the only text state in the product, exactly as before.

| Row | Letters | Symbols (`&123`) |
|---|---|---|
| 1 | `q w e r t y u i o p` | `1 2 3 4 5 6 7 8 9 0` |
| 2 | `a s d f g h j k l` | `. / + - * : # \| '` |
| 3 | `SHIFT` `z x c v b n m` `BACKSPACE` | *(blank)* `, ; = [ ] _ ␣` `BACKSPACE` |
| 4 | `&123` `,` `SPACE` `.` `ESC` `ENTER` | *(the same row — one array, shared)* |

Widths are authored in QUARTERS of a standard key and a row is 40 of them; a row
whose spans sum to less is centred (row 2's nine letters, the reference's own
half-key indent). The key PITCH is therefore the window's — ten keys across 2304
px is 230 px each — and only the row height (40), the gaps (4) and the outer pad
(4) are authored at 100% and scaled on `gui_scale`. At 225% that is a 407 px
surface of 90 px keys — the figure the off-device harness measured (§ below).
That is the tablet's own scale: its first-run template answers 225% (250 held
the template for one afternoon on 2026-08-27 and was stepped back the same
evening for the icon row's ~3 px crop).

`SHIFT` is **one-shot**: tap it, the next letter is a capital, then it clears; a
second tap while armed clears it; there is no caps lock. `&123` toggles the
symbol layer and reads `abc` while it stands, and leaving the letter page LEAVES
A PENDING CAPITAL STANDING — like every other key that types no letter, since
only a letter can spend the arm and every letter is on the page the toggle came
from, so a round trip to the symbols and back finds the arm where it was left.
*(This paragraph said the opposite — the toggle clearing the arm because the
lamp does not paint on the symbol page — until 2026-08-27; the input owner is
authoritative, `input_pointer.cpp`.)* The two lamps are session-scoped: they
are keyed to the live editor's `text_editor::State::session`, so a close, a
reopen or a retarget clears them by comparison rather than by a list of close
sites. No globe, no language label, no hide key, no long-press alternates, no
chords, no Left/Right keys.

### 13.2 Keys are HOTKEYS and act at the PRESS

A key's press calls `GuiPlatform::synthesize_key(keysym, stable_code, true,
codepoint)` and its lift the matching `false`, so the **ordinary key path runs
unchanged** from there: `GuiInputHandler::on_key`, the keyboard-modal gate,
`route_modal_editor_key`, each editor's own vocabulary and byte cap and
red-flash, the undo coalescing, and the core's repeat synthesis for a held key.
Nothing about the editors was mirrored, and a new editor gets a working keyboard
by existing.

- `stable_code` is the key's PLACE in the layout table (layer, row, column) —
  not the keysym, because two layers put different characters on one slot and
  one character sits in two slots.
- The CASE travels in the `codepoint` alone; the keysym is the lowercase base,
  which is what `GuiKey` (ASCII case-folded) already is. No modifier bit is set
  for a capital — `text_editor::classify_key`'s `PrintableKey` arm reads the
  codepoint, and this backend sets no modifiers at all.
- Every printable ASCII character IS its own X11 keysym, so the punctuation this
  keyboard types needed no new names in `GuiKeys`.
- A held key repeats because the core arms on the press: `pointer_button` clears
  `repeat_key_` BEFORE dispatching the press, so the arm made inside the press
  body survives, and the Android backend's `key_codepoints_` map — which
  `synthesize_key` fills — is what re-answers the codepoint probe per repeat.
  That map has had exactly this consumer in mind since M3b; this is the first
  one.
- The editors' own **buttons** (a dialog's OK / Cancel) are unchanged chrome and
  still act at the LIFT. That is the modality ruling's split read straight.

### 13.3 The two seam members

`wants_onscreen_keyboard()` and `synthesize_key()` are now on BOTH backends with
identical declarations, so `platform_android.h`'s public surface has **no
additions to the seam at all** for the first time (`synthesize_key` had stood
there alone as the one addition; the keyboard's press router is an ordinary
consumer, so it had to be callable against either backend). Wayland answers
`false` and forwards; Android answers `true`.

That predicate is the laptop's whole guarantee: `onscreen_keyboard::stands()` is
`gui.wants_onscreen_keyboard() && app.text_editor_session() != 0`, and **every**
paint and hit site asks it and nothing else — the painter returns at its head,
the press claim returns false, the pan-zone yield returns false, and the tick
comparator sees no drift. On the laptop the feature costs one bool per frame and
one per tick, and changes no pixel and no route.

### 13.4 Hit, paint and damage

- **Hit**: the press claim sits ABOVE EVERY GATE in `on_button_press` — above
  the dialog editors' VEIL in particular, which would otherwise swallow the
  press that types into the very editor raising it. It claims the whole rect: a
  finger in a gap, in the margin or on the blank slot **consumes** rather than
  falling through to the waveform's pan underneath. The release mirrors it at the
  head of `on_button_release`, guarded on the held index alone, and fires even
  when the press's own act (Enter, Esc) closed the editor under it — the key-down
  was delivered, so its pair is owed.
- **The pan zone yields under the surface** (`touch_point_in_pan_zone`), for the
  same reason the shown trim overlay yields: the whole waveform is a pan zone, so
  without this a finger on a key would become the phone-model pan and never
  deliver a press — every key would need a hold beat to type one character.
- **Paint**: `GuiPaintHandler::paint_onscreen_keyboard`, between the waveform
  passes and the three floating surfaces. Colours are three EXISTING constants
  (architect ruling, measured off his own Maliit screenshot): key face
  `kRedesignRowGround`, the ground around and between keys
  `kRedesignContentGround`, caps and glyphs `kRedesignLabel`. Pressed and armed
  reuse the icon row's own CLICK and SELECTED faces verbatim.
- **Damage**: per key on press and lift; the show and hide are the tick
  comparator's (`main.cpp`, the roster faces' own mechanism) — the editors' open
  and close damage the marker lane or the bottom row, never this band, so the
  live answer is compared against an as-painted bit and a drift pays a full
  waveform-area repaint plus the surface's own rect.

### 13.5 The five new icons

`keyboard-caps-disabled` / `keyboard-caps-enabled` (SHIFT's two faces off the one
lamp bit — the stateful-glyph shape Save and Render already wear),
`keyboard-enter`, `keyboard-spacebar` and `edit-clear-locationbar-rtl`
(BACKSPACE — the left-pointing tag with an X, which is what Plasma's own virtual
keyboard puts there). ESC wears the already-committed `dialog-cancel`. All five
are unmodified breeze-dark files, committed under `assets/icons/breeze/` with
their `d` strings transcribed verbatim (checked byte-for-byte against the
assets), and every one is a single `<path>` the interpreter's oldest arms already
cover — no departures, no new grammar. `kIconCount` 48 → 53.

### 13.6 What is NOT verified

**Nothing of this was driven on the device.** The tablet's book cover was shut
for the whole session (`dumpsys power`: `mIsCoverClosed: true`,
`mLastSleepReason=cover_close`), which dozes the display and disables the touch
panel; `KEYCODE_WAKEUP`, `KEYCODE_POWER` and `svc power stayon` all leave
`mWakefulness=Dozing`, and `adb` is not root on a production build, so the hall
sensor cannot be overridden. Every screenshot comes back pure black.

What WAS verified: the APK builds and installs, the process launches and runs
(marker parse, AAudio open, target render all logged as before), and the
frame-rate pin returns 0. The keyboard's GEOMETRY and its GLYPHS were verified
off-device with a scratchpad harness that links the real
`onscreen_keyboard.h` walk and the real `icons.cpp` against the real
`main.cpp` lane stack at 2304x1440 / 225%: 34 keys per layer, rows aligned and
centred as designed, surface 2304x407 seated at y=927 flush on the bottom row at
1334, and all five new glyphs render. **The device drive — open the flag editor,
type, commit, hold backspace — is owed.**
