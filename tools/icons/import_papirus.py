#!/usr/bin/env python3
"""THE PAPIRUS IMPORTER (architect 2026-11-17, session 140: his set is
Papirus; 2026-11-21, send GG: the one set, src/gui/icons.h's head).

A standalone tool, stdlib only, with no link path from any product target. Its
ONE TABLE, ROSTER below, maps each `icons::Icon` enumerator (src/gui/icons.h)
to its papirus-icon-theme source drawing, a path under the checkout's
`Papirus/16x16/` (the 16-px symbolic set, `symbolic/...-symbolic.svg`, or,
where the architect's pick lives there, the 16-px `actions/`, `devices/` and
`panel/` trees) and, on a row that names one, A TRANSFORM. For each row it
writes assets/icons/<Enumerator>.svg by THE RECOLOR RULE
(assets/icons/README.md):

  - every literal color in a `fill`, `stroke`, `stop-color`, `color`,
    `flood-color` or `lighting-color` attribute or `style` declaration becomes
    `currentColor`; `none`, `currentColor`, `inherit` and `url(...)` stay, and
    every opacity stays;
  - the color-scheme `<style>` block (and with it an emptied `<defs>`) and
    every `class` attribute are dropped;
  - the path data and the drawing's own viewBox are kept verbatim (no seat).

THE TRANSFORM (architect 2026-11-22, send II: one source turned to serve a
second act): a row's fourth field, 'mirror' (left-right) or 'cw' (90 degrees
clockwise), wraps every child of the root <svg> in ONE
`<g transform="...">` about the center of the drawing's box (its viewBox,
else its width and height): mirror `matrix(-1 0 0 1 2cx 0)`, cw
`rotate(90 cx cy)`. The path data inside stays verbatim; resvg applies the
group at the parse. A row with no fourth field is written untransformed.

A written file (the transform's group included) that would leave a literal
color, a class, a style block, or a shape painting resvg's default black (no
fill on it or an ancestor) fails the run, naming it. EVERY ROW IS IMPORTED, the two cards included: Papirus's
dialog-error / dialog-information already cut their mark out of the disc (the
mark's subpaths wound against the disc's), so one color shows it and no hand
transcription is needed.

IT WRITES ONLY THE ROWS' FILES and removes nothing: the folder's other files
-- the licence text and README.md -- are never opened, so a rerun leaves
them as they are.
A row dropped from ROSTER leaves its old file behind, to be deleted by hand
with its enumerator.

Then it prints the README's provenance table: the enumerator, the source file
(and the file a source symlink resolves to), the sha256 of the source bytes,
and a note on a pick whose name is not the enumerator's (the parked run's
flags, session 123), on a re-pick, on two rows sharing one drawing (the
written path data and transform, so a mirrored row is not its source's
twin), or on two rows sharing one source file turned into two drawings.
Re-pick a drawing by editing ROSTER and rerunning against the checkout
(PAPIRUS_SOURCE below):

    python3 -I tools/icons/import_papirus.py <papirus-icon-theme checkout>

THE CHECKOUT IS DATA: nothing in it is run; the tool reads its SVG files and
nothing else.
"""

import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET

# THE SOURCE the table names: papirus-icon-theme 20260801 (its installed
# package's icon root, /usr/share/icons, serves as the checkout: it holds
# Papirus/16x16/). The rows kept from the import at commit 33d971eb are
# byte-identical in it, source and written file alike; the re-picks of
# 2026-11-22 are its own.
PAPIRUS_SOURCE = 'papirus-icon-theme 20260801'
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, 'assets', 'icons')

# ENUMERATOR -> (source under Papirus/16x16/, note[, transform]), in icons.h's
# enum order. THE TABLE OF SESSION 123 (the parked importer's picks, the
# architect's sheets of session 140) WITH TWO ROWS RE-PICKED BY HIM
# 2026-11-17, session 140 ("the first alternative for each one"): TabDetach
# and GoParentFolder (the 16-px actions tree, no symbolic twin); AND
# TWENTY-ONE ROWS RE-PICKED BY HIM
# 2026-11-22, send II, off the alternates sheet: BboxPrev / BboxNext (one
# source, BboxPrev mirrored), ViewMediaLyrics, ViewMediaTrack,
# TransformMoveVertical (turned clockwise), GoJump, TimelineUseZoneOn,
# GoJumpDeclaration, Mathmode, ViewMediaVisualization, ZoomInY, Speedometer,
# ZoomPrevious, KeyframePrevious, KeyframeNext, DeepHistory, ZoneIn, ZoneOut,
# DocumentSave, BlackSum and Merge, most from the 16-px actions, devices and
# panel trees. A note names a pick that is not the enumerator's own name and
# the date of a re-pick.
SYM = 'symbolic/'
ROSTER = [
    ('DocumentOpen', SYM + 'actions/document-open-symbolic.svg', ''),
    ('DocumentSave', 'devices/media-floppy.svg',
     'media-floppy, a floppy disk, his pick over document-save '
     '(re-picked 2026-11-22)'),
    ('DocumentSaveAs', SYM + 'actions/document-save-as-symbolic.svg', ''),
    ('VcsPull', SYM + 'actions/arrow-pointing-at-line-down-symbolic.svg',
     'no vcs-pull; an arrow down onto a line'),
    ('NetworkConnect', SYM + 'status/network-transmit-receive-symbolic.svg',
     'no network-connect; the two-way network arrows'),
    ('VcsCommit', SYM + 'actions/arrow-pointing-away-from-line-up-symbolic.svg',
     'no vcs-commit; an arrow up from a line'),
    ('EditUndo', SYM + 'actions/edit-undo-symbolic.svg', ''),
    ('EditRedo', SYM + 'actions/edit-redo-symbolic.svg', ''),
    ('MediaRecord', SYM + 'actions/media-record-symbolic.svg', ''),
    ('DialogCancel', SYM + 'actions/cross-small-circle-outline-symbolic.svg',
     'no dialog-cancel; a circled cross'),
    ('EditCopy', SYM + 'actions/edit-copy-symbolic.svg', ''),
    ('EditPaste', SYM + 'actions/edit-paste-symbolic.svg', ''),
    ('ZoomIn', SYM + 'actions/zoom-in-symbolic.svg', ''),
    ('ZoomOut', SYM + 'actions/zoom-out-symbolic.svg', ''),
    ('ZoomFitBest', SYM + 'actions/zoom-fit-best-symbolic.svg', ''),
    ('ZoomPrevious', 'actions/view-restore.svg',
     'no zoom-previous; view-restore, four corners turned in '
     '(re-picked 2026-11-22)'),
    ('ZoomInY', 'actions/audio-volume-high.svg',
     'no zoom-in-y; audio-volume-high, a speaker and its waves '
     '(re-picked 2026-11-22)'),
    ('GoJump', 'actions/footsteps.svg',
     'footsteps, his pick over go-jump (re-picked 2026-11-22)'),
    ('TimelineUseZoneOn', 'actions/measure.svg',
     'no timeline-use-zone-on; measure, a ruler over two arrows '
     '(re-picked 2026-11-22)'),
    ('EditCut', SYM + 'actions/edit-cut-symbolic.svg', ''),
    ('Bookmarks', SYM + 'actions/bookmarks-symbolic.svg', ''),
    ('MusicNote16th', SYM + 'actions/music-note-single-symbolic.svg',
     'no 16th note; the single note'),
    ('Mathmode', 'actions/insert-math-expression.svg',
     'no mathmode; insert-math-expression, an f of x '
     '(re-picked 2026-11-22)'),
    ('Merge', 'actions/merge.svg',
     'two blocks joined (re-picked 2026-11-22)'),
    ('AudioHeadphones', SYM + 'devices/audio-headphones-symbolic.svg', ''),
    ('Lock', SYM + 'actions/lock-symbolic.svg', ''),
    ('SettingsConfigure', SYM + 'actions/settings-symbolic.svg',
     'no settings-configure; settings'),
    ('HelpWhatsthis', SYM + 'actions/help-about-symbolic.svg',
     'no help-whatsthis; help-about'),
    ('ViewMediaLyrics', SYM + 'places/folder-music-symbolic.svg',
     'no view-media-lyrics; folder-music, two beamed notes '
     '(re-picked 2026-11-22)'),
    ('ShallowHistory', SYM + 'actions/recent-symbolic.svg',
     'no shallow-history; recent (a clock)'),
    ('DeepHistory', 'panel/network-wireless-offline.svg',
     'no deep-history; network-wireless-offline, a faded fan and a cross '
     '(re-picked 2026-11-22)'),
    ('BlackSum', 'actions/office-chart-line-forecast.svg',
     'no black_sum; office-chart-line-forecast, a line run on dashed '
     '(re-picked 2026-11-22)'),
    ('DocumentRevert', SYM + 'actions/document-revert-symbolic.svg', ''),
    ('KeyframePrevious', 'actions/keyframe-previous.svg',
     'a stopwatch and a triangle back (re-picked 2026-11-22)'),
    ('KeyframeNext', 'actions/keyframe-next.svg',
     'a stopwatch and a triangle forward (re-picked 2026-11-22)'),
    ('DialogOkApply', SYM + 'actions/feather-check-symbolic.svg',
     'no dialog-ok-apply; a check'),
    ('ViewMediaTrack', 'devices/media-optical.svg',
     'no view-media-track; media-optical, a disc (re-picked 2026-11-22)'),
    ('ViewMediaVisualization', 'actions/kdenlive-show-audiothumb.svg',
     'no view-media-visualization; kdenlive-show-audiothumb, an arrow '
     'through chevrons (re-picked 2026-11-22)'),
    ('Speedometer', 'actions/slow_off.svg',
     'slow_off, a gauge, his pick over speedometer (re-picked 2026-11-22)'),
    ('ChronometerStart', SYM + 'status/stopwatch-symbolic.svg',
     'no chronometer-start; the stopwatch'),
    ('MediaPlaybackStart', SYM + 'actions/media-playback-start-symbolic.svg', ''),
    ('MediaPlaybackStop', SYM + 'actions/media-playback-stop-symbolic.svg', ''),
    ('MediaPlaybackPause', SYM + 'actions/media-playback-pause-symbolic.svg', ''),
    ('MediaSkipBackward', SYM + 'actions/media-skip-backward-symbolic.svg', ''),
    ('MediaSkipForward', SYM + 'actions/media-skip-forward-symbolic.svg', ''),
    ('MediaSeekBackward', SYM + 'actions/media-seek-backward-symbolic.svg', ''),
    ('MediaSeekForward', SYM + 'actions/media-seek-forward-symbolic.svg', ''),
    ('MediaRepeatSingle', SYM + 'actions/playlist-repeat-song-symbolic.svg',
     'no media-repeat-single; playlist-repeat-song'),
    ('ListAdd', SYM + 'actions/list-add-symbolic.svg', ''),
    ('ListRemove', SYM + 'actions/list-remove-symbolic.svg', ''),
    ('EditDelete', SYM + 'actions/edit-delete-symbolic.svg', ''),
    ('ViewHidden', SYM + 'actions/view-conceal-symbolic.svg',
     'no view-hidden; view-conceal'),
    ('InsertLink', SYM + 'actions/insert-link-symbolic.svg', ''),
    ('Link', SYM + 'actions/link-symbolic.svg', ''),
    ('GoJumpDeclaration', 'actions/go-jump-declaration.svg',
     'a flag and a turning arrow (re-picked 2026-11-22)'),
    ('DocumentEdit', SYM + 'actions/document-edit-symbolic.svg', ''),
    ('EditSelect', SYM + 'actions/edit-select-symbolic.svg', ''),
    ('TransformMoveVertical', 'actions/transform-move-horizontal.svg',
     'no transform-move-vertical; transform-move-horizontal turned 90 '
     'degrees clockwise, arrows up and down (re-picked 2026-11-22)', 'cw'),
    ('GoDown', SYM + 'actions/go-down-symbolic.svg', ''),
    ('GoUp', SYM + 'actions/go-up-symbolic.svg', ''),
    ('BboxPrev', 'actions/debug-execute-to-cursor.svg',
     'no bboxprev; debug-execute-to-cursor, an arrow into a bar, mirrored '
     'left-right to point left (re-picked 2026-11-22)', 'mirror'),
    ('BboxNext', 'actions/debug-execute-to-cursor.svg',
     'no bboxnext; debug-execute-to-cursor, an arrow into a bar, as drawn, '
     'pointing right (re-picked 2026-11-22)'),
    ('TabDetach', SYM + 'actions/focus-windows-symbolic.svg',
     'no tab-detach; focus-windows, two windows (re-picked 2026-11-17)'),
    ('ZoomOriginal', SYM + 'actions/zoom-original-symbolic.svg', ''),
    ('GoPrevious', SYM + 'actions/go-previous-symbolic.svg', ''),
    ('GoNext', SYM + 'actions/go-next-symbolic.svg', ''),
    ('GoParentFolder', 'actions/go-parent-folder.svg',
     'the 16-px actions tree, no symbolic twin (re-picked 2026-11-17)'),
    ('GoPreviousViewPage', SYM + 'actions/lucide-chevron-left-symbolic.svg',
     'no go-previous-view-page; a chevron of its own'),
    ('GoNextViewPage', SYM + 'actions/lucide-chevron-right-symbolic.svg',
     'no go-next-view-page; a chevron of its own'),
    ('EditReset', SYM + 'actions/object-rotate-left-symbolic.svg',
     'no edit-reset; the counter-clockwise arrow'),
    ('ZoneIn', 'actions/view-readermode-active.svg',
     'view-readermode-active, an open book and a dot, his pick over zone-in '
     '(re-picked 2026-11-22)'),
    ('ZoneOut', 'actions/view-readermode.svg',
     'view-readermode, an open book, his pick over zone-out '
     '(re-picked 2026-11-22)'),
    ('WindowClose', SYM + 'actions/window-close-symbolic.svg', ''),
    ('EditClear', SYM + 'actions/edit-clear-symbolic.svg', ''),
    ('ViewRefresh', SYM + 'actions/view-refresh-symbolic.svg', ''),
    ('Folder', SYM + 'places/folder-symbolic.svg', ''),
    ('DialogInformation', SYM + 'status/dialog-information-symbolic.svg', ''),
    ('DialogError', SYM + 'status/dialog-error-symbolic.svg', ''),
    ('Battery000', SYM + 'status/battery-000-symbolic.svg', ''),
    ('Battery000Charging', SYM + 'status/battery-000-charging-symbolic.svg', ''),
    ('FormatIndentMore', SYM + 'actions/format-indent-more-symbolic.svg', ''),
    ('KeyEnter', SYM + 'actions/keyboard-enter-symbolic.svg',
     'no key-enter; keyboard-enter'),
]

PAINT_PROPS = ('fill', 'stroke', 'stop-color', 'color', 'flood-color',
               'lighting-color')
KEEP_VALUES = ('none', 'currentcolor', 'inherit')
SHAPES = ('path', 'rect', 'circle', 'ellipse', 'polygon', 'polyline', 'line')
SVG_NS = '{http://www.w3.org/2000/svg}'


def is_literal(value):
    v = value.strip().lower()
    return not (v in KEEP_VALUES or v.startswith('url('))


def recolor_style(style):
    out = []
    for decl in style.split(';'):
        if ':' in decl:
            prop, value = decl.split(':', 1)
            if prop.strip().lower() in PAINT_PROPS and is_literal(value):
                decl = prop + ':currentColor'
        out.append(decl)
    return ';'.join(out)


def recolor(text):
    text = re.sub(r'<style\b[^>]*>.*?</style>\s*', '', text, flags=re.S)
    text = re.sub(r'<style\b[^>]*/>\s*', '', text)
    text = re.sub(r'<defs\b[^>]*>\s*</defs>\s*', '', text)
    text = re.sub(r'<defs\b[^>]*/>\s*', '', text)
    text = re.sub(r'\s+class="[^"]*"', '', text)

    def attr(m):
        name, value = m.group(1), m.group(2)
        if name == 'style':
            return 'style="%s"' % recolor_style(value)
        if is_literal(value):
            return '%s="currentColor"' % name
        return m.group(0)
    names = '|'.join(re.escape(p) for p in PAINT_PROPS + ('style',))
    return re.sub(r'(?<![\w-])(%s)="([^"]*)"' % names, attr, text)


def paint_of(el, prop):
    """The element's own `prop` (attribute or style declaration), or None."""
    for decl in el.get('style', '').split(';'):
        if ':' in decl:
            p, v = decl.split(':', 1)
            if p.strip() == prop:
                return v.strip()
    return el.get(prop)


def check(name, text):
    if re.search(r'#[0-9a-fA-F]{3,8}\b', text):
        sys.exit('%s: a literal color is left' % name)
    if '<style' in text or 'class=' in text:
        sys.exit('%s: a style block or a class is left' % name)
    root = ET.fromstring(text)
    parents = {c: p for p in root.iter() for c in p}
    for el in root.iter():
        if el.tag.replace(SVG_NS, '') not in SHAPES:
            continue
        node, fill = el, None
        while node is not None and fill is None:
            fill = paint_of(node, 'fill')
            node = parents.get(node)
        if fill is None:
            sys.exit('%s: a shape paints the default black' % name)


def num(v):
    return '%g' % v


def transform(name, text, kind):
    """THE TRANSFORM (the module's head): every child of the root <svg>
    wrapped in one group turned about the drawing's box's center."""
    if not kind:
        return text
    root = ET.fromstring(text)
    box = root.get('viewBox')
    if box:
        x, y, w, h = (float(v) for v in box.replace(',', ' ').split())
    else:
        x, y = 0.0, 0.0
        w, h = float(root.get('width')), float(root.get('height'))
    cx, cy = x + w / 2, y + h / 2
    if kind == 'mirror':
        matrix = 'matrix(-1 0 0 1 %s 0)' % num(2 * cx)
    elif kind == 'cw':
        matrix = 'rotate(90 %s %s)' % (num(cx), num(cy))
    else:
        sys.exit('%s: no transform %r' % (name, kind))
    head = re.search(r'<svg\b[^>]*>', text)
    tail = text.rindex('</svg>')
    return (text[:head.end()] + '\n <g transform="%s">' % matrix
            + text[head.end():tail] + ' </g>\n' + text[tail:])


def drawing_digest(text):
    """The written drawing, its path data and transforms with whitespace
    dropped: two rows with one digest are one drawing whatever their names,
    and a transformed row is not its source's twin."""
    data = ''.join(re.findall(r'\s(?:d|transform)="([^"]*)"', text))
    return hashlib.sha256(re.sub(r'\s+', '', data).encode()).hexdigest()


def main():
    if len(sys.argv) != 2:
        sys.exit('usage: python3 -I tools/icons/import_papirus.py '
                 '<papirus-icon-theme checkout>')
    root = os.path.join(os.path.abspath(sys.argv[1]), 'Papirus', '16x16')
    if not os.path.isdir(root):
        sys.exit('%s: not a papirus-icon-theme checkout (no Papirus/16x16)'
                 % sys.argv[1])
    os.makedirs(OUT, exist_ok=True)
    rows = []
    seen = {}
    sources = {}
    for row in ROSTER:
        enum, rel, note = row[:3]
        kind = row[3] if len(row) > 3 else ''
        src = os.path.join(root, rel)
        real = os.path.realpath(src)
        if not os.path.isfile(real):
            sys.exit('%s: no source %s' % (enum, rel))
        with open(real, 'rb') as f:
            data = f.read()
        text = transform(enum, recolor(data.decode('utf-8')), kind)
        check(enum, text)
        with open(os.path.join(OUT, enum + '.svg'), 'w', encoding='utf-8') as f:
            f.write(text)
        digest = drawing_digest(text)
        if digest in seen:
            note = '; '.join(x for x in (note, 'one drawing with ' + seen[digest]) if x)
        else:
            seen[digest] = enum
            if real in sources:
                note = '; '.join(x for x in (
                    note, 'one source with %s, another drawing by the '
                    'transform' % sources[real]) if x)
        sources.setdefault(real, enum)
        shown = rel
        if real != os.path.abspath(src):
            shown += ' (-> %s)' % os.path.relpath(real, root)
        rows.append((enum, shown, hashlib.sha256(data).hexdigest(), note))
    print('| Enumerator | Source (Papirus/16x16/, %s) '
          '| sha256 of the source | Note |' % PAPIRUS_SOURCE)
    print('|---|---|---|---|')
    for enum, shown, digest, note in rows:
        print('| `%s` | `%s` | `%s` | %s |' % (enum, shown, digest, note))


if __name__ == '__main__':
    main()
