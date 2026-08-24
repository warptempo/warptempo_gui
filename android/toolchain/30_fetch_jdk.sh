#!/usr/bin/env bash
# apksigner, d8 and keytool are Java programs. This host has NO system JDK
# (checked: no java/javac/keytool on PATH), so install a private Temurin JDK
# under $WT_ANDROID_ROOT/jdk rather than touching the system. If a system JDK
# ever appears, delete $WT_ANDROID_ROOT/jdk and this script no-ops around it.
# Idempotent.
set -euo pipefail
. "$(dirname -- "${BASH_SOURCE[0]}")/00_env.sh"

if [ -x "$WT_JDK/bin/keytool" ]; then
    wt_say "private JDK already installed: $("$WT_JDK/bin/java" -version 2>&1 | head -1)"
    exit 0
fi

if command -v keytool >/dev/null 2>&1 && command -v java >/dev/null 2>&1; then
    wt_say "system JDK present ($(java -version 2>&1 | head -1)) -- not installing a private one"
    exit 0
fi

wt_fetch "$WT_JDK_URL" "$WT_CACHE/$WT_JDK_TARBALL" sha256 "$WT_JDK_SHA256"

rm -rf "$WT_JDK.unpack"; mkdir -p "$WT_JDK.unpack"
wt_say "unpacking $WT_JDK_TARBALL"
tar -xzf "$WT_CACHE/$WT_JDK_TARBALL" -C "$WT_JDK.unpack"
top="$(find "$WT_JDK.unpack" -mindepth 1 -maxdepth 1 -type d)"
[ -d "$top" ] || wt_die "unexpected JDK archive layout"
rm -rf "$WT_JDK"; mv "$top" "$WT_JDK"; rmdir "$WT_JDK.unpack"

"$WT_JDK/bin/java" -version
wt_say "JDK installed: $WT_JDK"
