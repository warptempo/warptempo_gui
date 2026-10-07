#!/usr/bin/env bash
# tools/mockup/push.sh — sheets onto the tablet's /sdcard/Download, where Samsung Gallery shows a PNG byte for byte
# when it carries the Display-P3 iCCP chunk and nothing else. The planner runs it; the coder never does.
#   push.sh <png...>             each file in NAME ORDER: pushed, touched there (Gallery orders by the file's time),
#                                announced to the media scanner, then two seconds before the next, so the sheets
#                                land in Gallery in their names' order
#   push.sh --delete <name...>   each superseded sheet removed from /sdcard/Download (a base name, no path) and the
#                                media scanner told, so Gallery drops it
set -euo pipefail
DEVICE=192.168.1.87:5555
DEST=/sdcard/Download
SCAN=android.intent.action.MEDIA_SCANNER_SCAN_FILE

usage() { echo "usage: $0 <png...> | $0 --delete <name...>" >&2; exit 2; }
[ $# -ge 1 ] || usage

if [ "$1" = "--delete" ]; then
    shift
    [ $# -ge 1 ] || usage
    for name in "$@"; do
        case "$name" in */*|'') echo "push.sh: --delete takes base names, not '$name'" >&2; exit 2;; esac
        adb -s "$DEVICE" shell rm -f "$DEST/$name"
        adb -s "$DEVICE" shell am broadcast -a "$SCAN" -d "file://$DEST/$name" >/dev/null
        echo "deleted $DEST/$name"
    done
    exit 0
fi

mapfile -t files < <(printf '%s\n' "$@" | awk -F/ '{print $NF "\t" $0}' | sort | cut -f2-)
for f in "${files[@]}"; do
    [ -f "$f" ] || { echo "push.sh: no file '$f'" >&2; exit 1; }
done
for f in "${files[@]}"; do
    name=$(basename "$f")
    adb -s "$DEVICE" push "$f" "$DEST/$name" >/dev/null
    adb -s "$DEVICE" shell touch "$DEST/$name"
    adb -s "$DEVICE" shell am broadcast -a "$SCAN" -d "file://$DEST/$name" >/dev/null
    echo "pushed $DEST/$name"
    sleep 2
done
