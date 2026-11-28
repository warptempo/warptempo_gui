#!/usr/bin/env python3
"""THE PAPIRUS IMPORTER (architect 2026-11-17, session 140: his set is
Papirus; 2026-11-21, send GG: the one set, src/gui/icons.h's head).

A standalone tool, stdlib only, with no link path from any product target. Its
ONE TABLE, ROSTER below, maps each `icons::Icon` enumerator (src/gui/icons.h)
to its source drawing and, on a row that names one, A TRANSFORM. A source is
either papirus-icon-theme's, a path under the checkout's `Papirus/16x16/`
(the 16-px symbolic set, `symbolic/...-symbolic.svg`, or, where the
architect's pick lives there, the 16-px `actions/`, `devices/` and `panel/`
trees), or, on a path that starts with OWN, OURS: a hand-made drawing in
this repository's tools/icons/own/, its derivation named in its own XML
comment (twelve such files: the marker pair's arrow to a line, from
Papirus's go-first, architect 2026-11-22, send MM; Value Drag's double
arrow, go-first's arrow upright with a head each way, architect
2026-11-23, send NN; and, architect 2026-11-23, send OO, his picks off the
mocks: Cumulative's sigma, drawn outright with no Papirus source; the
score's page keys' head, go-previous's without its stem (the history
view's Older / Newer its second reader, architect 2026-11-27); Restrict's
dot and wall, go-first's dot and the marker pair's line; the picker's Up,
folder-symbolic's folder with an up arrow cut out of it; Commit and Pull,
go-up's and go-down's arrows on a line; and Check, the two arrows side by
side; and, architect 2026-11-24, send QQ, his pick "L1", the tab pair's A
and B, Liberation Sans Bold's own outlines of the two capitals; and,
architect 2026-11-24, his pick "V1", Fit's recall, view-restore's four
corners with the left and right pairs 1 closer, both gaps 2). Both kinds
take the same road: for each row
it writes assets/icons/<Enumerator>.svg by THE RECOLOR RULE
(assets/icons/README.md):

  - every literal color in a `fill`, `stroke`, `stop-color`, `color`,
    `flood-color` or `lighting-color` attribute or `style` declaration becomes
    `currentColor`; `none`, `currentColor`, `inherit` and `url(...)` stay, and
    every opacity stays;
  - the color-scheme `<style>` block (and with it an emptied `<defs>`) and
    every `class` attribute are dropped;
  - the path data and the drawing's own viewBox are kept verbatim (no seat).

THE TRANSFORM (architect 2026-11-22, send II: one source turned to serve a
second act): a row's fourth field, 'mirror' (left-right, the one kind),
wraps every child of the root <svg> in ONE `<g transform="...">` about the
center of the drawing's box (its viewBox, else its width and height),
`matrix(-1 0 0 1 2cx 0)`. The path data inside stays verbatim; resvg
applies the group at the parse. A row with no fourth field is written
untransformed. Two rows wear it: BboxNext, the own marker drawing mirrored
(send MM), and GoNextViewPage, the score's own page head mirrored (send
OO).

A written file (the transform's group included) that would leave a literal
color, a class, a style block, or a shape painting resvg's default black (no
fill on it or an ancestor) fails the run, naming it. THE CARDS ARE IMPORTED
AS PAPIRUS DRAWS THEM: Papirus's dialog-error / dialog-information already
cut their mark out of the disc (the mark's subpaths wound against the
disc's), so one color shows it and no hand transcription is needed.

IT WRITES ONLY THE ROWS' FILES and removes nothing: the folder's other files
-- the licence text and README.md -- are never opened, so a rerun leaves
them as they are.
A row dropped from ROSTER leaves its old file behind, to be deleted by hand
with its enumerator.

Then it prints the README's provenance table: the enumerator, the source file
(and the file a source symlink resolves to; an own source by its repository
path), the sha256 of the source bytes, and a note on a pick whose name is
not the enumerator's (the parked run's flags, session 123), on a re-pick,
on two rows sharing one drawing (the written shapes, their geometry and
every transform, so a mirrored row is not its source's twin), or on two
rows sharing one source file turned into two drawings.
Re-pick a drawing by editing ROSTER and rerunning against the checkout
(PAPIRUS_SOURCE below):

    python3 -I tools/icons/import_papirus.py <papirus-icon-theme checkout>

THE CHECKOUT IS DATA: nothing in it is run; the tool reads its SVG files and
nothing else (and of this repository's tools/icons/own/, the rows' files).
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
# 2026-11-22 are its own. An own source (OWN below) is no Papirus file; the
# table names it by its repository path.
PAPIRUS_SOURCE = 'papirus-icon-theme 20260801'
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, 'assets', 'icons')
# THE OWN SOURCES (the module's head): a ROSTER path starting with OWN names a
# file of this folder instead of the checkout's.
OWN = 'own:'
OWN_DIR = os.path.join(REPO, 'tools', 'icons', 'own')

# ENUMERATOR -> (source under Papirus/16x16/ or OWN + a file of
# tools/icons/own/, note[, transform]), in icons.h's enum order. THE TABLE OF
# SESSION 123 (the parked importer's picks, the architect's sheets of session
# 140) WITH ONE ROW KEPT OF TWO RE-PICKED BY HIM 2026-11-17, session 140
# ("the first alternative for each one"): GoParentFolder (the 16-px actions
# tree, no symbolic twin; re-picked once more, below); AND
# NINETEEN ROWS RE-PICKED BY HIM 2026-11-22, send II, off the alternates
# sheet: BboxPrev / BboxNext, ViewMediaLyrics,
# ViewMediaTrack, TransformMoveVertical, GoJump, TimelineUseZoneOn,
# GoJumpDeclaration, Mathmode, ViewMediaVisualization, ZoomInY, Speedometer,
# ZoomPrevious, DeepHistory, ZoneIn, ZoneOut,
# DocumentSave, BlackSum and Merge, most from the 16-px actions, devices and
# panel trees (ZoomPrevious and ZoneIn / ZoneOut re-picked once more,
# below); AND EIGHT OF
# THOSE RE-PICKED AGAIN BY HIM 2026-11-22, send MM: TransformMoveVertical,
# BlackSum and TimelineUseZoneOn (each re-picked once more, below), GoJump
# (go-first-rtl, an arrow right to a dot), DeepHistory, Merge, and BboxPrev /
# BboxNext (our own drawing, OWN + 'BboxPrev.svg', Next's mirrored); AND
# TransformMoveVertical RE-PICKED AGAIN BY HIM 2026-11-23, send NN, his pick
# "B3" off the mock (our own drawing, OWN + 'TransformMoveVertical.svg', no
# transform); AND TEN ROWS
# RE-PICKED BY HIM 2026-11-23, send OO, his picks off the mocks, every one
# our own drawing, OWN + '<Enumerator>.svg': BlackSum (re-picked again by
# him 2026-11-23, send PP, "S2", the serifed sigma),
# GoPreviousViewPage / GoNextViewPage ("P1", one source, Next's mirrored),
# ZoneIn / ZoneOut ("K5"; re-picked again, below), TimelineUseZoneOn ("C2"),
# GoParentFolder ("U1"), VcsCommit / VcsPull ("V1") and NetworkConnect
# ("N2"); AND TWO ROWS ADDED BY HIM 2026-11-24, send QQ, his pick "L1", the
# tab pair's TabA / TabB, our own drawings, Liberation Sans Bold's own
# outlines of the capitals (OWN + '<Enumerator>.svg'); AND THREE ROWS
# RE-PICKED BY HIM 2026-11-24: ZoomPrevious ("V1", view-restore's corners
# with the halves 1 closer, our own drawing, OWN + 'ZoomPrevious.svg'), and
# ZoneIn / ZoneOut ("G", Papirus's actions/bookmark-new and bookmark-remove,
# untouched); AND ONE ROW ADDED BY HIM 2026-11-26, his pick, RELEASE's
# BluetoothPaired (the 16-px panel tree's bluetooth-paired, untouched). A
# note names a pick that is not the enumerator's own name and the date of a
# re-pick.
SYM = 'symbolic/'
ROSTER = [
    ('DocumentOpen', SYM + 'actions/document-open-symbolic.svg', ''),
    ('DocumentSave', 'devices/media-floppy.svg',
     'media-floppy, a floppy disk, his pick over document-save '
     '(re-picked 2026-11-22)'),
    ('VcsPull', OWN + 'VcsPull.svg',
     'no vcs-pull; ours, go-down\'s arrow, its tip landing on a line 2 '
     'thick (re-picked 2026-11-23, send OO)'),
    ('NetworkConnect', OWN + 'NetworkConnect.svg',
     'no network-connect; ours, two arrows side by side, one up and one '
     'down, in go-up\'s and go-down\'s grammar (re-picked 2026-11-23, '
     'send OO)'),
    ('VcsCommit', OWN + 'VcsCommit.svg',
     'no vcs-commit; ours, go-up\'s arrow, its stem standing on a line 2 '
     'thick (re-picked 2026-11-23, send OO)'),
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
    ('ZoomPrevious', OWN + 'ZoomPrevious.svg',
     'no zoom-previous; ours, view-restore\'s four corners turned in, the '
     'left pair 1 right and the right pair 1 left, both gaps 2 (re-picked '
     '2026-11-24, "V1")'),
    ('ZoomInY', 'actions/audio-volume-high.svg',
     'no zoom-in-y; audio-volume-high, a speaker and its waves '
     '(re-picked 2026-11-22)'),
    ('GoJump', SYM + 'actions/go-first-rtl-symbolic.svg',
     'go-first-rtl, an arrow right to a dot, his pick over go-jump '
     '(re-picked 2026-11-22, send MM)'),
    ('TimelineUseZoneOn', OWN + 'TimelineUseZoneOn.svg',
     'no timeline-use-zone-on; ours, go-first\'s dot and the marker '
     'pair\'s wall with nothing between, no arrow (re-picked 2026-11-23, '
     'send OO)'),
    ('EditCut', SYM + 'actions/edit-cut-symbolic.svg', ''),
    ('Bookmarks', SYM + 'actions/bookmarks-symbolic.svg', ''),
    ('MusicNote16th', SYM + 'actions/music-note-single-symbolic.svg',
     'no 16th note; the single note'),
    ('Mathmode', 'actions/insert-math-expression.svg',
     'no mathmode; insert-math-expression, an f of x '
     '(re-picked 2026-11-22)'),
    ('Merge', SYM + 'actions/tool-brush-symbolic.svg',
     'tool-brush, a brush, his pick over merge and edit-paste-style '
     '(re-picked 2026-11-22, send MM)'),
    ('AudioHeadphones', SYM + 'devices/audio-headphones-symbolic.svg', ''),
    ('Lock', SYM + 'actions/lock-symbolic.svg', ''),
    ('SettingsConfigure', SYM + 'actions/settings-symbolic.svg',
     'no settings-configure; settings'),
    ('HelpWhatsthis', SYM + 'actions/help-about-symbolic.svg',
     'no help-whatsthis; help-about'),
    ('BluetoothPaired', 'panel/bluetooth-paired.svg',
     'the Bluetooth sign between two dots, RELEASE\'s, his pick '
     '(2026-11-26)'),
    ('ViewMediaLyrics', SYM + 'places/folder-music-symbolic.svg',
     'no view-media-lyrics; folder-music, two beamed notes '
     '(re-picked 2026-11-22)'),
    ('ShallowHistory', SYM + 'actions/recent-symbolic.svg',
     'no shallow-history; recent (a clock)'),
    ('DeepHistory', 'panel/network-unavailable.svg',
     'no deep-history; network-unavailable, a faded network port and a '
     'cross (re-picked 2026-11-22, send MM)'),
    ('BlackSum', OWN + 'BlackSum.svg',
     'no black_sum; ours, a serifed sigma, no Papirus source (re-picked '
     '2026-11-23, send PP)'),
    ('DocumentRevert', SYM + 'actions/document-revert-symbolic.svg', ''),
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
    ('TransformMoveVertical', OWN + 'TransformMoveVertical.svg',
     'no transform-move-vertical; ours, go-first\'s arrow upright, one head '
     'up and one down on one stem (re-picked 2026-11-23, send NN)'),
    ('GoDown', SYM + 'actions/go-down-symbolic.svg', ''),
    ('GoUp', SYM + 'actions/go-up-symbolic.svg', ''),
    ('BboxPrev', OWN + 'BboxPrev.svg',
     'no bboxprev; ours, go-first\'s arrow to a line 2 wide, as drawn, '
     'pointing left (re-picked 2026-11-22, send MM)'),
    ('BboxNext', OWN + 'BboxPrev.svg',
     'no bboxnext; ours, go-first\'s arrow to a line 2 wide, mirrored '
     'left-right to point right (re-picked 2026-11-22, send MM)', 'mirror'),
    ('TabA', OWN + 'TabA.svg',
     'ours, Liberation Sans Bold\'s own outline of the capital A, 10 tall '
     'in the 16 box, on Papirus\'s grid (2026-11-24, send QQ; resized, '
     '"PG_1_P1")'),
    ('TabB', OWN + 'TabB.svg',
     'ours, Liberation Sans Bold\'s own outline of the capital B, 10 tall '
     'in the 16 box, on Papirus\'s grid (2026-11-24, send QQ; resized, '
     '"PG_1_P1")'),
    ('ZoomOriginal', SYM + 'actions/zoom-original-symbolic.svg', ''),
    ('GoPrevious', SYM + 'actions/go-previous-symbolic.svg', ''),
    ('GoNext', SYM + 'actions/go-next-symbolic.svg', ''),
    ('GoParentFolder', OWN + 'GoParentFolder.svg',
     'ours, folder-symbolic\'s folder with an up arrow cut out of its '
     'body, his pick over go-parent-folder (re-picked 2026-11-23, send OO)'),
    ('GoPreviousViewPage', OWN + 'GoPreviousViewPage.svg',
     'no go-previous-view-page; ours, go-previous\'s head without its '
     'stem, as drawn, pointing left (re-picked 2026-11-23, send OO)'),
    ('GoNextViewPage', OWN + 'GoPreviousViewPage.svg',
     'no go-next-view-page; ours, go-previous\'s head without its stem, '
     'mirrored left-right to point right (re-picked 2026-11-23, send OO)',
     'mirror'),
    ('EditReset', SYM + 'actions/object-rotate-left-symbolic.svg',
     'no edit-reset; the counter-clockwise arrow'),
    ('ZoneIn', 'actions/bookmark-new.svg',
     'no zone-in; bookmark-new, a bookmark ribbon with a plus cut out of '
     'it, his pick (re-picked 2026-11-24, "G")'),
    ('ZoneOut', 'actions/bookmark-remove.svg',
     'no zone-out; bookmark-remove, a bookmark ribbon with a minus cut out '
     'of it, his pick (re-picked 2026-11-24, "G")'),
    ('EditClear', SYM + 'actions/edit-clear-symbolic.svg', ''),
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
GEOMETRY = ('d', 'x', 'y', 'width', 'height', 'cx', 'cy', 'r', 'rx', 'ry',
            'points', 'x1', 'y1', 'x2', 'y2')
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
        x, _, w, _ = (float(v) for v in box.replace(',', ' ').split())
    else:
        x, w = 0.0, float(root.get('width'))
    cx = x + w / 2
    if kind != 'mirror':
        sys.exit('%s: no transform %r' % (name, kind))
    matrix = 'matrix(-1 0 0 1 %s 0)' % num(2 * cx)
    head = re.search(r'<svg\b[^>]*>', text)
    tail = text.rindex('</svg>')
    return (text[:head.end()] + '\n <g transform="%s">' % matrix
            + text[head.end():tail] + ' </g>\n' + text[tail:])


def drawing_digest(text):
    """The written drawing, every shape's kind and geometry and every
    element's transform, in document order, with whitespace dropped: two
    rows with one digest are one drawing whatever their names, and a
    transformed row is not its source's twin."""
    parts = []
    for el in ET.fromstring(text).iter():
        tag = el.tag.replace(SVG_NS, '')
        names = ('transform',) + (GEOMETRY if tag in SHAPES else ())
        parts.append(tag + ''.join('%s=%s;' % (n, el.get(n))
                                   for n in names if el.get(n) is not None))
    data = re.sub(r'\s+', '', '|'.join(parts))
    return hashlib.sha256(data.encode()).hexdigest()


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
        own = rel.startswith(OWN)
        src = (os.path.join(OWN_DIR, rel[len(OWN):]) if own
               else os.path.join(root, rel))
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
        if own:
            shown = os.path.relpath(real, REPO)
        else:
            shown = rel
            if real != os.path.abspath(src):
                shown += ' (-> %s)' % os.path.relpath(real, root)
        rows.append((enum, shown, hashlib.sha256(data).hexdigest(), note))
    print('| Enumerator | Source (Papirus/16x16/, %s; or ours, '
          'its repository path) | sha256 of the source | Note |'
          % PAPIRUS_SOURCE)
    print('|---|---|---|---|')
    for enum, shown, digest, note in rows:
        print('| `%s` | `%s` | `%s` | %s |' % (enum, shown, digest, note))


if __name__ == '__main__':
    main()
