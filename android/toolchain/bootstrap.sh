#!/usr/bin/env bash
# Whole host-side bootstrap, in order. Safe to re-run at any time.
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
bash "$here/10_fetch_ndk.sh"
bash "$here/20_fetch_build_tools.sh"
bash "$here/30_fetch_jdk.sh"
bash "$here/40_gen_cross_file.sh"
. "$here/00_env.sh"
wt_say "bootstrap complete"
printf '  NDK          %s  (%s)\n' "$WT_NDK_VERSION" "$WT_NDK"
printf '  build-tools  %s  (%s)\n' "$WT_BUILD_TOOLS_VERSION" "$WT_BUILD_TOOLS"
printf '  android.jar  API %s  (%s)\n' "$WT_TARGET_SDK" "$WT_ANDROID_JAR"
printf '  cross file   %s\n' "$WT_CROSS_FILE"
printf '  prefix       %s\n' "$WT_PREFIX"
printf '  target       %s API %s (%s)\n' "$WT_TARGET" "$WT_API" "$WT_ABI"
