# Installing and keeping warptempo_gui

This file is how warptempo_gui is installed, built, run for the first time and kept running on its two devices. It is a copy-paste recipe: paste one block at a time into a terminal on the laptop, read what it prints (the `#` lines say what to expect), and only then go on to the next. Blocks run from the root of this repository unless they say otherwise. How to use the program is not here: its tooltips (the tooltip lamp, bare `\`) and [`HELP.md`](HELP.md) carry that.

The two devices are an Arch Linux laptop (labwc, Wayland, JACK), where the program is built and tested, and one Android tablet (a Galaxy Tab S10 FE), which runs the same program as an APK built on the laptop. The pieces' marker files live in their own public repository, [warptempo_projects](https://github.com/warptempo/warptempo_projects), and each device keeps its own clone of it. The program itself commits, pushes and pulls those files (the history view `h`, then `Ctrl+S`). The tablet authors and commits; the laptop tests and never commits, pulling the tablet's work when it wants it. What git does not carry, the source audio in and the renders out, travels by `scripts/warptempo_sync` (Daily use).

Every value that belongs to one person's setup is an environment variable. Export these in your shell profile (`~/.bashrc` or the like) and open a new terminal:

| Variable | What it holds |
|---|---|
| `WARPTEMPO_TABLET_ADDR` | the tablet's wireless adb address, host:port (e.g. `192.168.0.20:5555`); read by `scripts/warptempo_sync` |
| `WARPTEMPO_TABLET_DEPLOY_KEY` | path of the tablet's GitHub deploy key; its `.pub` lies beside it; read by `scripts/warptempo_sync` |
| `WARPTEMPO_KEYSTORE_BACKUP` | path of your backup copy of `~/.android/debug.keystore` |
| `WARPTEMPO_SIGNING_CERT_SHA256` | the SHA-256 fingerprint of that keystore's certificate, as keytool prints it |
| `ANDROID_SERIAL` | the tablet's USB serial (adb's own variable) |

adb obeys `ANDROID_SERIAL` on every command, so with it exported a plain `adb …` talks to the tablet over the cable, even when the wireless link is up too. Over wireless alone, name the device: `adb -s "$WARPTEMPO_TABLET_ADDR" …`. Until you know the serial (a new tablet, below), leave it unset.

## The laptop

### Packages

The program needs a C++23 compiler (GCC 12+ or Clang 16+), CMake 3.20+, pkg-config, fftw3, and for the GUI wayland-scanner, cairo (with cairo-ft), HarfBuzz, wayland-client, wayland-cursor, wayland-protocols, libxkbcommon, JACK and libgit2. JACK can be jackd2 or PipeWire's JACK. The headless `warptempo_cli` needs only the first four. libgit2 can be left out with `-DWARPTEMPO_GUI_GIT=OFF` for a build that only paints (the git road then reports no repository); the two devices build with it on. Where the distribution's compiler is older than GCC 12 (Ubuntu 22.04 ships GCC 11), install `g++-12` or later and configure with `-DCMAKE_CXX_COMPILER=g++-12` added.

```bash
# Arch (the host it is developed on):
sudo pacman -S --needed base-devel cmake pkgconf git fftw cairo harfbuzz \
    wayland wayland-protocols libxkbcommon pipewire-jack libgit2

# Debian / Ubuntu (libwayland-dev carries wayland-scanner):
sudo apt install build-essential cmake pkg-config git libfftw3-dev \
    libcairo2-dev libharfbuzz-dev libwayland-dev wayland-protocols \
    libxkbcommon-dev libjack-jackd2-dev libgit2-dev

# Fedora:
sudo dnf install gcc gcc-c++ cmake pkgconf-pkg-config git fftw-devel \
    cairo-devel harfbuzz-devel wayland-devel wayland-protocols-devel \
    libxkbcommon-devel pipewire-jack-audio-connection-kit-devel libgit2-devel
```

The tablet side adds adb and the Android build's host tools; its two fonts, Roboto and Roboto Mono, are the repository's own (`fonts/`), which both the laptop's binary and the APK carry, so no font package is installed. On Arch:

```bash
sudo pacman -S --needed android-tools meson ninja zip unzip
pacman -Q git libgit2 android-tools cmake meson ninja fftw cairo harfbuzz wayland-protocols libxkbcommon pipewire-jack zip unzip
# every line shows a version; "was not found" names a package still to install
```

The GUI needs a Wayland compositor that offers server-side decorations (xdg-decoration); it exits at startup without one. labwc, the other wlroots compositors (sway, Hyprland) and KDE Plasma offer it; GNOME does not. There is no X11 backend. Under WSL2 the CLI builds and runs from the Debian / Ubuntu packages; the GUI does not (WSLg routes audio through PulseAudio and the program has no non-JACK path). macOS is not supported.

### Build

```bash
cmake -B build -S . -DWARPTEMPO_BUILD_CLI=ON
cmake --build build -j$(nproc)
# the GUI is build/warptempo_gui, the CLI build/warptempo_cli
```

Leave `CMAKE_BUILD_TYPE` unset: the flags `-O3 -march=native -ffp-contract=off` are always on, and asserts stay live. A debug build goes in its own folder (`cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug`), never over `build/`. `-march=native` makes the binary fit this CPU alone, so build on the machine that runs it; `-ffp-contract=off` is what keeps a rebuilt binary rendering byte for byte what the last one did. What can still move the bytes, by a least significant bit here and there, is an upgrade of glibc or FFTW underneath: after one, re-render anything you mean to compare against. The CLI is opt-in; on a headless host with none of the GUI's packages, configure with `-DWARPTEMPO_BUILD_GUI=OFF -DWARPTEMPO_BUILD_CLI=ON`.

There is no `make install`. The binary is self-contained; to put it on `$PATH` and register it with the application launcher (the `.desktop` names the project's own icon, `warptempo_gui`, installed into the hicolor theme beside it):

```bash
install -Dm755 build/warptempo_gui ~/.local/bin/warptempo_gui
install -Dm644 packaging/warptempo_gui.desktop ~/.local/share/applications/warptempo_gui.desktop
install -Dm644 packaging/warptempo_gui.svg ~/.local/share/icons/hicolor/scalable/apps/warptempo_gui.svg
update-desktop-database ~/.local/share/applications
```

### First run

The program always has a project open. With no argument it opens the one it had open last (the config's `last_project`), else the first valid project folder under `projects_path` in name order; with none at all it prints `No project under <projects_path>` and exits. With one argument, that argument must be a project's source `.wav`, inside its project folder directly under `projects_path`; anything else refuses with the reason on stderr.

```bash
./build/warptempo_gui
./build/warptempo_gui "path/to/project/source.wav"
./build/warptempo_cli "path/to/project/source.wav"
# the CLI opens no window: it renders the project's <title>.wav into its render/ folder,
# the active tab's trim applied, byte for byte what the GUI's render writes
```

A project is a folder directly under `projects_path`, named by its folder. It either carries the three sidecars `<stem>.warpmarkers`, `<stem>.phaseresetmarkers` and `<stem>.settings` beside `<stem>.wav`, or carries none and holds exactly one `.wav`, which makes it a new project whose first open writes the three. A folder with some sidecars must have all three; any other shape refuses to open with the reason stated. The sidecars are program-written and strictly validated: a hand edit that breaks one fails the load, with the first error on stderr. A name that is `.` or `..`, begins or ends with whitespace, or holds a line break is not a project. The source is stereo WAV, 16- or 24-bit PCM, 44100 Hz or above; convert anything else once with an external tool (ffmpeg). The program writes into two folders beside the source: `render/` holds the deliverable, `<title>.wav` and its `.fingerprint`, and nothing else (other pairs there are deleted at the next publish); `tmp/` holds the batch renders the render player walks.

The per-device config is `$XDG_CONFIG_HOME/warptempo_gui/config` (`~/.config/warptempo_gui/config` when that is unset): exactly six lines, in this order.

| Key | What it is |
|---|---|
| `gui_scale=` | the interface's scale, an integer percent in [50, 350]: device pixels per Windows 95 pixel (100 is Windows' own size; the laptop runs 138, the tablet 275) |
| `max_waveform_height=` | the waveform's cap in unscaled (Windows 95) pixels, [0, 9999]; `0` removes it |
| `projects_repo=` | the projects repository the history view commits to |
| `projects_path=` | the absolute folder whose subfolders are the projects |
| `last_project=` | the folder name opened last, written by the program at every open |
| `theme=` | the theme every colour is painted in: `windows-95-standard` (the one built in, the default) or the key of a theme file (the bundled ones are listed, with a picture of each, in `docs/themes/CATALOG.md`) |

A theme file is `<key>.theme` in the `themes/` folder beside the config (`~/.config/warptempo_gui/themes/` on the laptop, `files/warptempo_gui/themes/` in the tablet app's private folder), read once at launch; the key is lowercase letters and digits joined by single hyphens. Each line is `role=value` (no blank lines, no comments), the value `#rrggbb` or one of Windows' twenty always-solid colours by name (`black`, `maroon`, `green`, `olive`, `navy`, `purple`, `teal`, `silver`, `gray`, `red`, `lime`, `yellow`, `blue`, `fuchsia`, `aqua`, `white`, `moneygreen`, `skyblue`, `cream`, `medgray`); a file may name only some roles, and every other role keeps the built-in's colour (the roles and the built-in's values are the role table in `src/gui/theme_file.h`). A file that breaks the grammar stops the program at startup, naming the file and the line.

Every theme but the built-in ships with the program as a theme file: the catalog's imported themes, `warptempo` and the colour picker's presets (`warptempo-preset-<n>`), generated into the repository's `assets/themes/` (`tools/theme_catalog/gen_theme_files.py`). EVERY LAUNCH COPIES THEM INTO `themes/` before reading it, on the laptop from the repository's `assets/themes/` (the folder of the repository it was built from, read as it stands at each launch), on the tablet from the APK (a regenerated set reaches the tablet with the next APK). The bundled files win for their own names: a bundled file edited by hand is overwritten at the next launch. Your own themes take other names (copy a bundled file to a new name and edit that); a file of any other name is never touched. A launch that cannot copy them (the repository moved after the build, a full disk) stops at startup, naming what failed.

The first run writes it: on the laptop `gui_scale=138`, `max_waveform_height=364`, `projects_repo=github.com/warptempo/warptempo_projects`, `projects_path=$HOME/.warptempo/warptempo_projects/projects` (spelled out as an absolute path), `last_project=` blank, then `theme=windows-95-standard`. That `projects_path` is the example layout this file assumes: the projects clone at `~/.warptempo/warptempo_projects`, beside this repository. The clone is wherever `projects_path` says, one folder up. Change every key but `last_project` in the app (the Settings menu); the theme repaints at once, among the built-in and the theme files read at launch. A hand edit is for when the program is not running (it rewrites the whole file at every commit), and a line that breaks the grammar stops the program at startup, naming the line.

On a new laptop, make the projects clone where `projects_path` points. It is public, so the clone needs no key; the program's own fetches then go over SSH on port 443 with the laptop's deploy key (Trouble, "the laptop has no deploy key"):

```bash
git clone https://github.com/warptempo/warptempo_projects.git ~/.warptempo/warptempo_projects
git -C ~/.warptempo/warptempo_projects remote set-url origin ssh://git@ssh.github.com:443/warptempo/warptempo_projects.git
git -C ~/.warptempo/warptempo_projects config core.sshCommand "ssh -F /dev/null -i ~/.config/warptempo_gui/deploy_key -o IdentitiesOnly=yes -o UserKnownHostsFile=~/.ssh/known_hosts"
# the last line lets plain git in the clone use the deploy key too
```

The audio is not in the repository: put each piece's source `.wav` into its folder under `projects/`, named `<stem>.wav` after the stem its sidecars share. `main` is the only branch; the program refuses any other.

### Restoring a laptop

After an OS reinstall, install the packages (above), then restore these from your backup before anything else:

- `~/.android/debug.keystore` (the key that signs the APK) and `~/.android/adbkey` (the tablet already trusts it)
- `~/.config/warptempo_gui/config`, `deploy_key` and `deploy_key.pub`
- the tablet's deploy key pair at `$WARPTEMPO_TABLET_DEPLOY_KEY`, and the keystore copy at `$WARPTEMPO_KEYSTORE_BACKUP`
- this repository, with `android/prebuilt/` if you keep it (else rebuild it: The tablet)
- the projects clone, with its audio and renders

```bash
ls -l ~/.android/debug.keystore ~/.android/adbkey ~/.config/warptempo_gui/deploy_key "$WARPTEMPO_TABLET_DEPLOY_KEY"
cat ~/.config/warptempo_gui/config
# six lines, the six keys above in that order. Any other line (sync_path, or any
# retired key: Migrating) stops the program at startup: delete it with the program not running.
```

```bash
projects="$(sed -n 's/^projects_path=//p' ~/.config/warptempo_gui/config)"
git -C "$projects/.." fetch
git -C "$projects/.." status -sb
# first line:  ## main...origin/main
# "[ahead N]"  = commits here GitHub has not got
# "[behind N]" = GitHub has newer; the program's h, then Ctrl+S (it reads Pull), takes them
# "Are you sure you want to continue connecting": check the fingerprint is one of GitHub's,
#   SHA256:+DiY3wvvV6TuJJhbpZisF/zLDA0zPMSvHdkr4UvCOqU (ed25519)
#   SHA256:p2QAMXNIC1TJYWeIOttrVc98/R1BUFWu3/LiyKgUfQM (ECDSA)
#   SHA256:uNiVztksCsDhcc0u9e8BujQXVUpKZIDTMczCvj3tD2s (RSA)
#   then type yes. Anything else: type no and stop.
# "Permission denied (publickey)" = the deploy key is missing (Trouble)
```

Then build (above) and run it: the program opens the last piece. Press `h`; after a moment the bottom row ends with `GitHub: up to date` (or `behind`: the laptop may stay behind GitHub). `GitHub: refused` is the deploy key (Trouble); `GitHub: offline` is the network. `h` again leaves the view, `Ctrl+Q` quits. With the cable in, `adb devices` shows the tablet's serial, then `device`: the restored `adbkey` means the tablet still trusts the laptop.

## The tablet

### The Android toolchain

```bash
bash android/toolchain/bootstrap.sh
# no sudo; into ~/.local/android. About 1.1 GB downloaded (NDK, build tools, android.jar,
# a JDK), about 3 GB on disk, every download checked against a pinned checksum. Safe to run
# again: it skips what is there. The last lines list
#   NDK 29.0.14206865, build-tools 36.0.0, android.jar API 35
```

### The prebuilt libraries

The APK links fftw, freetype, harfbuzz, pixman, cairo and git (OpenSSL 3.6.4's libcrypto under libssh2 1.11.1 under libgit2 1.9.7) statically from `android/prebuilt/`, which is not in the repository.

```bash
ls android/prebuilt/arm64-v8a/lib/libgit2.a
# present: skip the next line. Missing: build them all (about 12 minutes)
bash android/deps/build_all.sh
```

### The signing key

The tablet accepts an update only when it is signed by the same key as the installed app. That key is `~/.android/debug.keystore`.

```bash
~/.local/android/jdk/bin/keytool -list -v -keystore ~/.android/debug.keystore -storepass android | grep -c "$WARPTEMPO_SIGNING_CERT_SHA256"
# expect:  1
# 0, or "Keystore file does not exist": restore it, then run the line above again
mkdir -p ~/.android && cp "$WARPTEMPO_KEYSTORE_BACKUP" ~/.android/debug.keystore
```

Never let a build make a new one: an APK signed by another key will not install over the app, and uninstalling the app deletes everything it holds on the tablet. `build_apk.sh` refuses to mint a key when none is there. Only for a brand-new app identity (a first install, nothing on the tablet to keep) run it once as `WT_NEW_KEYSTORE=1 bash android/app/build_apk.sh`, then copy the new keystore to `$WARPTEMPO_KEYSTORE_BACKUP` and record its fingerprint, the `SHA256:` line of the keytool command above without the `grep`, in `WARPTEMPO_SIGNING_CERT_SHA256`. (`WT_KEYSTORE` points the build at a keystore elsewhere.)

### Build the APK

```bash
bash android/app/build_apk.sh
# five VERIFY steps, then near the end "APK: …/android/app/build-android/warptempo.apk"
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
adb install -r android/app/build-android/warptempo.apk
# expect:  Success
# Play Protect or "unknown app": More details > Install anyway
```

### Put the projects on it

`scripts/warptempo_sync setup` makes the tablet ready in one command. It shows its plan (every file it would delete or copy over, the settings file it would write, the audio) and asks `Proceed? [y/N]`; any answer but `y` changes nothing, and nothing is backed up. It starts the app once if it never ran (with no piece yet, it closes), places a fresh clone of GitHub's projects on the tablet, gives the tablet its deploy key from `$WARPTEMPO_TABLET_DEPLOY_KEY`, corrects the tablet's config, copies the audio and starts the app. If you have no tablet key yet, make one as the laptop's (Trouble, "the laptop has no deploy key") with `-f "$WARPTEMPO_TABLET_DEPLOY_KEY"`, titled `tablet` on GitHub, with write access.

```bash
scripts/warptempo_sync setup -n
# the plan alone, and nothing on the tablet changes
scripts/warptempo_sync setup
# the same plan, then "Proceed? [y/N]": read it, then type y. The last line starts "done:"
```

Then check. On the tablet, tap Toggle History View in the icon row (hold the S Pen just above an icon to read its name), or press `h` on a keyboard; after a moment the bottom row ends with `GitHub: up to date`. From the laptop:

```bash
adb logcat -d -s warptempo:I | grep -E 'working_column|GitHub'
# a line with working_column= (a piece opened), and none with "GitHub refused" or "GitHub offline"
adb shell run-as com.warptempo.gui cat /data/user/0/com.warptempo.gui/files/warptempo_gui/config
# six lines:
#   gui_scale=275                 (the tablet's first-run value; change it in the app)
#   max_waveform_height=364
#   projects_repo=github.com/warptempo/warptempo_projects
#   projects_path=/storage/emulated/0/Android/data/com.warptempo.gui/files/projects
#   last_project=<the piece open>
#   theme=windows-95-standard     (the built-in; or a theme file's key)
# /storage/emulated/0 and /sdcard are one folder; the tablet's clone is its files/ folder.
```

Change the tablet's config in the app (the Settings menu), not by editing the file: a bad line stops the app. The config and the deploy key live in the app's private folder, `/data/user/0/com.warptempo.gui/files/warptempo_gui`, reachable only through `adb shell run-as com.warptempo.gui` (the APK is a debuggable build).

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
bash android/app/build_apk.sh
adb install -r android/app/build-android/warptempo.apk
adb shell am start -n com.warptempo.gui/.MainActivity
adb logcat -s warptempo:I
# the app's log, live; Ctrl+C stops it. Over wireless, add -s "$WARPTEMPO_TABLET_ADDR" after adb.
```

## Daily use

`scripts/warptempo_sync` reads the laptop's projects from the device config's `projects_path` and finds the tablet on the cable first, then at `$WARPTEMPO_TABLET_ADDR`. `tt` and `ft` take piece names (folder names under `projects_path`) to narrow a run; with none they take every piece. `-n` rehearses `tt`, `ft` or `setup`, saying in words what it would do and doing none of it; `-v` adds the commands. It runs only from a terminal; with no verb it prints its usage.

Audio in, for a new piece (its folder under the laptop's `projects_path`, holding its one `.wav`):

```bash
scripts/warptempo_sync tt
# copies every piece's source wav the tablet lacks; the app is neither stopped nor started.
# Then on the tablet: File > Open Project.
```

By hand, what it does for one piece:

```bash
piece="<the piece's folder name>"
wav="<its source file name>.wav"
projects="$(sed -n 's/^projects_path=//p' ~/.config/warptempo_gui/config)"
far=/sdcard/Android/data/com.warptempo.gui/files/projects
adb push "$projects/$piece/$wav" "$far/$piece/$wav"
adb shell "chmod 777 '$far/$piece'"
# a folder adb makes belongs to adb's shell user with mode 770, which the app cannot even
# list; the chmod opens it. Then on the tablet: File > Open Project.
```

Renders out:

```bash
scripts/warptempo_sync ft
# brings every render newer than the laptop's into its piece's render/ folder
```

A render is a wav and its `.fingerprint` together, and the fingerprint names the wav by its size and its date to the nanosecond, so a hand copy that rounds the date makes a pair the program rejects; there is no hand road.

A shell on the tablet as the app:

```bash
scripts/warptempo_sync ot "<piece>"
# in that piece's folder (with no name, in the projects folder); the app is stopped
# until you type exit, which starts it again
```

By hand, `adb shell run-as com.warptempo.gui` opens one in the app's data folder, with the app running; the projects are under `/sdcard/Android/data/com.warptempo.gui/files/projects`.

The tablet commits the marker files: in the app, `h`, then `Ctrl+S` on a keyboard or the Save button (its tooltip: Save and Commit) sends them to GitHub. The laptop is for testing and never commits; it may stay behind GitHub for as long as you like. To bring the tablet's work to the laptop, open a piece, press `h`, and when the bottom row says `GitHub: behind`, press `Ctrl+S` (it reads Pull). When the pull changes the open piece it asks `Reload this piece from GitHub's newer checkpoint?`: Reload takes GitHub's.

A pull refuses while the laptop holds saved test edits. To throw every laptop test edit away, quit the program, then:

```bash
projects="$(sed -n 's/^projects_path=//p' ~/.config/warptempo_gui/config)"
git -C "$projects/.." restore projects
```

If the pull still refuses and names a piece the laptop opened before the tablet first committed it, the laptop wrote that piece's three marker files itself and `restore` does not touch them: open that piece on the laptop and pull from it (answer Reload), or delete its three marker files by hand.

One device per movement: a movement is authored on one device, so if the tablet breaks, that movement waits for the replacement. With the same screen resolution, continue where GitHub left off; with a different one, restart the unfinished pieces.

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

The laptop has no deploy key (`GitHub: refused` on the laptop):

```bash
ssh-keygen -t ed25519 -N '' -C warptempo-laptop -f ~/.config/warptempo_gui/deploy_key
cat ~/.config/warptempo_gui/deploy_key.pub
# copy the printed line. In a browser: the projects repository on GitHub > Settings >
# Deploy keys > Add deploy key: title "laptop", paste it, tick "Allow write access", Add key.
# Delete the old "laptop" key there.
```

`GitHub: diverged` on the laptop: the laptop and GitHub have both moved, which only a laptop commit can cause. The laptop holds nothing authored, so quit the program and drop its local commits:

```bash
projects="$(sed -n 's/^projects_path=//p' ~/.config/warptempo_gui/config)"
git -C "$projects/.." reset --hard origin/main
# HEAD is now at … : the laptop is GitHub's again. On the tablet, diverged means its
# clone is re-placed by scripts/warptempo_sync setup (copy its files home first, above)
```

Moving the projects to another GitHub repository: point each clone's origin at it (`git remote set-url origin ssh://git@ssh.github.com:443/<owner>/<repository>.git`), give it the devices' deploy keys, and change Projects Repository in the Settings menu to match, on each device. The program pins GitHub's host keys, so the projects stay on GitHub.

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

An unknown key is fatal, with no migration: the fix is deleting the line by hand, with the program not running. A `.settings` still carrying `gui_scale`, `audio_player`, `projects_repo`, `playback_speed`, `follow`, `centered`, `center_on_next_marker`, `waveform_magnification_level`, `font_size`, `libm_hash`, `libmvec_hash`, `fftw3_hash` or `fftw3_threads_hash` refuses to load. A device config carrying any of these stops the program at startup: `audio_player=`, `sync_path=`, `hold_delay_ms=`; any `waveform_gain_…=` or `waveform_expander_…=`; `waveform_magnified_ink=`, `waveform_ghost_ink=`; `waveform_magnified_gain=`, `waveform_ghost_reduction=`, `waveform_magnified_gain_db=`, `waveform_ghost_gain_db=`, `waveform_magnification_foreground_db=`, `waveform_magnification_background_db=`; `waveform_compressor_threshold_db=`, `waveform_compressor_ratio=`, `waveform_foreground_gain_db=`; `fg_color=`, `bg_color=`, `fg_border_color=`, `bg_border_color=`, `fg_blend_loud=`, `fg_blend_quiet=`, `waveform_widening=`; `pen_plane_distance=`, `pen_plane_enter=`, `pen_plane_exit=`; `palette_passes=`, `waveform_passes=`. A config still carrying `theme_level=` or any of the twelve colour keys (`waveform_ink=`, `waveform_canvas=`, `waveform_outline=`, `flag_face=`, `flag_face_selected=`, `flag_label=`, `flag_label_selected=`, `invalid_face=`, `invalid_face_selected=`, `invalid_label=`, `playhead_head=`, `playhead_stem=`; 2026-10-03 to 2026-10-04, when every colour moved into the theme files) stops it too: delete those lines (`scripts/warptempo_sync setup` deletes them from the tablet's config); its `theme=` stays good, every catalog key shipping as a bundled theme file. A config missing one of its six lines stops it the same way; for a missing `max_waveform_height=`, add `max_waveform_height=364`, and for a missing `theme=`, add `theme=windows-95-standard`.
