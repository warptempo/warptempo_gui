#!/usr/bin/env bash
# snapshot_device_defaults.sh — the tablet's device config, snapshotted into
# the repository's tracked assets/device_config_defaults (architect
# 2026-11-08, session 95: "there should be a pipeline in place that snapshots
# it each time we do an arc … so my settings are durable and git-historied";
# src/gui/device_config.h's THE FIRST-RUN VALUES owns the rule).
#
#     bash tools/snapshot_device_defaults.sh <adb serial>
#
# WHEN IT RUNS: the planner's road at every arc's last commit, before the
# commit, so a changed file rides that arc's commit (the APK packs the file as
# the app's first-run values, and the sync script's setup adds its lines where
# a tablet's config lacks a key). THE SERIAL IS THE FIRST ARGUMENT, required:
# no address is spelled in the repository.
#
# WHAT IT DOES: pulls the app's config off the tablet (run-as, the debuggable
# build's road), checks that its key column is EXACTLY the tracked file's —
# the same keys in the same order (the writer's, kDeviceConfigKeys) — and
# writes the tracked file only when the pulled bytes differ, printing
# "unchanged" otherwise. A key mismatch (a key added or retired in the arc,
# whose tracked line the planner adds or removes by hand first, or a reading
# that came back wrong) prints both key lists and exits 1, writing nothing.
# The values are taken as the app wrote them: the app is their one judge, and
# a file it cannot read back fails its launch out loud.
#
# A tools/ script: no product target links or runs it.

set -euo pipefail

if [[ $# -ne 1 || -z "$1" ]]; then
    printf 'usage: bash tools/snapshot_device_defaults.sh <adb serial>\n' >&2
    exit 2
fi
serial="$1"

file="$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")/../assets/device_config_defaults"
[[ -f "$file" ]] || { printf 'snapshot_device_defaults: no tracked file at %s\n' "$file" >&2; exit 1; }

# The config as the app wrote it; command substitution drops the file's last
# newline, put back at the write.
pulled="$(adb -s "$serial" exec-out run-as com.warptempo.gui cat files/warptempo_gui/config)" || {
    printf 'snapshot_device_defaults: could not read the tablet'"'"'s config over adb (%s)\n' "$serial" >&2
    exit 1
}
tracked="$(<"$file")"

pulled_keys="$(sed 's/=.*//' <<< "$pulled")"
tracked_keys="$(sed 's/=.*//' <<< "$tracked")"
if [[ "$pulled_keys" != "$tracked_keys" ]]; then
    printf 'snapshot_device_defaults: the tablet'"'"'s keys differ from the tracked file'"'"'s; nothing written\n' >&2
    printf -- '--- the tablet'"'"'s keys:\n%s\n' "$pulled_keys" >&2
    printf -- '--- the tracked file'"'"'s keys (%s):\n%s\n' "$file" "$tracked_keys" >&2
    exit 1
fi

if [[ "$pulled" == "$tracked" ]]; then
    printf 'unchanged\n'
    exit 0
fi
printf '%s\n' "$pulled" > "$file"
printf 'written: %s\n' "$file"
diff <(printf '%s\n' "$tracked") "$file" || true
