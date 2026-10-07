#!/usr/bin/env python3
# tools/mockup/scene.py — A SCENE: what one captured screen holds, measured once by hand (tools/mockup/README.md,
# THE SCENE FORMAT). Lengths are WINDOWS PX at the capture's gui_scale; this tool takes whole-percent scales that are
# multiples of 100 (one Windows px = a whole number of device px), every row and column landing on a device px.
# The chrome's own constants (the caption's icon seat and button boxes off src/gui/render.h) are read on every run,
# never restated here; the captures' toolbar case is the one frozen record (below).
import glob, json, os, re

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
SCENES = os.path.join(HERE, 'scenes')
RENDER_H = os.path.join(REPO, 'src', 'gui', 'render.h')

_RENDER_CONSTANTS = ('kCaptionHeightPx', 'kCaptionIconXPx', 'kCaptionIconYPx', 'kCaptionIconPx',
                     'kCaptionButtonWPx', 'kCaptionButtonHPx', 'kCaptionButtonInsetPx', 'kCaptionCloseGapPx',
                     'kReliefLinePx')
# THE CAPTURES' TOOLBAR CASE, FROZEN: every scene in scenes/ is a capture of the Windows 95 chrome, Windows 95's small
# case 23 x 22 (lead 3, the 16-px glyph, trail 4 right and 3 below — kChromeSpecWin95 at e7e0ddf4), which the source no
# longer carries since the product dropped Windows 95 for Windows 2000 (2026-10-06 evening; chrome_spec.h's
# kChromeSpecWin2000, the large 31 x 30 case). A capture of the Windows 2000 chrome needs a scene field of its own for
# its case; none is supported yet.
_WIN95_CASE = {'kIconCaseLeadPx': 3, 'kIconGlyphPx': 16, 'kIconCaseTrailXPx': 4, 'kIconCaseTrailYPx': 3}


def render_constants():
    src = open(RENDER_H).read()
    out = {}
    for k in _RENDER_CONSTANTS:
        m = re.search(r'inline constexpr int ' + k + r'\s*=\s*(\d+);', src)
        if not m:
            raise SystemExit(f'tools/mockup: {RENDER_H}: no `inline constexpr int {k}` (the source moved)')
        out[k] = int(m.group(1))
    out.update(_WIN95_CASE)
    return out


class SceneError(Exception):
    pass


class Scene:
    """A loaded scene; every length also available in device px (`px`)."""

    def __init__(self, path):
        self.path = path
        d = json.load(open(path))
        self.d = d
        for k in ('capture', 'gui_scale', 'size_px', 'body', 'rows', 'lanes'):
            if k not in d:
                raise SceneError(f'{path}: missing "{k}"')
        if d['gui_scale'] % 100:
            raise SceneError(f'{path}: gui_scale {d["gui_scale"]} is not a multiple of 100 (this tool needs whole '
                             f'device px per Windows px)')
        self.U = d['gui_scale'] // 100
        self.capture = d['capture']
        self.capture_path = d.get('capture_path')
        self.body = d['body']
        if self.body not in ('wave', 'list'):
            raise SceneError(f'{path}: body "{self.body}" is neither "wave" nor "list"')
        self.rows = {k: self.span(v, k) for k, v in d['rows'].items()}
        need = ('caption', 'menu', 'bottom') + (('trim', 'ruler', 'marker', 'well') if self.body == 'wave'
                                                else ('list',))
        for k in need:
            if k not in self.rows:
                raise SceneError(f'{path}: rows lacks "{k}" (a "{self.body}" body needs {", ".join(need)})')
        self.fields = [(self.span(f['rows'], 'field rows'), self.span(f['cols'], 'field cols'))
                       for f in d.get('fields', [])]
        self.keeps = [(self.span(k['rows'], 'keep rows'), self.span(k['cols'], 'keep cols'), k['ground'])
                      for k in d.get('keeps', [])]
        self.lanes = {}
        for name, lane in d['lanes'].items():
            groups = lane['groups']
            self.lanes[name] = {'rows': self.span(lane['rows'], f'lane {name} rows'),
                                'icons': [e for g in groups for e in g], 'groups': groups}

    def px(self, w):
        v = w * self.U
        if abs(v - round(v)) > 1e-9:
            raise SceneError(f'{self.path}: {w} Windows px is not a whole device px at {self.U} px a unit')
        return int(round(v))

    def span(self, pair, what):
        a, b = pair
        if not b > a:
            raise SceneError(f'{self.path}: {what} {pair} is empty')
        return (self.px(a), self.px(b))


def find(capture_arg):
    """--capture: a PNG path, or a fragment naming a scene's capture (e.g. 062542) -> (capture path, scene path)."""
    scenes = sorted(glob.glob(os.path.join(SCENES, '*.json')))
    if os.path.isfile(capture_arg):
        base = os.path.basename(capture_arg)
        hits = [s for s in scenes if json.load(open(s))['capture'] == base]
        return capture_arg, (hits[0] if len(hits) == 1 else None)
    hits = [s for s in scenes if capture_arg in json.load(open(s))['capture']]
    if len(hits) != 1:
        raise SceneError(f"--capture '{capture_arg}': not a file, and it names {len(hits)} scenes' captures "
                         f"(one is needed)")
    d = json.load(open(hits[0]))
    p = d.get('capture_path')
    if not p:
        raise SceneError(f'{hits[0]}: names no capture_path; pass the capture PNG itself')
    p = os.path.join(REPO, p)
    if not os.path.isfile(p):
        raise SceneError(f"{hits[0]}: its capture_path '{p}' does not exist; pass the capture PNG itself")
    return p, hits[0]
