#!/usr/bin/env python3
"""THE BREEZE IMPORTER (architect 2026-11-01 / 2026-11-02, the Ableton parity's
glyphs: Breeze committed, monochrome re-coloring allowed).

A standalone tool, stdlib only, with no link path from any product target. Its
ONE TABLE, ROSTER below, maps each `icons::Icon` enumerator (src/gui/icons.h)
to its breeze-icons source drawing under the dark theme's root
(/usr/share/icons/breeze-dark, Arch's breeze-icons package). For each row it
writes assets/icons/<Enumerator>.svg by THE RECOLOR RULE
(assets/icons/README.md):

  - every literal color in a `fill`, `stroke`, `stop-color`, `color`,
    `flood-color` or `lighting-color` attribute or `style` declaration becomes
    `currentColor`; `none`, `currentColor`, `inherit` and `url(...)` stay, and
    every opacity stays;
  - the color-scheme `<style>` block (and with it an emptied `<defs>`) and
    every `class` attribute are dropped;
  - the path data and the drawing's own viewBox are kept verbatim (no seat).

The program then parses each file under the color its caller passes
(svg_icon::parse's bound color, resvg's user stylesheet). A file that would
leave a literal color, a class, a style block, or a shape painting resvg's
default black (no fill on it or an ancestor) fails the run, naming it.

Then it prints the README's provenance table: the enumerator, the source file
(and the file a source symlink resolves to), and the sha256 of the source
bytes. Re-pick a drawing by editing ROSTER and rerunning:

    python3 -I tools/icons/import_breeze.py
"""

import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET

BREEZE_VERSION = '6.31.0'
ROOT = '/usr/share/icons/breeze-dark'
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, 'assets', 'icons')

# ENUMERATOR -> <context>/22/<name>, in icons.h's enum order (THE ROSTER AS
# RULED, architect 2026-11-02, R12 / R14 / R15). EVERY PICK IS A DISTINCT
# DRAWING BY realpath AND PATH DATA — many Breeze names are symlinks to one
# file (dialog-close is window-close, so one enumerator, WindowClose) — save
# the one deliberate share R15 records: the score viewer's page PREV / NEXT
# (GoPreviousViewPage / GoNextViewPage) resolve to go-previous / go-next, the
# chevrons LEFT / RIGHT wear ("the left/right arrows mean 'step 1'").
ROSTER = [
    ('DocumentOpen', 'actions/22/document-open'),
    ('DocumentSave', 'actions/22/document-save'),
    ('DocumentSaveAs', 'actions/22/document-save-as'),
    ('VcsPull', 'actions/22/vcs-pull'),
    ('NetworkConnect', 'actions/22/network-connect'),
    ('VcsCommit', 'actions/22/vcs-commit'),
    ('EditUndo', 'actions/22/edit-undo'),
    ('EditRedo', 'actions/22/edit-redo'),
    ('MediaRecord', 'actions/22/media-record'),
    ('DialogCancel', 'actions/22/dialog-cancel'),
    ('EditCopy', 'actions/22/edit-copy'),
    ('ZoomIn', 'actions/22/zoom-in'),
    ('ZoomOut', 'actions/22/zoom-out'),
    ('ZoomFitBest', 'actions/22/zoom-fit-best'),
    ('ZoomPrevious', 'actions/22/zoom-previous'),
    ('ZoomInY', 'actions/22/zoom-in-y'),
    ('GoJump', 'actions/22/go-jump'),
    ('TimelineUseZoneOn', 'actions/22/timeline-use-zone-on'),
    ('EditCut', 'actions/22/edit-cut'),
    ('Bookmarks', 'actions/22/bookmarks'),
    ('MusicNote16th', 'actions/22/music-note-16th'),
    ('Mathmode', 'actions/22/mathmode'),
    ('Merge', 'actions/22/merge'),
    ('AudioHeadphones', 'devices/22/audio-headphones'),
    ('Lock', 'actions/22/lock'),
    ('SettingsConfigure', 'actions/22/settings-configure'),
    ('HelpWhatsthis', 'actions/22/help-whatsthis'),
    ('ViewMediaLyrics', 'actions/22/view-media-lyrics'),
    # (Raindrop, the NOISE lamp's drawing, left the roster 2026-11-05 with
    # the lamp: the band's two level cases paint no glyph.)
    ('ShallowHistory', 'actions/22/shallow-history'),
    ('DeepHistory', 'actions/22/deep-history'),
    ('BlackSum', 'actions/22/black_sum'),
    ('DocumentRevert', 'actions/22/document-revert'),
    ('KeyframePrevious', 'actions/22/keyframe-previous'),
    ('KeyframeNext', 'actions/22/keyframe-next'),
    ('DialogOkApply', 'actions/22/dialog-ok-apply'),
    ('ViewMediaTrack', 'actions/22/view-media-track'),
    ('ViewMediaVisualization', 'actions/22/view-media-visualization'),
    ('Speedometer', 'actions/22/speedometer'),
    ('ChronometerStart', 'actions/22/chronometer-start'),
    ('MediaPlaybackStart', 'actions/22/media-playback-start'),
    ('MediaPlaybackStop', 'actions/22/media-playback-stop'),
    ('MediaPlaybackPause', 'actions/22/media-playback-pause'),
    ('MediaSkipBackward', 'actions/22/media-skip-backward'),
    ('MediaSkipForward', 'actions/22/media-skip-forward'),
    ('MediaSeekBackward', 'actions/22/media-seek-backward'),
    ('MediaSeekForward', 'actions/22/media-seek-forward'),
    ('MediaRepeatSingle', 'actions/22/media-repeat-single'),
    ('ListAdd', 'actions/22/list-add'),
    ('ListRemove', 'actions/22/list-remove'),
    ('EditDelete', 'actions/22/edit-delete'),
    ('ViewHidden', 'actions/22/view-hidden'),
    ('InsertLink', 'actions/22/insert-link'),
    ('Link', 'actions/22/link'),
    ('GoJumpDeclaration', 'actions/22/go-jump-declaration'),
    ('DocumentEdit', 'actions/22/document-edit'),
    ('EditSelect', 'actions/22/edit-select'),
    ('TransformMoveVertical', 'actions/22/transform-move-vertical'),
    ('GoDown', 'actions/22/go-down'),
    ('GoUp', 'actions/22/go-up'),
    ('BboxPrev', 'actions/22/bboxprev'),
    ('BboxNext', 'actions/22/bboxnext'),
    ('TabDetach', 'actions/22/tab-detach'),
    ('ZoomOriginal', 'actions/22/zoom-original'),
    ('GoPrevious', 'actions/22/go-previous'),
    ('GoNext', 'actions/22/go-next'),
    ('GoParentFolder', 'actions/22/go-parent-folder'),
    ('GoPreviousViewPage', 'actions/22/go-previous-view-page'),
    ('GoNextViewPage', 'actions/22/go-next-view-page'),
    ('EditReset', 'actions/22/edit-reset'),
    ('ZoneIn', 'actions/22/zone-in'),
    ('ZoneOut', 'actions/22/zone-out'),
    ('WindowClose', 'actions/22/window-close'),
    ('EditClear', 'actions/22/edit-clear'),
    ('ViewRefresh', 'actions/22/view-refresh'),
    ('Folder', 'places/22/folder'),
    ('Battery000', 'status/22/battery-000'),
    ('Battery000Charging', 'status/22/battery-000-charging'),
]

# THE HAND TRANSCRIPTIONS (architect 2026-11-02, R14 (3)): the two cards'
# class glyphs are not Breeze's plain recolor — dialog-error and
# dialog-information fill a square in one color with the mark in another,
# so one color would fill the square whole. Their files are the 10-10
# CUT-OUT transcriptions (the mark cut out of the filled square, even-odd),
# kept by hand in assets/icons/ and NEVER WRITTEN HERE: the importer checks
# them by the recolor rule's own check and leaves their bytes alone.
HAND = ('DialogError', 'DialogInformation')

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


def main():
    os.makedirs(OUT, exist_ok=True)
    for enum in HAND:
        with open(os.path.join(OUT, enum + '.svg'), encoding='utf-8') as f:
            check(enum, f.read())
    rows = []
    for enum, rel in ROSTER:
        src = os.path.join(ROOT, rel + '.svg')
        real = os.path.realpath(src)
        data = open(real, 'rb').read()
        text = recolor(data.decode('utf-8'))
        check(enum, text)
        with open(os.path.join(OUT, enum + '.svg'), 'w', encoding='utf-8') as f:
            f.write(text)
        shown = rel + '.svg'
        if real != src:
            shown += ' (-> %s)' % os.path.relpath(real, os.path.join(ROOT, os.path.dirname(rel)))
        rows.append((enum, shown, hashlib.sha256(data).hexdigest()))
    print('| Enumerator | Source (breeze-dark %s) | sha256 of the source |' % BREEZE_VERSION)
    print('|---|---|---|')
    for enum, shown, digest in rows:
        print('| `%s` | `%s` | `%s` |' % (enum, shown, digest))


if __name__ == '__main__':
    main()
