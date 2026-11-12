# Installing and keeping warptempo_gui

This file is how warptempo_gui is installed, built, run for the first time and kept running on its two devices. It is a copy-paste recipe: paste one block at a time into a terminal on the laptop, read what it prints (the `#` lines say what to expect), and only then go on to the next. Blocks run from the root of this repository unless they say otherwise. How to use the program is not here: its tooltips (the tooltip lamp, bare `\`) and [`HELP.md`](HELP.md) carry that.

The two devices are one Android tablet (a Galaxy Tab S10 FE), which runs the program, and an Arch Linux laptop, which builds the tablet's APK and renders with the headless `warptempo_cli`. The pieces' marker files live in their own public repository, [warptempo_projects](https://github.com/warptempo/warptempo_projects), and each device keeps its own clone of it. On the tablet the program itself commits, pushes and pulls those files (Save and Commit: the SAVE key's long press in the main window, or SAVE in the history view); the laptop never commits, taking the tablet's commits with plain git when it wants them. What git does not carry, the source audio and the scores in and nothing out, travels by `scripts/warptempo_sync` (Daily use); a render is never brought back from the tablet: the laptop pulls the committed markers and renders the same file again.

Every value that belongs to one person's setup is an environment variable. Export these in your shell profile (`~/.bashrc` or the like) and open a new terminal:

| Variable | What it holds |
|---|---|
| `WARPTEMPO_TABLET_ADDR` | the tablet's wireless adb address, host:port (e.g. `192.168.0.20:5555`); read by `scripts/warptempo_sync` |
| `WARPTEMPO_TABLET_DEPLOY_KEY` | path of the tablet's GitHub deploy key; its `.pub` lies beside it; read by `scripts/warptempo_sync` |
| `WARPTEMPO_PROJECTS_PATH` | the laptop's projects folder: the `projects/` folder of its projects clone (e.g. `$HOME/.warptempo/warptempo_projects/projects`); read by `scripts/warptempo_sync` |
| `WARPTEMPO_TABLET_HOME` | the laptop's folder of tablet material: its `noise/` (the background sounds) and `themes/` (your own theme files, `<name>.theme`) folders, mirrored to the tablet; read by `scripts/warptempo_sync` |
| `WARPTEMPO_KEYSTORE_BACKUP` | path of your backup copy of `~/.android/debug.keystore` |
| `WARPTEMPO_SIGNING_CERT_SHA256` | the SHA-256 fingerprint of that keystore's certificate, as keytool prints it |
| `ANDROID_SERIAL` | the tablet's USB serial (adb's own variable) |

adb obeys `ANDROID_SERIAL` on every command, so with it exported a plain `adb …` talks to the tablet over the cable, even when the wireless link is up too. Over wireless alone, name the device: `adb -s "$WARPTEMPO_TABLET_ADDR" …`. Until you know the serial (a new tablet, below), leave it unset.

## The laptop

The laptop builds the tablet's APK (The tablet, below) and renders with the headless `warptempo_cli`; it runs no window of the program.

### Packages

The CLI needs a C++23 compiler (GCC 12+ or Clang 16+), CMake 3.20+, pkg-config, fftw3 (with fftw3_threads) and git. Where the distribution's compiler is older than GCC 12 (Ubuntu 22.04 ships GCC 11), install `g++-12` or later and configure with `-DCMAKE_CXX_COMPILER=g++-12` added. Under WSL2 the CLI builds and runs from the Debian / Ubuntu packages. macOS is not supported.

The APK's build adds adb and the Android build's host tools: meson and ninja (the cross-built libraries), zip and unzip, Rust (rustup with its stable toolchain and the `aarch64-linux-android` target, which builds the APK's resvg), and resvg and ImageMagick 7 (`resvg` and `magick`: the APK build renders the launcher icon from `assets/icons/AppIcon.svg`, the logo strip's note, at every build; `resvg` must take `--stylesheet`, which gives the drawing its color). The APK cross-builds every library it links (The prebuilt libraries), so the laptop installs none of them; its fonts — Liberation Sans and Liberation Sans Bold — are the repository's own (`fonts/`), so no font package is installed either. The score tool (A movement's score, below) adds poppler for a PDF.

```bash
# Arch (the host it is developed on):
sudo pacman -S --needed base-devel cmake pkgconf git fftw \
    android-tools meson ninja zip unzip imagemagick resvg rustup poppler
rustup default stable
rustup target add aarch64-linux-android   # the APK's resvg
pacman -Q git cmake fftw android-tools meson ninja zip unzip imagemagick resvg rustup poppler
# every line shows a version; "was not found" names a package still to install
rustup target list --installed
# lists aarch64-linux-android

# Debian / Ubuntu:
sudo apt install build-essential cmake pkg-config git libfftw3-dev \
    adb meson ninja-build zip unzip imagemagick resvg poppler-utils
# then Rust, from https://rustup.rs (the distribution's cargo may be too old):
#   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
#   rustup target add aarch64-linux-android
# a distribution whose ImageMagick is 6 has no `magick`: install 7 from imagemagick.org

# Fedora:
sudo dnf install gcc gcc-c++ cmake pkgconf-pkg-config git fftw-devel \
    android-tools meson ninja-build zip unzip ImageMagick resvg poppler-utils rustup
rustup-init   # then: rustup default stable; rustup target add aarch64-linux-android
```

### Build

```bash
cmake -B build -S .
cmake --build build -j$(nproc)
# the laptop's one product: build/warptempo_cli
```

Leave `CMAKE_BUILD_TYPE` unset: the flags `-O3 -march=native -ffp-contract=off` are always on, and asserts stay live. A debug build goes in its own folder (`cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug`), never over `build/`. `-march=native` makes the binary fit this CPU alone, so build on the machine that runs it; `-ffp-contract=off` is what keeps a rebuilt binary rendering byte for byte what the last one did. What can still move the bytes, by a least significant bit here and there, is an upgrade of glibc or FFTW underneath: after one, re-render anything you mean to compare against.

There is no `make install`: the CLI is run from `build/`.

### Rendering

```bash
./build/warptempo_cli "path/to/project/source.wav"
# it renders the project's <title>.wav into its render/ folder, the active tab's trim applied:
# the file the tablet's render writes, through the same stages, to within a few ULPs
```

It reads the three sidecars beside the source it is given and writes nothing else into the project: no `.fingerprint`, and an older title's render in `render/` is left standing. A folder without its sidecars refuses with `Missing '<path>'`: a new project is begun on the tablet, whose first open writes them, and reaches the laptop as its commit.

A project is a folder directly under `projects_path`, named by its folder. It either carries the three sidecars `<stem>.warpmarkers`, `<stem>.phaseresetmarkers` and `<stem>.settings` beside `<stem>.wav`, or carries none and holds exactly one `.wav`, which makes it a new project whose first open on the tablet writes the three. A folder with some sidecars must have all three; any other shape refuses to open with the reason stated. The sidecars are program-written and strictly validated: a hand edit that breaks one fails the load, with the first error on stderr. A name that is `.` or `..`, begins or ends with whitespace, or holds a line break is not a project. The source is stereo WAV, 16- or 24-bit PCM, 44100 Hz or above; convert anything else once with an external tool (ffmpeg). The program writes into two folders beside the source: `render/` holds the deliverable, `<title>.wav` and its `.fingerprint`, and nothing else (other pairs there are deleted at the next publish); `tmp/` holds the batch renders the render player walks.

The laptop has no config file (the `~/.config/warptempo_gui/` folder of the laptop GUI's days was retired 2026-11-06): `scripts/warptempo_sync` finds the laptop's projects at `$WARPTEMPO_PROJECTS_PATH` and mirrors the `noise/` and `themes/` folders under `$WARPTEMPO_TABLET_HOME` to the tablet (Daily use), and the CLI reads no config at all.

On a new laptop, make the projects clone so that `$WARPTEMPO_PROJECTS_PATH` is its `projects/` folder. It is public, so the clone needs no key; plain git's fetches and pulls then go over SSH on port 443 with the laptop's deploy key (Trouble, "the laptop has no deploy key"):

```bash
git clone https://github.com/warptempo/warptempo_projects.git ~/.warptempo/warptempo_projects
git -C ~/.warptempo/warptempo_projects remote set-url origin ssh://git@ssh.github.com:443/warptempo/warptempo_projects.git
git -C ~/.warptempo/warptempo_projects config core.sshCommand "ssh -F /dev/null -i ~/.ssh/warptempo_projects_deploy_key -o IdentitiesOnly=yes -o UserKnownHostsFile=~/.ssh/known_hosts"
# the last line makes plain git in the clone use the deploy key
```

The audio is not in the repository: put each piece's source `.wav` into its folder under `projects/`, named `<stem>.wav` after the stem its sidecars share. `main` is the only branch; the program refuses any other.

### A movement's score

The score viewer (the SCORE key; [`HELP.md`](HELP.md)'s Score) shows the pages of a sheet-music video or of a PDF of the printed score. Make the pages once per video or PDF with the tool (python3 with numpy and Pillow; ffmpeg and ffprobe on the PATH for a video, poppler's `pdftoppm` and `pdfinfo` for a PDF). From a video it writes `page001.jpg`, `page002.jpg` … (grayscale JPEG, each page already the size the tablet's viewer shows it at, about a tenth of the PNG the tool wrote before), `pages.txt` and `audio.wav` (the video's soundtrack, which the viewer plays: 16-bit stereo at the soundtrack's own rate, 265 MB for a 23-minute symphony) into the folder it is given, creating it, and prints one line per page found:

```bash
tools/score_extract.py "<the video or the PDF>" "$WARPTEMPO_PROJECTS_PATH/<piece>/score"
# a few minutes for a 4K video; the last line: "wrote N pages, pages.txt and audio.wav to …"
# (for a PDF: "wrote N pages and pages.txt to …")
```

It needs no option: a page is a still frame that carries bar lines, so a cover, a title card or a flash is left out by itself, and each page holds the video from where it first shows until the next one shows, so the pages cover the whole video. A second run reuses the first one's scan. The pages are sized for the tablet's screen at its 200 %; for another screen or scale, open the score viewer there once and read its box off the log (`adb logcat -s warptempo:I`), `warptempo_gui: score page box 2256x1145`, then run the tool again with `--fit 2256:1145` (that pair). A rerun deletes the folder's old `page<NNN>.png`, and `scripts/warptempo_sync tt` deletes the tablet's.

From a PDF (the file starting `%PDF-`; `--rate` is refused, `--fit` and `--url` work as for a video) the tool cuts each printed page between its systems: one scale for the whole PDF, set by its tallest system, so a page the viewer shows holds whole systems with their staves at one size, the first staff at the same height, and the next system's top may show cut off at the bottom as a preview; a page whose systems the tool cannot read (a recitative's short systems) is cut where the other pages are cut on average, each cut moved to a blank row where one lies within reach and else left where it stands, so that page may be cut through a staff and an occasional slice may repeat part of another; a page of the PDF without staves (the cover, a preface, a catalog) is left out, with one line saying so. Its folder has no `audio.wav` (a rerun into a folder that held one deletes it, as `scripts/warptempo_sync tt` deletes the tablet's), so the viewer turns its pages by hand.

The SCORE key lights at the next open of that piece: a piece whose folder holds `score/pages.txt` needs no typing. For another movement of the same video, open that movement's piece and type into the Settings line `score_path=../<the first piece>/score` (for instance `score_path=../550 - 1/score`); the path is relative to the piece's folder, and an empty `score_path=` turns the score off. The SCORE key lights once the folder is read; a path with no score folder behind it is kept and says so on a card, and a folder whose `pages.txt` is out of the tool's grammar, or misses a page it names, is refused in the Settings line. At a piece's open such a folder only means no score (the SCORE key dimmed, one line in the log); the piece loads as ever. A score folder without a playable `audio.wav` (missing, or not 16-bit stereo at `pages.txt`'s rate) is still a score: the viewer turns its pages by hand, and one line in the log says why. `score_path`, the page range and each tab's remembered position (with a soundtrack the exact point it was left at, else the start of the page shown) live in the piece's `score/viewer.txt`, written by the app at every change and never saved with the piece, committed or carried by the sync script: the tablet keeps its own, and a missing or damaged one only means the defaults (a damaged one says so in the log and is rewritten at the next change). The TRIM key's named trim presets live beside them in the piece's `trim/presets.txt`, likewise written by the app and never committed (the projects repository ignores `projects/*/trim/`) or carried: a damaged one reads as no presets, with one line in the log, and is rewritten at the next change. `scripts/warptempo_sync tt` carries the pages, `pages.txt` and `audio.wav` to the tablet, deleting there what the laptop's folder no longer holds: a page `pages.txt` does not name, and the `audio.wav` of a folder now made from a PDF (Daily use).

### Restoring a laptop

After an OS reinstall, install the packages (above), then restore these from your backup before anything else:

- `~/.android/debug.keystore` (the key that signs the APK) and `~/.android/adbkey` (the tablet already trusts it)
- the laptop's deploy key pair, `~/.ssh/warptempo_projects_deploy_key` and its `.pub`
- the folder `$WARPTEMPO_TABLET_HOME` names, with its `noise/` and `themes/` folders (the tablet's are mirrored from them)
- the tablet's deploy key pair at `$WARPTEMPO_TABLET_DEPLOY_KEY`, and the keystore copy at `$WARPTEMPO_KEYSTORE_BACKUP`
- this repository, with `deps/prebuilt/` if you keep it (else rebuild it: The tablet)
- the projects clone, with its audio and renders

```bash
ls -l ~/.android/debug.keystore ~/.android/adbkey ~/.ssh/warptempo_projects_deploy_key "$WARPTEMPO_TABLET_DEPLOY_KEY"
ls -d "$WARPTEMPO_PROJECTS_PATH" "$WARPTEMPO_TABLET_HOME/noise" "$WARPTEMPO_TABLET_HOME/themes"
# the projects folder of the clone and the tablet material scripts/warptempo_sync reads
```

```bash
projects="$WARPTEMPO_PROJECTS_PATH"
git -C "$projects/.." fetch
git -C "$projects/.." status -sb
# first line:  ## main...origin/main
# "[ahead N]"  = commits here GitHub has not got
# "[behind N]" = GitHub has newer: git -C "$projects/.." pull --ff-only takes them
# "Are you sure you want to continue connecting": check the fingerprint is one of GitHub's,
#   SHA256:+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU (ed25519)
#   SHA256:p2QAMXNIC1TJYWeIOttrVc98/R1BUFWu3/LiyKgUfQM (ECDSA)
#   SHA256:uNiVztksCsDhcc0u9e8BujQXVUpKZIDTMczCvj3tD2s (RSA)
#   then type yes. Anything else: type no and stop.
# "Permission denied (publickey)" = the deploy key is missing (Trouble)
```

Then build the CLI (Build, above) and render a piece with it (Rendering). With the cable in, `adb devices` shows the tablet's serial, then `device`: the restored `adbkey` means the tablet still trusts the laptop.

## The tablet

### The Android toolchain

```bash
bash deps/toolchain/bootstrap.sh
# no sudo; into ~/.local/android. About 1.1 GB downloaded (NDK, build tools, android.jar,
# a JDK), about 3 GB on disk, every download checked against a pinned checksum. Safe to run
# again: it skips what is there. The last lines list
#   NDK 29.0.14206865, build-tools 36.0.0, android.jar API 35
```

### The prebuilt libraries

The APK links fftw, freetype, harfbuzz, pixman, cairo, git (OpenSSL 3.6.4's libcrypto under libssh2 1.11.1 under libgit2 1.9.7) and resvg 0.48.1 (the renderer of the icon SVGs and the decoder of the score viewer's page images, built with Rust) statically from `deps/prebuilt/`, which is not in the repository. `THIRD_PARTY.md` lists each with its licence.

```bash
ls deps/prebuilt/arm64-v8a/lib/libgit2.a deps/prebuilt/arm64-v8a/lib/libresvg.a
# both present: skip the next lines. libresvg.a alone missing: bash deps/85_resvg.sh
# (about 20 s), then bash deps/smoke/build_smoke.sh. Both missing: build them all (about 12 minutes)
bash deps/build_all.sh
```

### The signing key

The tablet accepts an update only when it is signed by the same key as the installed app. That key is `~/.android/debug.keystore`.

```bash
~/.local/android/jdk/bin/keytool -list -v -keystore ~/.android/debug.keystore -storepass android | grep -c "$WARPTEMPO_SIGNING_CERT_SHA256"
# expect:  1
# 0, or "Keystore file does not exist": restore it, then run the line above again
mkdir -p ~/.android && cp "$WARPTEMPO_KEYSTORE_BACKUP" ~/.android/debug.keystore
```

Never let a build make a new one: an APK signed by another key will not install over the app, and uninstalling the app deletes everything it holds on the tablet. `build_apk.sh` refuses to mint a key when none is there. Only for a brand-new app identity (a first install, nothing on the tablet to keep) run it once as `WT_NEW_KEYSTORE=1 bash apk/build_apk.sh`, then copy the new keystore to `$WARPTEMPO_KEYSTORE_BACKUP` and record its fingerprint, the `SHA256:` line of the keytool command above without the `grep`, in `WARPTEMPO_SIGNING_CERT_SHA256`. (`WT_KEYSTORE` points the build at a keystore elsewhere.)

### Build the APK

```bash
bash apk/build_apk.sh
# five VERIFY steps, then near the end "APK: …/apk/build-android/warptempo.apk"
# "no keystore at …": do The signing key
```

### A new tablet

For a new tablet, a replacement, or this one after a factory reset.

```bash
# 1. first boot: join the wifi, sign in to Google, skip every restore or transfer offer
# 2. Settings > Security and privacy > Auto Blocker > OFF (it blocks USB debugging);
#    if it offers to turn itself back on in 30 minutes, press Cancel
# 3. Settings > About tablet > Software information > tap "Build number" 7 times;
#    Settings > Developer options > USB debugging > ON
#    (optional: Default USB configuration > Transferring files, or the "charging via USB"
#    notification comes back at every plug-in)
# 4. Settings > Lock screen and AOD > Screen lock type > None. With a lock, closing the
#    cover locks the tablet, and Samsung cuts USB debugging while it is locked.
```

```bash
# 5. connect the cable; on the tablet: "Allow USB debugging?" > Always allow > Allow
adb devices
# one line: the tablet's serial, then "device" ("unauthorized": accept the prompt).
# Export that serial as ANDROID_SERIAL in your profile now.
adb shell settings list secure | grep rampart
# with Auto Blocker really off, every line printed ends in 0
# if step 4's menu hid "None" behind a PIN:
#   adb shell locksettings clear --old <PIN>
#   adb shell locksettings set-disabled true
```

```bash
# 6. sound: only media may make a noise. Settings > Sounds and vibration: sound mode Sound;
#    Notification 0, System 0, Ringtone and Alarm lowest; "Use volume buttons for media" ON.
#    Notification and system from the laptop:
adb shell cmd media_session volume --stream 5 --set 0
adb shell cmd media_session volume --stream 1 --set 0
# 7. Settings > Display > Font size and style > Font style > Default (a custom style also
#    replaces monospace). Software update: auto download OFF (an update can change audio and
#    USB behaviour; update by hand when you can test). Screen timeout to taste. Pair Bluetooth.
```

```bash
# 8. install the app over the cable (build it first)
adb install -r apk/build-android/warptempo.apk
# expect:  Success
# Play Protect or "unknown app": More details > Install anyway
```

### Put the projects on it

`scripts/warptempo_sync setup` makes the tablet ready in one command. It shows its plan (every file it would delete or copy over, the settings file it would write, the audio) and asks `Proceed? [y/N]`; any answer but `y` changes nothing, and nothing is backed up. It starts the app once if it never ran (with no piece yet, it closes), places a fresh clone of GitHub's projects on the tablet, gives the tablet its deploy key from `$WARPTEMPO_TABLET_DEPLOY_KEY`, corrects the tablet's config (the projects repository, and each line of the repository's `assets/device_config_defaults` — the app's first-run values, below — but `gui_scale`, `projects_repo`, `projects_path` and `last_project`, where its key has no line (a line already there is kept, whatever it holds: a level, `-9` or `-9.0`, or any tuned value), and no retired key — the `bevel_light=`, `bevel_dark=`, `strip_pad=`, `rain_fade_in_ms=`, `rain_fade_out_ms=`, `rain_level=`, `rain_crossfade_ms=`, `noise_crossfade_ms=`, `role=`, `waveform_column_px=`, `waveform=`, `chrome=`, `caption=`, `tooltip_gap=` and `audio_level=` lines deleted among them, so a level tuned under the old name comes back as the defaults file's unless the line is renamed by hand first, and a crossfade in milliseconds as the defaults file's seconds), mirrors the noise folder (below), copies the audio and starts the app. THE NOISE: the laptop's `$WARPTEMPO_TABLET_HOME/noise/` folder is mirrored whole into the app's `files/warptempo_gui/noise/` — every `.wav` of every sound folder copied where the tablet's differs (about 400 MB the first time), and every recording and sound folder the laptop no longer holds deleted there; with no `noise/` on the laptop, or no sound folder with a `.wav` in it, setup says so in one line and places or deletes nothing (the NOISE slider stays as it is). `tt` mirrors it the same way at every run. If you have no tablet key yet, make one as the laptop's (Trouble, "the laptop has no deploy key") with `-f "$WARPTEMPO_TABLET_DEPLOY_KEY"`, titled `tablet` on GitHub, with write access.

```bash
scripts/warptempo_sync setup -n
# the plan alone, and nothing on the tablet changes
scripts/warptempo_sync setup
# the same plan, then "Proceed? [y/N]": read it, then type y. The last line starts "done:"
```

Then check. On the tablet, tap Toggle History View in the icon row (hold the S Pen just above an icon to read its name); after a moment the bottom row ends with `GitHub: up to date`. From the laptop:

```bash
adb logcat -d -s warptempo:I | grep -E 'working_column|GitHub'
# a line with working_column= (a piece opened), and none with "GitHub refused" or "GitHub offline"
adb shell run-as com.warptempo.gui cat /data/user/0/com.warptempo.gui/files/warptempo_gui/config
# nineteen lines, in this order (the values here an example; a first run writes the lines of the repository's assets/device_config_defaults, below):
#   gui_scale=200                 (the tablet's scale, which a first run stamps; change it in the app)
#   projects_repo=github.com/warptempo/warptempo_projects
#   projects_path=/storage/emulated/0/Android/data/com.warptempo.gui/files/projects
#   last_project=<the piece open>
#   true_colors=on                (required; the first run and setup write the defaults file's value)
#   theme=default                 (required; the window's theme by name, `default` Live's Default Dark Cool Medium; the first run and setup write the defaults file's value; change it in the app)
#   waveform_grow=1               (required; the waveform's antialiasing grow in device pixels, a multiple of 0.25 in [0, 2]; the first run and setup write the defaults file's value; change it in the app)
#   noise=rain                    (required; the sound's folder under noise/; the first run and setup write the defaults file's value)
#   noise_level=-inf              (required; the sound's level in decibels, a decimal in [-60.0, 0.0] or -inf, which is off; the first run and setup write the defaults file's value; change it in the app, the NOISE slider)
#   noise_attenuation_level=-9    (required; the fixed decibels the sound plays at under the NOISE slider's number, a decimal in [-60.0, 0.0]; the first run and setup write the defaults file's value; change it in the app)
#   noise_crossfade_s=4           (required; the crossfade in seconds; the first run and setup write the defaults file's value; change it in the app)
#   key_height=17                 (required while it stands; the buttons' height under tuning, in W; the first run and setup write the defaults file's value; change it in the app)
#   key_width=35                  (required while it stands; the buttons' width under tuning, in W; the first run and setup write the defaults file's value; change it in the app)
#   run_gap=22                    (required while it stands; the space between two runs of buttons under tuning, in W; the first run and setup write the defaults file's value; change it in the app)
#   osk_key_w=24                  (required while it stands; the keyboard's key width under tuning, in W; the first run and setup write the defaults file's value; change it in the app)
#   osk_key_h=30                  (required while it stands; the keyboard's key height under tuning, in W; the first run and setup write the defaults file's value; change it in the app)
#   osk_font_px=12                (required while it stands; the keyboard's label size under tuning, in W; the first run and setup write the defaults file's value; change it in the app)
#   lamp_hold_ms=100              (required while it stands; the least time either lamp by the battery stays lit, in milliseconds, under tuning; the first run and setup write the defaults file's value; change it in the app)
#   tooltip_delay_ms=100          (required while it stands; the wait from the pen's arrival on a key to its tooltip, in milliseconds, under tuning; the first run and setup write the defaults file's value; change it in the app)
# /storage/emulated/0 and /sdcard are one folder; the tablet's clone is its files/ folder.
```

### The config

The program's config is the tablet's, `files/warptempo_gui/config` in the app's private folder (`/data/user/0/com.warptempo.gui/files/warptempo_gui`, beside the deploy key), reachable only through `adb shell run-as com.warptempo.gui` (the APK is a debuggable build). Change it in the app (the SETUP key), not by editing the file: a hand edit is for when the app is stopped, and a bad line stops the app. Nineteen lines, in this order, every one required: a missing line stops the startup, naming it. `audio_level=` WAS REMOVED 2026-11-11 (the level of the program's own sound, set by the icon row's AUDIO slider, retired with the slider: the program plays its files at their own level): a config still carrying it stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes it on the tablet). `tooltip_gap=` WAS REMOVED 2026-11-07 (the space between a tooltip and the button it stands above, a setting under tuning for part of that day, settled at 1 W, two of the tablet's pixels, and built in): a config still carrying it stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes it on the tablet). `tooltip_delay_ms=` JOINED 2026-11-07 (the wait from the pen's arrival on a key to its tooltip, a setting under tuning, after `lamp_hold_ms=`): a config from before it stops the startup on the missing line, so with the program not running add `tooltip_delay_ms=100` (`scripts/warptempo_sync setup` adds it on the tablet). `run_gap=` JOINED 2026-11-07 (the space between two runs of the program's buttons, a setting under tuning, after `key_width=`): a config from before it stops the startup on the missing line, so with the program not running add `run_gap=22` (`scripts/warptempo_sync setup` adds it on the tablet). `key_width=` JOINED 2026-11-06 (the width of the program's buttons, a setting under tuning, after `key_height=`): a config from before it stops the startup on the missing line, so with the program not running add `key_width=35` (`scripts/warptempo_sync setup` adds it on the tablet). `lamp_hold_ms=` JOINED 2026-11-05 night (the least time either lamp by the battery stays lit after its last event, a setting under tuning, after `osk_font_px=`): a config from before it stops the startup on the missing line, so with the program not running add `lamp_hold_ms=100` (`scripts/warptempo_sync setup` adds it on the tablet). `noise_attenuation_level=` JOINED 2026-11-05 night (the fixed decibels the background sound plays at under the NOISE slider's number, after `noise_level=`): a config from before it stops the startup on the missing line, so with the program not running add `noise_attenuation_level=-9` (`scripts/warptempo_sync setup` adds it on the tablet). THE LEVELS KEEP THEIR EXACT VALUE SINCE 2026-11-05 night: `noise_level=` and `noise_attenuation_level=` are written as the shortest decimal that reads back exactly (`0`, `-9`, `-15.6`, `-0.0108695652173913`); a line spelling one decimal, `-9.0`, loads as it is and is rewritten `-9` at the next commit, so nothing need change by hand. `theme=` JOINED 2026-11-05 (the window's theme, one of Live 12's, after `true_colors=`): a config from before it stops the startup on the missing line, so with the program not running add `theme=default` (`scripts/warptempo_sync setup` adds it on the tablet). `waveform=` WAS REMOVED 2026-11-01 (the waveform color presets dropped: the program has one palette, Ableton Live's colors, built in): a config still carrying it stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes it on the tablet). `waveform_column_px=` WAS REMOVED 2026-11-01 night (the waveform's column width, a tuning key required for part of that day, settled at one device pixel: every column and stem of the waveform is one device pixel at every scale): a config still carrying it stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes it on the tablet). THE LEVEL WENT DECIMAL 2026-11-05 (the icon row's slider): `noise_level=` takes a decimal in [-60.0, 0.0] or `-inf`; a line still spelling the whole number of before, `-9`, loads as it is (and is the canonical spelling again since that night, above). `role=` WAS REMOVED 2026-10-30 (one device runs the program, so no device is a mirror): a config still carrying it stops the startup like any unknown key, so with the app stopped delete the line (`scripts/warptempo_sync setup` deletes it). `osk_font_px=` JOINED 2026-10-29 night (the size of the on-screen keyboard's key labels, a setting under tuning, after `osk_key_h=`): a config from before it stops the startup on the missing line, so with the program not running add `osk_font_px=12` (`scripts/warptempo_sync setup` adds it on the tablet). `osk_key_w=` and `osk_key_h=` JOINED 2026-10-29 evening (the on-screen keyboard's key width and height, settings under tuning, after `key_height=`): a config from before them stops the startup on the missing line, so with the program not running add `osk_key_w=24` and `osk_key_h=30` (`scripts/warptempo_sync setup` adds them on the tablet). `key_height=` JOINED 2026-10-29 (the buttons' height, a setting under tuning, after `noise_crossfade_s=`): a config from before it stops the startup on the missing line, so with the program not running add `key_height=17` (`scripts/warptempo_sync setup` adds it on the tablet). `noise_crossfade_ms=` WAS REPLACED 2026-10-28 night by `noise_crossfade_s=`, the same crossfade in whole seconds, and `noise_level=` narrowed the same night to [-30, 0] (since 2026-11-05 [-60.0, 0.0] or `-inf`, decimal, above): a config still carrying `noise_crossfade_ms=` stops the startup like any unknown key, one without `noise_crossfade_s=` stops on the missing line, and a level below -60 stops on its value, so with the program not running replace the line with `noise_crossfade_s=4` and bring a lower level up to -60.0 (`scripts/warptempo_sync setup` deletes the old line and adds `noise_crossfade_s=4` on the tablet). `rain_level=` and `rain_crossfade_ms=` WERE RENAMED `noise_level=` and `noise_crossfade_ms=` 2026-10-28 and `noise=` JOINED the same day (the sound's folder, after `true_colors=`): a config still carrying the old names stops the startup like any unknown key, and one missing `noise=` stops on it, so with the program not running rename the two lines and add `noise=rain` (`scripts/warptempo_sync setup` deletes the old lines and adds the three where missing). `rain_fade_in_ms=` and `rain_fade_out_ms=` WERE REMOVED 2026-10-28, the same day they joined, settled at 200 and 40 ms and now built in: a config still carrying either stops the startup like any unknown key, so with the program not running delete the lines (`scripts/warptempo_sync setup` deletes them on the tablet). The level and the crossfade JOINED 2026-10-28 (as `rain_level=` and `rain_crossfade_ms=`, after `noise=`): a config from before them stops the startup on the missing line, so with the program not running add `noise_level=-inf` (or the level wanted) and `noise_crossfade_s=4` (`scripts/warptempo_sync setup` adds them on the tablet). The lit lamps' glow, a tuning key of 2026-10-23 (named with the retired keys under Migrating), WAS REMOVED 2026-10-26, settled at 4 and built in (the glow itself went 2026-11-01 with the lamps, the keys now flat): a config still carrying it stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes it on the tablet). `cue_triangle_rows=`, `cue_triangle_aa=`, `cue_text=`, `cue_lead=` and `cue_shadow=` WERE REMOVED 2026-10-26 (the cue tuning keys of 2026-10-26: `cue_shadow=` settled that morning at on; `cue_triangle_rows=`, `cue_triangle_aa=`, `cue_text=` and `cue_lead=` settled the same day at 8 rows authored at 300 %, the staircase, 22 and a gap of 8, now built in): a config still carrying any of them stops the startup like any unknown key, so with the program not running delete the lines (`scripts/warptempo_sync setup` deletes them on the tablet). `strip_pad=` WAS REMOVED 2026-10-23 (a tuning key, the toolbar's pad, 2026-10-22 to 2026-10-23; the pad was then built in at 13 Windows pixels, 39 device pixels at 300 %): a config still carrying it stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes it on the tablet). `true_colors=` JOINED 2026-10-21 (the Settings menu's True Colors row made a key, placed after `waveform=`): a config from before it stops the startup on the missing line, so with the program not running add `true_colors=on` (`scripts/warptempo_sync setup` adds it on the tablet). THE TUNING KEYS `bevel_light=` AND `bevel_dark=` WERE REMOVED 2026-10-23 with the walnut chrome (required 2026-10-21 to 2026-10-23), and `text_shadow=` beside them 2026-10-22: a config still carrying any of them stops the startup like any unknown key, so with the program not running delete the lines (`scripts/warptempo_sync setup` deletes them on the tablet). THE COLOR KEY WAS RENAMED AND THE CAPTION KEY REMOVED 2026-10-21: `chrome=` is `waveform=` (its value unchanged), and `caption=` is gone with the title bar's color schemes (the program draws no title bar since 2026-10-24); a config still carrying `chrome=` or `caption=` stops the startup like any unknown key, so with the program not running delete the line (`scripts/warptempo_sync setup` deletes both on the tablet; `waveform=` itself went 2026-11-01, above). (`icons=` and `font=` were removed 2026-10-12: one icon folder and Liberation Sans are the only icons and face, built in; an old line stops the startup like any unknown key, so with the program not running delete it.) There is one window style, the flat look after Ableton Live (2026-11-01), and no key chooses it. (The keys were renamed once before, 2026-10-13: `caption=` was `scheme=` and `chrome=` was `palette=`; a config still carrying a `scheme=` or `palette=` line stops the startup like any unknown key, so with the program not running delete the line.)

| Key | What it is |
|---|---|
| `gui_scale=` | the interface's scale, an integer percent in [50, 1000]: device pixels per W, the Windows pixel (96 dpi), the unit every length of the interface is written in (100 is Windows' own size; the tablet runs 200 since 2026-11-05, one W two of its pixels — 300 before that day, every length then written in its pixels, the picture the same to the pixel) |
| `projects_repo=` | the projects repository the history view commits to |
| `projects_path=` | the absolute folder whose subfolders are the projects |
| `last_project=` | the folder name opened last, written by the program at every open |
| `true_colors=` | `on` or `off`, required: whether the program converts its colors for the tablet's Display-P3 screen, so every color shows as a web browser shows the same hex. `on` in use; `off` only for a screenshot whose bytes are the colors as typed (the tablet's screen reads oversaturated meanwhile). Set it in the app, live: the Settings list's `true_colors` line lists `on` and `off`, a tap applying one; any other word stops the startup |
| `theme=` | the window's theme, required: a theme's name as the program spells it — `default` (Ableton Live 12's "Default Dark Cool Medium"), Live's stock themes (`default_dark_neutral_medium`, Live's own default and the colors HELP's Colors names, `default_dark_cool_medium`, Default's ten other variants, `angst_robot`, `classic_medium_dark`, `classic_medium_light`, `immaterial`, `immaterial_mod` — Immaterial with its own gold, shaded darker, in place of its pink — `riparian`, `twenty_four_carat`), the Sonic Bloom set as `sb_…` (`sb_ochre`, `sb_dash_mod` …), and your own theme files (below); the whole window changes with it, the waveform's colors included. Set it in the app, live: the Settings list's `theme` line lists every theme under its group's heading — SYSTEM, EXTRA, USER-DEFINED — a tap applying one; a name the list lacks opens in `default` and is kept; a word not spelled as a theme name (lowercase letters and digits joined by single `_`) stops the startup |
| `waveform_grow=` | how far the waveform's drawn shape is grown on every side before its edge pixels take their coverage, in device pixels (the one length on the waveform that is a setting: the waveform's own pixels are the screen's while its lines and the rest of the window scale with `gui_scale`, so the waveform keeps this one number of its own, and keeps it for good, since another tablet may not have this screen's density): a multiple of 0.25 in [0, 2] written as the shortest exact decimal (`0.5`, `0.75`, `1`, `1.25`; never `0.50` or `.5`); 1 the thickness chosen beside the two-pixel lines of the window; 0.5 the thickness at which the neighboring pixels share the weight exactly, 0.75 a step thicker than the picture of Ableton Live at 125 % Windows scale (which sits between the two at 0.625). Required; set it in the app, live: the Settings list's `waveform_grow` line lists 0 up to 2 in steps of a quarter, a tap applying one at once; any other spelling stops the startup |
| `noise=` | the background sound's folder: the name of one folder under `noise/` beside the config, one path component (not empty, no `/`, not `.` or `..`, no line break, no space at either end; anything else stops the startup). Required; set it in the app, live: the Settings list's `noise` line lists the folders there, a tap choosing one (in a typed line, Tab after `noise=` and a name's first letters completes it from them). A name with no folder behind it is kept and dims the NOISE slider |
| `noise_level=` | the background sound's loudness in decibels and its one switch: a decimal in [-60.0, 0.0] written as the shortest decimal that reads back exactly (`-6`, `-9.5`, `-15.6`; `-6.0` is read too and rewritten `-6`; never `-06`, `.5`, `+1`, `1e0`, `-0` or `-0.0`), the slider's exact value, or `-inf`, which is off — the recordings freed from memory — applied to the recordings as they are, with `noise_attenuation_level=` under it: at 0 the sound plays at the attenuation alone; above `-inf` the sound plays from the program's start. The slider shows it to one decimal, its own number alone. The first run writes the defaults file's level, the tablet's own as last snapshotted, so a fresh tablet plays the sound from its start unless that level is `-inf`. Required; set it in the app, live: the icon row's NOISE slider (drag to set, tap to turn it off and back), its one road — the Settings list has no line for it |
| `noise_attenuation_level=` | a fixed number of decibels the background sound plays at under the NOISE slider's number: the sound plays at `noise_level=` plus this, the slider showing, dragging and resetting its own number as ever; a decimal in [-60.0, 0.0] written as `noise_level=`'s is, never `-inf` (the slider is the sound's switch); -9 plays a full-scale moment of the sound at about a third of full scale with the slider at 0.0. Required; set it in the app, live: the Settings list's `noise_attenuation_level` line, typed |
| `noise_crossfade_s=` | the background sound's crossfade from one recording to the next, in whole seconds, an integer in [1, 30] written plainly. A sound of its own may want its own: the crossfade is how a recording joins itself. Required; set it in the app, live: the Settings list's `noise_crossfade_s` line lists 1 to 30, a tap applying one |
| `key_height=` | the height of the program's buttons (the toolbar's, the bottom row's and every box's), in W (one W = two of the tablet's pixels at its 200 %) and scaled with `gui_scale` like every other length, an integer in [10, 50] written plainly; the first run and setup write the defaults file's value (a line already there is kept, a tuned value included); 17 is 34 of the tablet's pixels. The buttons' width is `key_width=`'s, below. The drawing on each button follows the height, its longer side 9 / 17 of it (9 W, 18 pixels, at 17); each button's name is printed under it in small capitals. A setting under tuning, kept until its number is settled and then built in: required while it stands; set it in the app, live, the window laid out again at once: the Settings list's `key_height` line opens the line to edit. The on-screen keyboard's keys (the two keys below) and the clock keep their own heights |
| `key_width=` | the width of the program's buttons (the toolbar's, the bottom row's and every box's), in W and scaled with `gui_scale` like `key_height=`, an integer in [10, 50] written plainly; the first run and setup write the defaults file's value (a line already there is kept, a tuned value included); 35 is 70 of the tablet's pixels. Only the button widens or narrows: the drawing on it keeps the size `key_height=` gives it, and the name printed under it never widens a button, so below 32 W the longest names (MAGNIFY) run past their button's edges. On the tablet the toolbar holds every button up to 37 W; past it the buttons at the end of its left part give way to the right part (the history and view buttons), from their end — SETUP itself from 46 W, so a width that wide is undone by a hand edit of the config with the program not running. A setting under tuning like `key_height=`, kept until its number is settled and then built in: required while it stands; set it in the app, live, the window laid out again at once: the Settings list's `key_width` line opens the line to edit. The on-screen keyboard's keys keep their own width |
| `run_gap=` | the space between two runs of the program's buttons — the toolbar's runs, the bottom row's groups and the groups of every box's row of buttons (the render player's, the score viewer's) — in W and scaled with `gui_scale` like `key_width=`, an integer in [2, 44] written plainly; the first run and setup write the defaults file's value (a line already there is kept, a tuned value included); 22 is 44 of the tablet's pixels. One setting for every one of those spaces. On the tablet the toolbar holds every button up to 41 W (at `key_width=35`); past it the last button of its left part, NOISE, gives way to the right part (the history and view buttons), cut short from 42 W; on the bottom row the buttons reach further left over the state text, and in the render player and the score viewer the slider takes the difference. A setting under tuning like `key_width=`, kept until its number is settled and then built in: required while it stands; set it in the app, live, the window laid out again at once: the Settings list's `run_gap` line opens the line to edit |
| `osk_key_w=` | the width of the on-screen keyboard's one-wide keys (the letters, the digits, the symbols), in W and scaled with `gui_scale`, an integer in [12, 80] written plainly; the first run writes the defaults file's value (24 is 48 of the tablet's pixels). Every other key is as wide as its share of them (Space four, Backspace two and a half, and so on), with the gaps between them; the keyboard stands at the window's foot, the gap between its `5` and `6` keys on the screen's middle. A setting under tuning, kept until its number is settled and then built in: required while it stands; set it in the app, live: the Settings list's `osk_key_w` line opens the line to edit, the keyboard standing under it at the new size once it is applied |
| `osk_key_h=` | the height of the on-screen keyboard's keys, in W and scaled with `gui_scale`, an integer in [12, 80] written plainly; the first run writes the defaults file's value (30 is 60 of the tablet's pixels); the gaps between the keys are one W, the program's buttons' own; the keyboard's top stands at or above the lower channel's center line, the ground above and below its keys growing to reach it. A setting under tuning like `osk_key_w=`, set the same way |
| `osk_font_px=` | the size of the on-screen keyboard's key labels (the letters, the symbols and the words on the function keys), in W and scaled with `gui_scale`, an integer in [6, 24] written plainly; the first run writes the defaults file's value (12 is 24 of the tablet's pixels). Where a label grows past its key, it runs over the key's edges; there is no shorter form. A setting under tuning like `osk_key_w=`, set the same way, the keyboard standing under the line at the new size once it is applied |
| `lamp_hold_ms=` | the time either lamp by the battery (the limiter's and the clip's, in the logo strip) stays lit after its last event, in whole milliseconds, an integer in [20, 1000] written plainly (`100`; never `0100` or `+100`); the first run writes the defaults file's value — a lamp lit again before its time is up stays lit until the time after its last event. A setting under tuning like `key_height=`, kept until its number is settled and then built in: required while it stands; set it in the app, live from the next frame: the Settings list's `lamp_hold_ms` line, typed |
| `tooltip_delay_ms=` | the wait, with the tooltips on (the TIPS key), from the pen's arrival on a key — its face, its name under it and half the ground each side — to its tooltip, in whole milliseconds, an integer in [0, 1000] written plainly (`100`; never `0100` or `+100`); the first run writes the defaults file's value — and, once a tooltip has shown, the time after leaving that key within which the next key's tooltip shows at once. A setting under tuning like `key_height=`, kept until its number is settled and then built in: required while it stands; set it in the app, live from the next arrival: the Settings list's `tooltip_delay_ms` line, typed |
There is no color key: `theme` names a theme by name. THE THEMES SHIP WITH THE PROGRAM (2026-11-11): each a small text file of Ableton Live 12's own role colors, `<name>.theme` (one `Role=RRGGBB` line per color the program reads, `#` comment lines; `src/gui/theme_roles.h` owns the format; since 2026-11-12 a file may also carry two optional lines of the program's own, `PlayheadLine=RRGGBB`, the playhead's line down the waveform, and `ControlContrastFrameDisabled=RRGGBB`, a disabled marker's outline — left out, the line is the playhead's color half way to white and the outline the enabled markers'; no shipped file carries them, and the converter never writes them), in the repository's `themes/system/` (SYSTEM: Live's stock themes and `immaterial_mod`) and `themes/extra/` (EXTRA: the Sonic Bloom set, the six `…_mod` files hand-corrected, each saying what it changed), packed into the APK and read from its assets at every launch; `default` heads SYSTEM, a copy of Live's Default Dark Cool Medium, and Live's own Default Dark Neutral Medium is compiled in, listed as `default_dark_neutral_medium`. A shipped file the program cannot read stops the startup with one line naming it (a build defect). `tools/themes/ask_to_theme.py` turns a Live theme file (`.ask`, Live's theme XML; Live's stock ones are in its `Resources/Themes`) into a `.theme` once, on the laptop; no `.ask` file is in the repository or the APK, and the program reads none. YOUR OWN THEMES are `.theme` files directly in the `themes/` folder beside the config (`files/warptempo_gui/themes/`), listed under USER-DEFINED, each named by its file's name in lowercase with every run of other characters one `_` (`My Theme.theme` is `my_theme`); a subfolder there, or any other file, is ignored. A file the program cannot use — unreadable, a line out of shape (an optional line with eight digits among them), a color name it does not know or twice, one missing (the two optional lines excepted), or named like a shipped theme (`default`, `riparian` …) or an earlier file — is left out of the list with one line in the log naming it and why. The folder is read at launch only: a theme added while the program runs is listed at its next start. A `theme=` naming a theme the list lacks (its file removed) opens the window in `default`, one line in the log, the line kept until another is chosen. The tablet's `themes/` holds the laptop's `$WARPTEMPO_TABLET_HOME/themes/*.theme`, mirrored flat by `scripts/warptempo_sync tt` and `setup` (Daily use); its subfolders are never touched. The `chromes/` and `captions/` folders the color picker kept until 2026-10-14 are never read; delete them at will.

The icons ship with the program and are never copied: the SVG drawings in `assets/icons/`, every one KDE's Breeze icons recolored by `tools/icons/import_breeze.py` (rerun it to re-pick one; it reads the system's `/usr/share/icons/breeze-dark/`; the launcher's `AppIcon.svg` is a symlink to the note's drawing) — packed into the APK, read from its assets at every launch and rendered by resvg. A launch that cannot read them, or that finds a file that is not well-formed SVG, stops at startup with one line in its log naming the file and what is wrong.

The window's panel ships with nothing and reads no picture: its ground is one flat color, the theme's (Live's SurfaceArea: 22242D on `default`, 242424 on Live's own default), drawn by the program itself (`src/gui/materials.h`'s head).

The background sound (the NOISE slider; [`HELP.md`](HELP.md)'s Noise) is a folder under `noise/` beside the config (`files/warptempo_gui/noise/`), one folder per sound (`noise/rain/`, say), the `noise=` key naming the one that plays: every file in it whose name ends `.wav` is one of its recordings, played in random order, and a folder of one recording loops it onto itself. They must be 16-bit PCM, stereo and all at one sample rate; any other `.wav` there leaves the NOISE slider dimmed, with one line in the log naming the file and why. With no folder of that name, or no `.wav` in it, the slider is dimmed and nothing is said. All of the named folder's recordings are read into memory when the level first rises above `-inf` (at the program's start, where the kept level is above it) and kept until the program closes, the `noise=` key names another folder or the slider is left at `-inf`; nothing limits their size, and 35 minutes at 48 kHz, about 400 MB, is a sensible ceiling. Convert recordings once with an external tool (ffmpeg) to 16-bit stereo WAV at one rate. The tablet's `noise/` is the laptop's, `$WARPTEMPO_TABLET_HOME/noise/`, mirrored whole by `scripts/warptempo_sync tt` and `setup` (Daily use): a sound is added or changed on the laptop.

The first run writes the file's lines of the repository's `assets/device_config_defaults` — the same nineteen keys in the same order, their values the tablet's own config as last snapshotted (`tools/snapshot_device_defaults.sh <adb serial>` refreshes it from the tablet at the end of each arc of work, and the changed file is committed with it; the APK packs the file and the app reads it at every launch, so a file that breaks the grammar stops the startup like a bad config). Its `projects_path=` is `/storage/emulated/0/Android/data/com.warptempo.gui/files/projects`, the `projects/` of the app's external files folder, where `scripts/warptempo_sync setup` places the tablet's clone; its `last_project=` names the piece the tablet had open, and a fresh tablet without that folder opens its first valid project instead. Change every key but `last_project` in the app (the icon row's SETUP key lists each key as `key=value`; a tap on one opens that line to edit, the on-screen keyboard's Tab completing as it goes, or, for `true_colors`, `theme`, `waveform_grow`, `noise` and `noise_crossfade_s`, lists the key's values, a tap applying one; the background sound's level, `noise_level`, has no line there: it is set on the icon row's NOISE slider alone). A hand edit is for when the app is stopped (it rewrites the whole file at every commit), and a line that breaks the grammar stops the app at startup, naming the line in its log (`adb logcat -s warptempo:I`).

### Wireless adb

The tablet has one USB-C port, so the cable and a USB stick or DAC cannot share it.

```bash
adb tcpip 5555
# unplug the cable, then:
adb connect "$WARPTEMPO_TABLET_ADDR"
adb devices
# the address, then "device". It is lost at every tablet reboot: plug in and repeat.
# The address is in Settings > About tablet > Status information > IP address; a new
# tablet gets a new one: change WARPTEMPO_TABLET_ADDR in your profile.
```

### Update the app

```bash
bash apk/build_apk.sh
adb install -r apk/build-android/warptempo.apk
adb shell am start -n com.warptempo.gui/.MainActivity
adb logcat -s warptempo:I
# the app's log, live; Ctrl+C stops it. Over wireless, add -s "$WARPTEMPO_TABLET_ADDR" after adb.
```

## Daily use

`scripts/warptempo_sync` reads the laptop's projects from `$WARPTEMPO_PROJECTS_PATH` and its noise and themes from `$WARPTEMPO_TABLET_HOME` and finds the tablet on the cable first, then at `$WARPTEMPO_TABLET_ADDR`. `tt` takes piece names (folder names under `$WARPTEMPO_PROJECTS_PATH`) to narrow a run; with none it takes every piece. `-n` rehearses `tt` or `setup`, saying in words what it would do and doing none of it; `-v` adds the commands. It runs only from a terminal; with no verb it prints its usage.

Audio in, for a new piece (its folder under `$WARPTEMPO_PROJECTS_PATH`, holding its one `.wav`):

```bash
scripts/warptempo_sync tt
# copies every piece's source wav the tablet lacks, and each piece's score folder (its pages.txt,
# the pages it names and its audio.wav, never viewer.txt) where the laptop has one, then mirrors
# the laptop's noise/ and themes/ folders; the soundtrack's few hundred MB and the noise's ~400 MB cross once;
# the app is neither stopped nor started.
# Then on the tablet: the OPEN key (Open Project).
```

By hand, what it does for one piece:

```bash
piece="<the piece's folder name>"
wav="<its source file name>.wav"
projects="$WARPTEMPO_PROJECTS_PATH"
far=/sdcard/Android/data/com.warptempo.gui/files/projects
adb push "$projects/$piece/$wav" "$far/$piece/$wav"
adb shell "chmod 777 '$far/$piece'"
# a folder adb makes belongs to adb's shell user with mode 770, which the app cannot even
# list; the chmod opens it. Then on the tablet: the OPEN key (Open Project).
```

Your themes in, after the laptop's `$WARPTEMPO_TABLET_HOME/themes/` folder changes: `scripts/warptempo_sync tt`, which mirrors its `.theme` files flat, each copied where the tablet's differs and every `.theme` the laptop no longer holds deleted there, a subfolder on either side never read, copied or deleted; the tablet lists them at the app's next start. A new theme is a `.theme` file dropped directly there (`tools/themes/ask_to_theme.py` makes one from a Live 12 `.ask`).

The noise in, after the laptop's `$WARPTEMPO_TABLET_HOME/noise/` folder changes: `scripts/warptempo_sync tt`. It copies each recording the tablet's `files/warptempo_gui/noise/` lacks or holds otherwise, and deletes there every recording and sound folder the laptop's no longer holds (`tt -n` lists them); `setup` does the same as its noise step. A new sound is a new folder of `.wav` recordings under the laptop's `$WARPTEMPO_TABLET_HOME/noise/`, converted by your own means (the sound paragraph beside the config's key table); choose it on the tablet with the Settings list's `noise` line.

No render comes back from the tablet: commit there, pull on the laptop (below) and render the piece again with the CLI, which makes the same file to within a few ULPs.

A shell on the tablet as the app:

```bash
scripts/warptempo_sync ot "<piece>"
# in that piece's folder (with no name, in the projects folder); the app is stopped
# until you type exit, which starts it again
```

By hand, `adb shell run-as com.warptempo.gui` opens one in the app's data folder, with the app running; the projects are under `/sdcard/Android/data/com.warptempo.gui/files/projects`.

The tablet commits the marker files: in the app, a long press on SAVE in the main window, or SAVE in the history view (HISTORY; its tooltip: Save and Commit), saves the piece and sends it to GitHub under the title `Update <piece>`, with nothing asked (the main window's SAVE is dimmed on a piece with nothing unsaved, so such a piece is committed from the history view); to undo a checkpoint, load the one before it in place (HISTORY, then LOAD) and commit again. A plain press on SAVE in the main window is the plain save. When GitHub has newer checkpoints the same press pulls them instead (in the history view SAVE then reads PULL), and while the device is offline it asks GitHub again (in the history view SAVE then reads CHECK). When the pull changes the open piece it asks `Pull GitHub's newer checkpoint and reload this piece?`: OK pulls and reopens the piece as GitHub has it, its undo history and any unsaved edits gone; Cancel leaves everything as it was. A pull that does not change the open piece runs without asking.

The tablet's pull overwrites whatever it changes: a file GitHub's newer checkpoints change becomes GitHub's even when the tablet holds saved edits to it, or a file of its own at that name. If a pull fails partway (`Pull failed: the files were updated but the branch did not move`), pull again.

The laptop never commits: it takes the tablet's work with plain git when you want it, and may stay behind GitHub as long as you like. The CLI writes only into `render/`, which the clone ignores, so the pull always fast-forwards:

```bash
projects="$WARPTEMPO_PROJECTS_PATH"
git -C "$projects/.." pull --ff-only
# "Fast-forward" and the files it changed, or "Already up to date."
./build/warptempo_cli "$projects/<piece>/<its source file name>.wav"
```

The tablet is the one device that authors: if it breaks, the unfinished movements wait for the replacement. With the same screen resolution, continue where GitHub left off; with a different one, restart the unfinished pieces.

## Trouble

`adb devices` is empty with the cable in: the tablet is locked (A new tablet, step 4), Auto Blocker came back on, or USB debugging is off. Try another cable or port. `no permissions` in the list: install `android-udev` (`sudo pacman -S android-udev`).

`more than one device/emulator`: the cable and wireless are both connected and `ANDROID_SERIAL` is not set. Set it, unplug the cable, or drop wireless with `adb disconnect`.

Wireless stopped answering: the tablet rebooted. Plug in the cable and redo Wireless adb.

Screenshots are black, or taps do nothing: the cover is closed, which turns off the screen and the touch panel. Open it.

`INSTALL_FAILED_UPDATE_INCOMPATIBLE`, or signatures do not match: the APK was signed with another keystore. Restore it (The signing key) and rebuild. Do not uninstall the app: that deletes everything it holds on the tablet, the commits GitHub has not got and the renders included.

The app closes at launch:

```bash
adb logcat -d -s warptempo:I | tail -20
# the last lines name the reason: usually the config file, or a piece folder missing a file
```

`GitHub: refused` on the tablet: its deploy key is missing. Setup replaces the tablet's history and backs nothing up, so first copy the tablet's files home, in case it holds commits GitHub never got:

```bash
adb pull /sdcard/Android/data/com.warptempo.gui/files ~/tablet_files_copy
# then run scripts/warptempo_sync setup again, reading its plan before you answer y
```

The laptop has no deploy key (`Permission denied (publickey)` from git in the clone):

```bash
ssh-keygen -t ed25519 -N '' -C warptempo-laptop -f ~/.ssh/warptempo_projects_deploy_key
cat ~/.ssh/warptempo_projects_deploy_key.pub
# copy the printed line. In a browser: the projects repository on GitHub > Settings >
# Deploy keys > Add deploy key: title "laptop", paste it, tick "Allow write access", Add key.
# Delete the old "laptop" key there.
```

The laptop's clone diverged (`git status -sb` says `ahead` and `behind` at once, and the pull refuses): the laptop and GitHub have both moved, which only a commit on the laptop can cause. The laptop holds nothing authored, so drop its local commits:

```bash
projects="$WARPTEMPO_PROJECTS_PATH"
git -C "$projects/.." reset --hard origin/main
# HEAD is now at … : the laptop is GitHub's again. On the tablet, diverged means its
# clone is re-placed by scripts/warptempo_sync setup (copy its files home first, above)
```

Moving the projects to another GitHub repository: point each clone's origin at it (`git remote set-url origin ssh://git@ssh.github.com:443/<owner>/<repository>.git`), give it the devices' deploy keys, and change `projects_repo` in the app's Settings list to match on the tablet. The program pins GitHub's host keys, so the projects stay on GitHub.

A config or `.settings` carrying a retired key refuses to start or to load: Migrating, below.

## Migrating an older project folder

A folder from before the project model (a bare `.warpmarkers` / `.phaseresetmarkers` pair, perhaps still in the legacy `MM:SS.mmm` timestamps, with a `renders/` folder or a stray output wav in its root) comes into the current layout like this:

1. Make a folder for the project directly under `projects_path`, named for the project, and copy the source wav into it.
2. Copy the `.warpmarkers` and `.phaseresetmarkers` in beside it.
3. If those files still hold timestamps, build the conversion tool (not part of the product build; `tools/` is its source) and convert the warp file first, then the phase-reset file, which needs the finished warp map:
   ```bash
   cmake -B build-tools -S tools && cmake --build build-tools
   build-tools/migrate_sidecar_to_frames <sample_rate> <stem>.warpmarkers
   build-tools/migrate_sidecar_to_frames <sample_rate> <stem>.phaseresetmarkers <stem>.wav
   ```
   `<sample_rate>` is the source's own (`44100`, for instance); each file is rewritten in place and the original kept as `<path>.bak`.
4. Do not bring the old `.settings` across. Move the two converted files aside, open the project with the wav alone (the first open writes all three templates), quit, and copy the two back over their templates. Re-enter `title`, `notes`, `url` and `cover` by hand.
5. Rename `renders/` to `tmp/`, and move the current `title`'s `<title>.wav` and `.fingerprint` from the root into `render/` (anything else there is deleted at the next publish).

An unknown key is fatal, with no migration: the fix is deleting the line by hand, with the program not running. A `.settings` still carrying `score_path`, `score_first_page`, `score_last_page`, `tab_a_score_position` or `tab_b_score_position` (the score's keys for a few hours of 2026-10-26, in the piece's `score/viewer.txt` since that night) refuses to load the same way: delete those five lines. A `.settings` still carrying `gui_scale`, `audio_player`, `projects_repo`, `playback_speed`, `follow`, `centered`, `center_on_next_marker`, `waveform_magnification_level`, `font_size`, `libm_hash`, `libmvec_hash`, `fftw3_hash` or `fftw3_threads_hash` refuses to load. A device config carrying any of these stops the program at startup: `audio_player=`, `sync_path=`, `hold_delay_ms=`; any `waveform_gain_…=` or `waveform_expander_…=`; `waveform_magnified_ink=`, `waveform_ghost_ink=`; `waveform_magnified_gain=`, `waveform_ghost_reduction=`, `waveform_magnified_gain_db=`, `waveform_ghost_gain_db=`, `waveform_magnification_foreground_db=`, `waveform_magnification_background_db=`; `waveform_compressor_threshold_db=`, `waveform_compressor_ratio=`, `waveform_foreground_gain_db=`; `fg_color=`, `bg_color=`, `fg_border_color=`, `bg_border_color=`, `fg_blend_loud=`, `fg_blend_quiet=`, `waveform_widening=`; `pen_plane_distance=`, `pen_plane_enter=`, `pen_plane_exit=`; `palette_passes=`, `waveform_passes=`; `cue_triangle_rows=`, `cue_triangle_aa=`, `cue_text=` and `cue_lead=`, the cue tuning keys of 2026-10-26, settled the same day at 8 rows authored at 300 %, the staircase, 22 and a gap of 8, and `cue_shadow=`, settled that morning at on; `led_glow=`, the lit LED's glow tuning key of 2026-10-23, settled 2026-10-26 at 4; `rain_fade_in_ms=` and `rain_fade_out_ms=`, the background sound's two fade tuning keys of 2026-10-28, settled the same day at 200 and 40 ms; `rain_level=` and `rain_crossfade_ms=`, renamed `noise_level=` and `noise_crossfade_ms=` 2026-10-28 (rename the two lines, values kept, with the program not running; `scripts/warptempo_sync setup` deletes them from the tablet's config and adds the new names at their first values); `noise_crossfade_ms=`, replaced 2026-10-28 night by `noise_crossfade_s=`, the crossfade in whole seconds (write `noise_crossfade_s=4` in its place, with the program not running; `scripts/warptempo_sync setup` does it on the tablet). A config still carrying `theme_level=` or any of the twelve colour keys (`waveform_ink=`, `waveform_canvas=`, `waveform_outline=`, `flag_face=`, `flag_face_selected=`, `flag_label=`, `flag_label_selected=`, `invalid_face=`, `invalid_face_selected=`, `invalid_label=`, `playhead_head=`, `playhead_stem=`; 2026-10-03 to 2026-10-04, when every colour moved into the theme files) stops it too: delete those lines (`scripts/warptempo_sync setup` deletes them from the tablet's config). A `theme=` line naming a theme file (a key from 2026-10-03 to 2026-10-08, when the bundled theme files retired and each chrome's colors were built into the program) stops it on its value: `theme=` is a key again since 2026-11-05, naming one of Ableton Live 12's themes (The config), so set it to `default`. The themes folder an earlier build left beside the config (2026-10-03..10-08, color files) is read again since 2026-11-06 as the folder of Live's theme files: delete any old files in it. A config still carrying the line of the waveform's height cap (a key from 2026-09-13 to 2026-10-07; the waveform now takes all the height the other rows leave it) stops it too, as an unknown key naming the line: delete that line. A config missing one of its nineteen lines stops it the same way, every line being required (`tooltip_delay_ms=` joined them 2026-11-07, a setting under tuning: add `tooltip_delay_ms=100`, which `scripts/warptempo_sync setup` adds to the tablet's config where it is missing; `run_gap=` joined them earlier that day, a setting under tuning: add `run_gap=22`, which `scripts/warptempo_sync setup` adds to the tablet's config where it is missing; `key_width=` joined them 2026-11-06, a setting under tuning: add `key_width=35`, which `scripts/warptempo_sync setup` adds to the tablet's config where it is missing; `lamp_hold_ms=` joined them 2026-11-05 night, a setting under tuning: add `lamp_hold_ms=100`; `noise_attenuation_level=` joined them the same night: add `noise_attenuation_level=-9`; `theme=` joined them 2026-11-05: add `theme=default` — each of the three `scripts/warptempo_sync setup` adds to the tablet's config where it is missing; `waveform_grow=` joined them 2026-10-31: add `waveform_grow=1`, which `scripts/warptempo_sync setup` adds the same way; `osk_font_px=` joined them 2026-10-29 night, a setting under tuning: add `osk_font_px=12`, which `scripts/warptempo_sync setup` adds to the tablet's config where it is missing; `osk_key_w=` and `osk_key_h=` joined them 2026-10-29 evening, settings under tuning: add `osk_key_w=24` and `osk_key_h=30`, which `scripts/warptempo_sync setup` adds to the tablet's config where they are missing; `key_height=` joined them 2026-10-29, a setting under tuning: add `key_height=17`, which `scripts/warptempo_sync setup` adds to the tablet's config where it is missing; the background sound's three, `noise=`, `noise_level=` and `noise_crossfade_s=`, joined them 2026-10-28, the last two first named `rain_level=` and `rain_crossfade_ms=` and the crossfade `noise_crossfade_ms=` until that night: add `noise=rain`, `noise_level=-inf` and `noise_crossfade_s=4`, which `scripts/warptempo_sync setup` adds to the tablet's config where they are missing; `true_colors=` joined them 2026-10-21: add `true_colors=on`; `strip_pad=`, required 2026-10-22 to 2026-10-23, `bevel_light=` and `bevel_dark=`, required 2026-10-21 to 2026-10-23, and `text_shadow=`, required 2026-10-21 to 2026-10-22, are removed and stop it as unknown keys: delete the lines); `role=`, required 2026-10-10 to 2026-10-30, is removed and stops it as an unknown key: delete the line (`scripts/warptempo_sync setup` deletes it from the tablet's config). The keys `waveform=` (the waveform color preset, required 2026-10-14 to 2026-11-01, removed with the presets: the program has one palette), `waveform_column_px=` (the waveform's column width, a tuning key required for part of 2026-11-01, removed 2026-11-01 night: one device pixel at every scale), `tooltip_gap=` (the space between a tooltip and its button, a tuning key required for part of 2026-11-07, removed the same day: 1 W, built in), `chrome=` (its name until 2026-10-21), `caption=` (removed 2026-10-21 with the title bar's color schemes), `scheme=` and `palette=` (renamed `caption=` and `chrome=` 2026-10-13), `icons=` and `font=` (removed 2026-10-12) stop the program too, whatever they say: delete them (`scripts/warptempo_sync setup` deletes the tablet's `waveform=`, `waveform_column_px=`, `tooltip_gap=`, `chrome=` and `caption=`).
