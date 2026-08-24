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
`ALooper_pollOnce` of an iteration waits 8 ms and the rest drain with a 0 timeout,
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
