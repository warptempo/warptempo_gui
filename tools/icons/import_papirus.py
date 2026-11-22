#!/usr/bin/env python3
"""THE PAPIRUS IMPORTER (architect 2026-11-17, session 140: his set is
Papirus; 2026-11-21, send GG: the one set, src/gui/icons.h's head).

A standalone tool, stdlib only, with no link path from any product target. Its
ONE TABLE, ROSTER below, maps each `icons::Icon` enumerator (src/gui/icons.h)
to its papirus-icon-theme source drawing, a path under the checkout's
`Papirus/16x16/` (the 16-px symbolic set, `symbolic/...-symbolic.svg`, save
the three drawings of the 16-px `actions/` tree that set does not carry,
re-picked by the architect 2026-11-17). For each row it writes
assets/icons/<Enumerator>.svg by THE RECOLOR RULE (assets/icons/README.md):

  - every literal color in a `fill`, `stroke`, `stop-color`, `color`,
    `flood-color` or `lighting-color` attribute or `style` declaration becomes
    `currentColor`; `none`, `currentColor`, `inherit` and `url(...)` stay, and
    every opacity stays;
  - the color-scheme `<style>` block (and with it an emptied `<defs>`) and
    every `class` attribute are dropped;
  - the path data and the drawing's own viewBox are kept verbatim (no seat).

A file that would leave a literal color, a class, a style block, or a shape
painting resvg's default black (no fill on it or an ancestor) fails the run,
naming it. EVERY ROW IS IMPORTED, the two cards included: Papirus's
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
flags, session 123) or on two rows sharing one drawing. Re-pick a drawing by
editing ROSTER and rerunning against the checkout (papirus-icon-theme, commit
33d971eb):

    python3 -I tools/icons/import_papirus.py <papirus-icon-theme checkout>

THE CHECKOUT IS DATA: nothing in it is run; the tool reads its SVG files and
nothing else.
"""

import hashlib
import os
import re
import sys
import xml.etree.ElementTree as ET

PAPIRUS_COMMIT = '33d971eb'
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, 'assets', 'icons')

# ENUMERATOR -> (source under Papirus/16x16/, note), in icons.h's enum order.
# THE TABLE OF SESSION 123 (the parked importer's picks, the architect's
# sheets of session 140, tmp/s140/out/icons_1_of_2.png / icons_2_of_2.png)
# WITH FIVE ROWS RE-PICKED BY HIM 2026-11-17, session 140 ("the first
# alternative for each one"): TabDetach, TransformMoveVertical,
# GoParentFolder, ZoneIn and ZoneOut, the last three from the 16-px actions
# tree (no symbolic twin). A note names a pick that is not the enumerator's
# own name.
SYM = 'symbolic/'
ROSTER = [
    ('DocumentOpen', SYM + 'actions/document-open-symbolic.svg', ''),
    ('DocumentSave', SYM + 'actions/document-save-symbolic.svg', ''),
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
    ('ZoomPrevious', SYM + 'actions/xapp-go-history-previous-symbolic.svg',
     'no zoom-previous; the history-back arrow'),
    ('ZoomInY', SYM + 'actions/image-resize-symbolic.svg',
     'no zoom-in-y; the resize handles'),
    ('GoJump', SYM + 'actions/go-jump-symbolic.svg', ''),
    ('TimelineUseZoneOn', SYM + 'actions/selection-mode-symbolic.svg',
     'no timeline-use-zone-on; selection-mode'),
    ('EditCut', SYM + 'actions/edit-cut-symbolic.svg', ''),
    ('Bookmarks', SYM + 'actions/bookmarks-symbolic.svg', ''),
    ('MusicNote16th', SYM + 'actions/music-note-single-symbolic.svg',
     'no 16th note; the single note'),
    ('Mathmode', SYM + 'apps/accessories-calculator-symbolic.svg',
     'no mathmode; the calculator'),
    ('Merge', SYM + 'actions/object-merge-symbolic.svg',
     'no merge; object-merge'),
    ('AudioHeadphones', SYM + 'devices/audio-headphones-symbolic.svg', ''),
    ('Lock', SYM + 'actions/lock-symbolic.svg', ''),
    ('SettingsConfigure', SYM + 'actions/settings-symbolic.svg',
     'no settings-configure; settings'),
    ('HelpWhatsthis', SYM + 'actions/help-about-symbolic.svg',
     'no help-whatsthis; help-about'),
    ('ViewMediaLyrics', SYM + 'actions/lyrics-on-symbolic.svg',
     'no view-media-lyrics; lyrics-on'),
    ('ShallowHistory', SYM + 'actions/recent-symbolic.svg',
     'no shallow-history; recent (a clock)'),
    ('DeepHistory', SYM + 'actions/clock-alt-symbolic.svg',
     'no deep-history; clock-alt'),
    ('BlackSum', SYM + 'actions/feather-plus-circle-symbolic.svg',
     'no black_sum; a circled plus'),
    ('DocumentRevert', SYM + 'actions/document-revert-symbolic.svg', ''),
    ('KeyframePrevious', SYM + 'actions/skip-backward-large-symbolic.svg',
     'no keyframe-previous; the large skip back'),
    ('KeyframeNext', SYM + 'actions/skip-forward-large-symbolic.svg',
     'no keyframe-next; the large skip forward'),
    ('DialogOkApply', SYM + 'actions/feather-check-symbolic.svg',
     'no dialog-ok-apply; a check'),
    ('ViewMediaTrack', SYM + 'actions/music-playlist-symbolic.svg',
     'no view-media-track; the playlist notes'),
    ('ViewMediaVisualization', SYM + 'apps/media-eq-symbolic.svg',
     'no view-media-visualization; the equalizer'),
    ('Speedometer', SYM + 'actions/speedometer-symbolic.svg', ''),
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
    ('GoJumpDeclaration', SYM + 'actions/send-to-symbolic.svg',
     'no go-jump-declaration; the send-to arrow'),
    ('DocumentEdit', SYM + 'actions/document-edit-symbolic.svg', ''),
    ('EditSelect', SYM + 'actions/edit-select-symbolic.svg', ''),
    ('TransformMoveVertical', SYM + 'actions/object-flip-vertical-symbolic.svg',
     'no transform-move-vertical; object-flip-vertical (re-picked 2026-11-17)'),
    ('GoDown', SYM + 'actions/go-down-symbolic.svg', ''),
    ('GoUp', SYM + 'actions/go-up-symbolic.svg', ''),
    ('BboxPrev', SYM + 'actions/pan-start-symbolic.svg',
     'no bboxprev; the left pan triangle'),
    ('BboxNext', SYM + 'actions/pan-end-symbolic.svg',
     'no bboxnext; the right pan triangle'),
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
    ('ZoneIn', 'actions/zone-in.svg',
     'the 16-px actions tree, no symbolic twin (re-picked 2026-11-17)'),
    ('ZoneOut', 'actions/zone-out.svg',
     'the 16-px actions tree, no symbolic twin (re-picked 2026-11-17)'),
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


def path_data_digest(text):
    """The drawing's path data, whitespace dropped: two rows with one digest
    are one drawing whatever their names."""
    data = ''.join(re.findall(r'\sd="([^"]*)"', text))
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
    for enum, rel, note in ROSTER:
        src = os.path.join(root, rel)
        real = os.path.realpath(src)
        if not os.path.isfile(real):
            sys.exit('%s: no source %s' % (enum, rel))
        with open(real, 'rb') as f:
            data = f.read()
        text = recolor(data.decode('utf-8'))
        check(enum, text)
        with open(os.path.join(OUT, enum + '.svg'), 'w', encoding='utf-8') as f:
            f.write(text)
        digest = path_data_digest(text)
        if digest in seen:
            note = '; '.join(x for x in (note, 'one drawing with ' + seen[digest]) if x)
        else:
            seen[digest] = enum
        shown = rel
        if real != os.path.abspath(src):
            shown += ' (-> %s)' % os.path.relpath(real, root)
        rows.append((enum, shown, hashlib.sha256(data).hexdigest(), note))
    print('| Enumerator | Source (Papirus/16x16/, papirus-icon-theme %s) '
          '| sha256 of the source | Note |' % PAPIRUS_COMMIT)
    print('|---|---|---|---|')
    for enum, shown, digest, note in rows:
        print('| `%s` | `%s` | `%s` | %s |' % (enum, shown, digest, note))


if __name__ == '__main__':
    main()
