#!/usr/bin/env bash
# Fetch + verify + install the MINIMAL SDK pieces: build-tools (aapt2, zipalign,
# apksigner, d8) and the android-$WT_PLATFORM_SDK platform (android.jar, needed
# by aapt2 link -I -- the INSTALLED platform, which is not the manifest's
# targetSdkVersion and does not have to match it; 00_env.sh states both). No
# sdkmanager, no licence dance, no system-wide state.
# Idempotent.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/00_env.sh"

install_zip() {  # <zip> <sha1> <final-dir>
    local zip="$1" sha="$2" final="$3" unpack top
    if [ -d "$final" ]; then wt_say "already installed: $final"; return 0; fi
    wt_fetch "$WT_SDK_REPO_BASE/$zip" "$WT_CACHE/$zip" sha1 "$sha"
    unpack="$(dirname "$final")/.unpack.$$"
    rm -rf "$unpack"; mkdir -p "$unpack"
    wt_say "unpacking $zip"
    unzip -q "$WT_CACHE/$zip" -d "$unpack"
    # Google's zips unpack into one dir named after the codename/api, e.g.
    # "android-16" for build-tools r36 and "android-15" for platform-35.
    top="$(find "$unpack" -mindepth 1 -maxdepth 1 -type d)"
    [ -d "$top" ] || wt_die "unexpected archive layout in $zip"
    mkdir -p "$(dirname "$final")"
    mv "$top" "$final"
    rmdir "$unpack"
    chmod -R u+rwX "$final"
}

install_zip "$WT_BUILD_TOOLS_ZIP" "$WT_BUILD_TOOLS_SHA1" "$WT_BUILD_TOOLS"
install_zip "$WT_PLATFORM_ZIP"    "$WT_PLATFORM_SHA1"    "$WT_SDK/platforms/android-$WT_PLATFORM_SDK"

for t in aapt2 zipalign apksigner d8; do
    [ -e "$WT_BUILD_TOOLS/$t" ] || wt_die "missing $t in $WT_BUILD_TOOLS"
    chmod +x "$WT_BUILD_TOOLS/$t" 2>/dev/null || true
done
[ -f "$WT_ANDROID_JAR" ] || wt_die "missing $WT_ANDROID_JAR"

wt_say "build-tools $WT_BUILD_TOOLS_VERSION at $WT_BUILD_TOOLS"
wt_say "android.jar (API $WT_PLATFORM_SDK) at $WT_ANDROID_JAR"
"$WT_BUILD_TOOLS/aapt2" version || true
