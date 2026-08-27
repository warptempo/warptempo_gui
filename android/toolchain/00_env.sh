#!/usr/bin/env bash
# Shared environment for every android/ script. Source it, never execute it:
#     . android/toolchain/00_env.sh
#
# Host installs live OUTSIDE the repo, under $WT_ANDROID_ROOT (default
# ~/.local/android). The only in-repo output is android/prebuilt/<abi>.
#
# One knob decides the API level of the whole sysroot: WT_API. The deps and the
# app are built at the SAME level deliberately (see android/NOTES.md, "API
# levels"), so changing it here and re-running android/deps/build_all.sh is the
# whole edit.

set -u

# --- repo + install roots -------------------------------------------------
WT_ANDROID_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
WT_REPO_ROOT="$(cd -- "$WT_ANDROID_DIR/.." && pwd)"
export WT_ANDROID_DIR WT_REPO_ROOT

export WT_ANDROID_ROOT="${WT_ANDROID_ROOT:-$HOME/.local/android}"
export WT_CACHE="$WT_ANDROID_ROOT/cache"          # downloaded archives
export WT_WORK="$WT_ANDROID_ROOT/work"            # unpacked sources + build trees

# --- versions (exact, pinned) --------------------------------------------
# NDK r29 is the newest STABLE release: the repository manifest's r30 entries
# are android-ndk-r30-beta{1,2} despite a "stable" channel ref. r28+ is the
# hard floor (16 KB page alignment by default); r29 is what the research doc
# and termux-packages both use.
export WT_NDK_VERSION="29.0.14206865"
export WT_NDK_RELEASE="r29"
export WT_NDK_ZIP="android-ndk-r29-linux.zip"
export WT_NDK_SHA1="87e2bb7e9be5d6a1c6cdf5ec40dd4e0c6d07c30b"

export WT_BUILD_TOOLS_VERSION="36.0.0"
export WT_BUILD_TOOLS_ZIP="build-tools_r36_linux.zip"
export WT_BUILD_TOOLS_SHA1="b0b6376977657e8ad9b969bacf4093601da2c6fb"

# TWO SEPARATE NUMBERS, stated only here: the manifest's targetSdkVersion, and
# the INSTALLED platform we compile and link against. They do not have to match
# -- runtime behaviour gates on the target aapt2 stamps, not on the jar.
#
# targetSdk 34 (architect 2026-08-27), NOT 35: from Android 15 on, an app whose
# target is 35 or higher is laid out EDGE-TO-EDGE by the platform whatever it
# asks for, and Window#setDecorFitsSystemWindows(true) does not step out of it
# -- measured on the device that evening with the sliver's explicit call in
# place, the log still said `window 2304x1440`, so the taskbar sat over the
# bottom row whose INPUT it owns (android/NOTES.md 11.7). The documented opt-out
# at 35 is the windowOptOutEdgeToEdgeEnforcement THEME attribute, which needs a
# res/values style and an aapt2 compile step this APK does not have (it ships no
# res/ at all); stepping the target back to 34 is the same result with no build
# machinery at all. NOT 36 either, which was 35's own reason: Android 16 revokes
# screenOrientation on screens >=600dp and this activity is landscape-locked.
# The spike's build script reads this same variable.
export WT_TARGET_SDK="34"

# The installed platform: android.jar for javac/d8 and for aapt2's -I. Keeping
# it at 35 while the target is 34 is what lets the target move with no second
# SDK download -- android-35 is the only platform under $WT_SDK/platforms.
export WT_PLATFORM_SDK="35"
export WT_PLATFORM_ZIP="platform-35_r02.zip"
export WT_PLATFORM_SHA1="0bb560a90a7a2cbd0dd8348224d518b638fe7949"

export WT_JDK_VERSION="21.0.12.1+1"
export WT_JDK_TARBALL="OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz"
export WT_JDK_URL="https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.12.1%2B1/OpenJDK21U-jdk_x64_linux_hotspot_21.0.12.1_1.tar.gz"
export WT_JDK_SHA256="ce79869e1307ed8ee1e2baa86a412b1eb5b75d10a01006d788a6f968bcfaee94"

export WT_SDK_REPO_BASE="https://dl.google.com/android/repository"

# --- installed locations --------------------------------------------------
export WT_NDK="$WT_ANDROID_ROOT/ndk/android-ndk-$WT_NDK_RELEASE"
export WT_SDK="$WT_ANDROID_ROOT/sdk"
export WT_BUILD_TOOLS="$WT_SDK/build-tools/$WT_BUILD_TOOLS_VERSION"
export WT_ANDROID_JAR="$WT_SDK/platforms/android-$WT_PLATFORM_SDK/android.jar"
export WT_JDK="$WT_ANDROID_ROOT/jdk"

# --- target ---------------------------------------------------------------
export WT_ABI="arm64-v8a"
export WT_TARGET="aarch64-linux-android"
# minSdk == the API the deps are compiled against. 30 = Android 11: the only
# target device is a Tab S10 FE on Android 15/16, and 30 makes AAudio (26/28),
# AChoreographer_postFrameCallback64 (29) and ANativeWindow_setFrameRate (30)
# unconditional -- no dlsym/compat branch anywhere. Install-time floor is 24.
export WT_API="30"

export WT_TOOLCHAIN="$WT_NDK/toolchains/llvm/prebuilt/linux-x86_64"
export WT_TCBIN="$WT_TOOLCHAIN/bin"
export WT_SYSROOT="$WT_TOOLCHAIN/sysroot"

# --- staging prefix (the one in-repo output) ------------------------------
export WT_PREFIX="$WT_ANDROID_DIR/prebuilt/$WT_ABI"
export WT_CROSS_FILE="$WT_ANDROID_ROOT/meson/android-aarch64.ini"
export WT_PKGCONFIG_WRAPPER="$WT_ANDROID_ROOT/bin/pkg-config-android"

# --- cross toolchain binaries --------------------------------------------
export CC="$WT_TCBIN/${WT_TARGET}${WT_API}-clang"
export CXX="$WT_TCBIN/${WT_TARGET}${WT_API}-clang++"
export AR="$WT_TCBIN/llvm-ar"
export RANLIB="$WT_TCBIN/llvm-ranlib"
export STRIP="$WT_TCBIN/llvm-strip"
export NM="$WT_TCBIN/llvm-nm"
export LD="$WT_TCBIN/ld.lld"
export OBJDUMP="$WT_TCBIN/llvm-objdump"
export READELF="$WT_TCBIN/llvm-readelf"

# -ffp-contract=off is OURS, not termux's: it is what keeps renders
# byte-reproducible across rebuilds, and it survives cross-compilation.
export WT_OPT_FLAGS="-O3 -fstack-protector-strong -ffp-contract=off"
export CFLAGS="$WT_OPT_FLAGS"
export CXXFLAGS="$WT_OPT_FLAGS"
export LDFLAGS="-L$WT_PREFIX/lib -Wl,--as-needed -Wl,-z,relro,-z,now -Wl,--enable-new-dtags"

# pkg-config must see the staging prefix ONLY. LIBDIR (not PATH) is the
# variable that REPLACES the default search dirs; the wrapper script written by
# 40_gen_cross_file.sh re-forces it so a stray host .pc can never leak in.
export PKG_CONFIG_LIBDIR="$WT_PREFIX/lib/pkgconfig:$WT_PREFIX/share/pkgconfig"
export PKG_CONFIG_SYSROOT_DIR=""
unset PKG_CONFIG_PATH PKG_CONFIG_DIR 2>/dev/null || true

export PATH="$WT_JDK/bin:$WT_TCBIN:$PATH"

# --- helpers used by the numbered scripts --------------------------------
wt_say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
wt_warn() { printf '\033[1;33m==> WARN:\033[0m %s\n' "$*" >&2; }
wt_die()  { printf '\033[1;31m==> ERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# wt_fetch <url> <dest> <algo> <sum>   -- idempotent, checksum-verified
wt_fetch() {
    local url="$1" dest="$2" algo="$3" sum="$4" got
    mkdir -p "$(dirname "$dest")"
    if [ -f "$dest" ]; then
        got="$("${algo}sum" "$dest" | cut -d' ' -f1)"
        if [ "$got" = "$sum" ]; then wt_say "cached, checksum ok: $(basename "$dest")"; return 0; fi
        wt_warn "checksum mismatch on cached $(basename "$dest") -- refetching"
        rm -f "$dest"
    fi
    wt_say "fetching $(basename "$dest")"
    curl -fL --retry 3 --retry-delay 5 -C - -o "$dest.part" "$url" || wt_die "download failed: $url"
    mv "$dest.part" "$dest"
    got="$("${algo}sum" "$dest" | cut -d' ' -f1)"
    [ "$got" = "$sum" ] || wt_die "checksum mismatch for $dest: expected $sum, got $got"
    wt_say "checksum ok: $(basename "$dest")"
}

mkdir -p "$WT_CACHE" "$WT_WORK" "$WT_PREFIX"
