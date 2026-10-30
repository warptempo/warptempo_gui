#!/usr/bin/env bash
# Fetch + verify + install the NDK under $WT_ANDROID_ROOT/ndk. Idempotent.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/00_env.sh"

if [ -x "$WT_TCBIN/${WT_TARGET}${WT_API}-clang" ]; then
    wt_say "NDK $WT_NDK_RELEASE already installed at $WT_NDK"
    exit 0
fi

wt_fetch "$WT_SDK_REPO_BASE/$WT_NDK_ZIP" "$WT_CACHE/$WT_NDK_ZIP" sha1 "$WT_NDK_SHA1"

mkdir -p "$WT_ANDROID_ROOT/ndk"
rm -rf "$WT_ANDROID_ROOT/ndk/.unpack"
mkdir -p "$WT_ANDROID_ROOT/ndk/.unpack"
wt_say "unpacking $WT_NDK_ZIP (this takes a minute)"
unzip -q "$WT_CACHE/$WT_NDK_ZIP" -d "$WT_ANDROID_ROOT/ndk/.unpack"

top="$(find "$WT_ANDROID_ROOT/ndk/.unpack" -mindepth 1 -maxdepth 1 -type d)"
[ -d "$top" ] || wt_die "unexpected NDK archive layout"
rm -rf "$WT_NDK"
mv "$top" "$WT_NDK"
rmdir "$WT_ANDROID_ROOT/ndk/.unpack"

# The zip does not carry the exec bit on every wrapper on some hosts.
chmod -R u+rwX "$WT_NDK"
chmod +x "$WT_TCBIN"/* 2>/dev/null || true

[ -x "$WT_TCBIN/${WT_TARGET}${WT_API}-clang" ] \
    || wt_die "no ${WT_TARGET}${WT_API}-clang in the NDK -- API $WT_API below the NDK floor?"

wt_say "NDK installed: $WT_NDK"
"$WT_TCBIN/clang" --version | head -2
