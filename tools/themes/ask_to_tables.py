#!/usr/bin/env python3
"""THE THEME TABLES' GENERATOR (architect 2026-11-05, the theme arc: "i'd like
to implement all" — Ableton Live 12's own themes selectable whole, chrome and
well together, through the one `theme` device key).

A standalone tool, stdlib only, with no link path from any product target (the
shape of tools/icons/import_breeze.py). It reads Live's theme files — `.ask`,
Ableton's theme XML: a `<Theme>` holding one `<RoleName Value="#rrggbb" />` or
`#rrggbbaa` per role — from a folder OUTSIDE the repository and writes ONE
GENERATED HEADER, src/gui/theme_tables.h: every sRGB word the GUI paints, one
row per theme (GuiThemeTones), with the slug the config spells, the group the
Settings page's value list shows it under and the author credited in
THIRD_PARTY.md. THE .ASK FILES NEVER ENTER THE REPO (Ableton's and the authors'
files): only the generated number tables do.

    python3 -I tools/themes/ask_to_tables.py [<ask folder>]

The folder defaults to tmp/keep/screenshots/ableton/ask (gitignored). It prints
the table it wrote and the credit lines THIRD_PARTY.md carries.

THE WALK. The folder's top-level `*.ask` files are BUILTIN, of which exactly
the seven of BUILTIN_FILES below are taken (by file name; Default's other
eleven variants — Light, Cool, Warm, High — are skipped silently: Default is
"Default Dark Neutral Medium" alone, architect 2026-11-05). Each entry of
VARIANTS below adds one more BUILTIN row from a taken file's roles, one role
read in place of another, optionally shaded in OKLCH (immaterial_gold:
Immaterial.ask with its own SearchIndication as the accent, darkened to L 72
and warmed to h 79, architect 2026-11-05 / 2026-11-06), so seven files give
eight builtin rows. Every `*.ask` under
a SUBFOLDER is EXTRA, walked recursively, `__MACOSX` directories and `._*`
files (the zips' resource forks) skipped; the first path component under the
ask folder names the author through AUTHORS below, the credit's one source —
an unknown author folder fails the run, naming it. THE SLUG is the file's stem
lowercased, every run of non-[a-z0-9] one `_`, leading and trailing `_`
stripped ("Twenty-Four Carat" -> `twenty_four_carat`, "SB Red Greys" ->
`sb_red_greys`); the Default file's slug is `default`. A duplicate slug fails
the run. THE ORDER is the value list's: BUILTIN with `default` first then the
other files' rows and the variants together by slug (immaterial_gold right
after immaterial), then EXTRA by slug. The display name is the slug — the value list
shows the config's own spelling, as every other list does (settings_editor.h's
head, the unitless rule).

THE DERIVATION — ONE TABLE, DERIVATION below, repeated at the generated
header's head. Our Default tone = Live's role, the planner having verified
2026-11-05 that the Default row reproduces every constant of HEAD to the byte
(the static_asserts in src/gui/palette_file.h are that proof in the build):

  ROLE-NAMED (one Live role each):
    ground                 242424 = SurfaceArea (ruling 2, 2026-11-05: the
                                    arrangement's floor, his "#56" on Classic
                                    Medium Light over the panels' DADADA)
    lane, trim bar outside 2A2A2A = Desktop
    card ink               2A2A2A = Desktop
    key press face         2A2A2A = Desktop (the press arc, 2026-11-05: his
                                    pick p2 over ControlBackground 1E1E1E —
                                    "press in skeumorphic is darker")
    key face, ruler face, pane, time field  363636 = SurfaceBackground
    cue outline, text on lit, selection text  111111 = ControlContrastFrame
    text                   B5B5B5 = ControlForeground
    dim text, well frame   757575 = TextDisabled (the scrub channel and the
                                    tooltip border)
    inactive cue           5D5D5D = BackgroundClip
    lit fill AND cue       FFAD56 = ViewCheckControlEnabledOn
                                    (a VARIANT row reads the role its VARIANTS
                                    entry names instead, shaded in OKLCH when
                                    the entry says so: immaterial_gold
                                    Immaterial's SearchIndication ECCA6D
                                    darkened to L 72 and warmed to h 79, its
                                    chroma scaled by 72 / L0 = C79D57, every
                                    other tone Immaterial's, the lit press fill
                                    re-derived from the shaded accent by the
                                    ratio)
    playhead head          03C3D5 = RangeDefault
    selection fill         637E86 = StandbySelectionBackground
    list press fill        B0DDEB = SelectionBackground (the press arc,
                                    2026-11-05: "lighter for list
                                    (highlight)")
    list press text        070707 = SelectionForeground
    card face              E76942 = Alert (the cards; THE LIMITER LAMP too,
                                    architect 2026-11-05: "orange for limiter
                                    and red for clipper" — a warning)
    door lit fill          B595FC = ScaleAwareness (architect 2026-11-05, his
                                    "yes" on mock s46_m2: the SEVEN OVERLAY
                                    OPENERS' lit face, the "doors" class,
                                    lavender — a lit door says a surface
                                    stands, a lit lamp says a mode is on;
                                    materials.h's THE KEY)
    clip lamp              FF0A0A = StandardVuMeter / Maximum, the one NESTED
                                    role the tool reads (<StandardVuMeter>
                                    <Maximum Value=.../>): Live's own meter
                                    clip box — THE CLIP LAMP's lit face
                                    (architect 2026-11-05, his d6 "yes": a
                                    fault). FFAD56 REFUSED for a lamp: it is
                                    ViewCheckControlEnabledOn, the lit KEY's
                                    face, and "clip indicator is not a button"
    level audio fill       007383 = RangeEditField (Live's volume mini-slider,
                                    "green") — the AUDIO level case's fill,
                                    read by paint_level_case (2026-11-05)
    level noise fill       3C6AB6 = ChosenPreListen (Live's cue mini-slider,
                                    "blue") — the NOISE level case's fill,
                                    read by paint_level_case (2026-11-05)
  COMPOSITED (Live's alpha role over the clip view's ground, straight alpha
  aa/255, a 6-digit word opaque; Python's round() on the float, clamped):
    ink                    191919 = WaveformColor over DetailViewBackground
    canvas                 393939 = DetailGridTiles over DetailViewBackground
    center                 = ink (a role is what it paints: the zero line IS
                                  the ink, palette_file.h)
    score pane             = ink (modal_box.h's score pane: "from ableton
                                  palette directly")
  DERIVED (the app's own tones, each the Default mix of two theme roles, so a
  light theme keeps the relation):
    core                   1E1E1E = ink + 5/32 · (canvas − ink)
    grid                   313131 = canvas + 1/4 · (ink − canvas)
    key dim ink            5A5A5A = face + 36/63 · (dim − face)
    trim span              6A6A6A = face + 52/127 · (text − face)
    ruler tick             6B6B6B = face + 53/127 · (text − face)
    playhead line          85E2EB = FFFFFF + 0.483 · (head − FFFFFF)
                                    (ruling 4: Default keeps his pick, every
                                    other theme the same ratio, white toward
                                    the head — not the mean)
    lit press fill         C68743 = lit fill · (mean(press face) / mean(face))
                                    per channel (42 / 54 = 0.778 on Default;
                                    Immaterial FF91C3 · 0.737 = BC6B90) — a
                                    checked key under a standing press
                                    (architect 2026-11-05, the press arc's
                                    second landing, his "ratio" on mocks d1 /
                                    i1 of tmp/s45: "darken the lit key by the
                                    relationship ... so the lit press is
                                    derived"). THE RATIO, NOT THE DIFFERENCE
                                    (d2 / i2, ruled out): the ratio keeps the
                                    theme's lit hue and scales with its own
                                    press step — Classic Medium Light's step
                                    is 60 levels, the difference would nearly
                                    black its FFB901, the ratio scales it by
                                    86 / 146 = 0.589.
    door lit press fill    8D74C4 = door lit fill · the same ratio per channel
                                    (B595FC · 0.778 on Default) — a checked
                                    door under a standing press, exactly as
                                    the lamps' lit press fill is derived
                                    (architect 2026-11-05, the doors)
    scanner                FFFFFF (the controls' white, every theme)

Where a theme makes the lane, the trim's outside or the dim text coincide with
the ground, that is Live's own number — accepted, not a bug, no backstop
(architect 2026-11-05).
"""

import math
import os
import re
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_ASK_DIR = os.path.join(REPO, 'tmp', 'keep', 'screenshots', 'ableton', 'ask')
OUT = os.path.join(REPO, 'src', 'gui', 'theme_tables.h')

# THE BUILTIN FILES TAKEN, by file stem -> slug (the head's WALK). Every other
# top-level .ask is skipped silently.
BUILTIN_FILES = {
    'Default Dark Neutral Medium': 'default',
    'Angst Robot':                 None,
    'Classic Medium Dark':         None,
    'Classic Medium Light':        None,
    'Immaterial':                  None,
    'Riparian':                    None,
    'Twenty-Four Carat':           None,
}
BUILTIN_AUTHOR = ('Ableton AG', 'https://www.ableton.com')

# THE VARIANTS, the one owner of what a variant is: slug -> (the BUILTIN_FILES
# stem it reads, {role replaced: the role read in its place}, {role read:
# (OKLCH lightness, OKLCH hue)} — the SHADE, empty for none). A variant is a
# BUILTIN row of its own, credited to its source file, through the one
# derivation with the named role substituted; a role read with a shade is
# first taken to OKLCH (Ottosson's, `oklch` / `from_oklch` below), its
# lightness SET to the first number (0..100) and its hue to the second
# (degrees), its chroma scaled by the new lightness over its own (C / L held
# at the role's), and back to sRGB, each channel rounded and clamped
# (`shade`). Its source stem must be a taken builtin, every role read (ROLES)
# and a shaded role one the entry reads, or the run fails naming it.
# immaterial_gold (architect 2026-11-05, session 56, his pick m1 of tmp/s56's
# accent mocks on a real Immaterial capture): Immaterial without the pink
# FF91C3 accent — its lit fill and cue read the same file's SearchIndication
# (ECCA6D, the muted gold) for ViewCheckControlEnabledOn, so the accent sits
# on the ground "in the same way the ableton ask's are tuned", and the lit
# press fill follows by the ratio. A SEPARATE THEME, not an override of
# `immaterial` (which stays byte-identical): "lowest overhead, most honest,
# and least churn if reversed"; a per-theme override and a hand-picked hex
# are not the road (closed_questions.md). THE SHADE (architect 2026-11-06,
# sessions 58 and 60): the plain ECCA6D read "too bright" on his glass —
# "yellow is good but this one is too bright" — and "tune to the ground"
# meant complementing it: the ground's hue is OKLCH 264, its complement near
# 84, and a darkened pure yellow drifts olive, so the role is darkened to
# L 72 (from its own 84.9) and warmed to h 79 (from 89.7), C79D57 — his pick
# r3_m3 of tmp/s58's round-3 mocks, "implement m3 color"; recorded here as
# a step on the role, not a hand-picked hex.
VARIANTS = {
    'immaterial_gold': ('Immaterial',
                        {'ViewCheckControlEnabledOn': 'SearchIndication'},
                        {'SearchIndication': (72, 79)}),
}

# THE EXTRAS' AUTHORS: the first path component under the ask folder -> the
# credit (name, url) THIRD_PARTY.md carries. The one table; an author folder
# not here fails the run.
AUTHORS = {
    'sonicbloom': ('Sonic Bloom', 'https://sonicbloom.gumroad.com'),
}

# THE ROLES READ from every file (the head's DERIVATION), each present in
# every stock and Sonic Bloom file (checked over all 45, 2026-11-05; the
# press arc's two, SelectionBackground and SelectionForeground, over the same
# files the same day; the lamp arc's three, ScaleAwareness, RangeEditField
# and ChosenPreListen, the same night; SearchIndication, immaterial_gold's
# accent (VARIANTS), over all 42 files — the 18 stock, the 24 Sonic Bloom —
# 2026-11-06).
ROLES = [
    'SurfaceArea', 'Desktop', 'SurfaceBackground', 'ControlContrastFrame',
    'ControlForeground', 'TextDisabled', 'BackgroundClip',
    'ViewCheckControlEnabledOn', 'RangeDefault', 'StandbySelectionBackground',
    'SelectionBackground', 'SelectionForeground',
    'Alert', 'WaveformColor', 'DetailViewBackground', 'DetailGridTiles',
    'ScaleAwareness', 'RangeEditField', 'ChosenPreListen',
    'SearchIndication',
]

# THE ONE NESTED ROLE (the lamp arc, 2026-11-05): the clip lamp's red is the
# `Maximum` of the `<StandardVuMeter>` block — a `Maximum` stands in every VU
# meter block of the file (Overload, Disabled, Headphones, Sends …), so it is
# read INSIDE that one element and never by the flat walk above. Present in
# every one of the files (the planner's grep, 2026-11-05); a file without it
# fails the run.
NESTED_ELEMENT = 'StandardVuMeter'
NESTED_ROLE    = 'Maximum'

# GuiThemeTones' members, in the struct's order (the generated header), each
# with the painters that read it — the comment the header prints beside it.
MEMBERS = [
    ('ground',          'the flat ground everywhere outside the well and the keys, the panel\'s `face` too (SurfaceArea)'),
    ('text',            'every word, digit and drawing the chrome prints; `label` / `field_text` (ControlForeground)'),
    ('text_dim',        'the inert rows and the tooltip\'s hint line (TextDisabled)'),
    ('text_on_lit',     'anything printed on a lit face (ControlContrastFrame)'),
    ('key_dim_ink',     'a disabled key\'s glyph and legend (face + 36/63 toward text_dim)'),
    ('key_face',        'the key at rest, the time fields (SurfaceBackground)'),
    ('lit_fill',        'a checked key\'s whole face, Live\'s "on" (ViewCheckControlEnabledOn)'),
    ('lit_press_fill',  'a checked key\'s face while a press stands on it (lit_fill scaled by the press face\'s ratio to the face)'),
    ('door_lit_fill',   'a checked OVERLAY OPENER\'s face, the seven doors (ScaleAwareness)'),
    ('door_lit_press_fill', 'a checked door\'s face while a press stands on it (door_lit_fill scaled by the same ratio)'),
    ('key_press_face',  'an unchecked key\'s face while a press stands on it (Desktop)'),
    ('well_frame',      'the scrub\'s channel and the tooltip\'s border (TextDisabled)'),
    ('ruler_face',      'the ruler (SurfaceBackground)'),
    ('ruler_tick',      'the ruler\'s ticks (face + 53/127 toward text)'),
    ('lane_face',       'the marker lane (Desktop)'),
    ('trim_bar_face',   'the trim bar outside the trim (Desktop)'),
    ('trim_span_face',  'the trim bar inside the trim (face + 52/127 toward text)'),
    ('selection_fill',  'the selection pair\'s bar: the lists\' lit row, the editors\' selected substring; `selected_fill` (StandbySelectionBackground)'),
    ('selection_text',  'the selection pair\'s text; `selected_text` (ControlContrastFrame)'),
    ('list_press_fill',  'a list row\'s fill while a press stands on it (SelectionBackground)'),
    ('list_press_text',  'a list row\'s text and glyph under that press (SelectionForeground)'),
    ('pane_face',       'the modal family\'s box interior and the tooltip\'s face (SurfaceBackground)'),
    ('score_pane_face', 'the score viewer\'s pane, the page\'s mat (= the ink)'),
    ('card_face',       'the cards\' box and their ring; `card_ground` / `card_frame` (Alert)'),
    ('card_ink',        'the cards\' words and class glyph; `card_text` (Desktop)'),
    ('clip_lamp',       'the logo lane\'s clip lamp, lit (StandardVuMeter / Maximum); the limiter lamp lit wears card_face'),
    ('level_audio_fill', 'the AUDIO level case\'s fill (RangeEditField), read by paint_level_case'),
    ('level_noise_fill', 'the NOISE level case\'s fill (ChosenPreListen), read by paint_level_case'),
    ('waveform_canvas', 'the canvas (DetailGridTiles over DetailViewBackground)'),
    ('waveform_ink',    'the waveform, the plate\'s outer bar (WaveformColor over DetailViewBackground)'),
    ('waveform_core',   'the magnified inner bar\'s solid fill (ink + 5/32 toward canvas)'),
    ('grid',            'the canvas\'s quarter lines (canvas + 1/4 toward ink)'),
    ('center',          'each channel\'s zero line (= the ink)'),
    ('cue',             'every enabled marker\'s chip (ViewCheckControlEnabledOn)'),
    ('cue_outline',     'the chips\' outline, the selection stem, the rings, the ruler head\'s outline (ControlContrastFrame)'),
    ('inactive_cue',    'a disabled marker\'s chip fill and inert label (BackgroundClip)'),
    ('playhead_head',   'the playhead\'s triangle in the ruler (RangeDefault)'),
    ('playhead_line',   'the playhead\'s canvas line (white + 0.483 toward the head)'),
    ('scanner',         'the scanner and the zoom anchor\'s stem (FFFFFF, every theme)'),
]

ROLE_RE = re.compile(r'<([A-Za-z0-9_]+)\s+Value="#([0-9A-Fa-f]{6}|[0-9A-Fa-f]{8})"\s*/>')
NESTED_RE = re.compile(r'<%s>(.*?)</%s>' % (NESTED_ELEMENT, NESTED_ELEMENT), re.S)


def fail(msg):
    sys.stderr.write('ask_to_tables: ' + msg + '\n')
    sys.exit(1)


def slugify(stem):
    s = re.sub(r'[^a-z0-9]+', '_', stem.lower())
    return s.strip('_')


def read_roles(path):
    """The flat roles (ROLES) of one .ask file, each an uppercase hex word of 6
    or 8 digits, the file's first occurrence of each role, plus the one nested
    role under the key 'StandardVuMeter/Maximum' (NESTED_ELEMENT /
    NESTED_ROLE: the first `Maximum` inside the first `<StandardVuMeter>`)."""
    with open(path, 'r', encoding='utf-8') as f:
        text = f.read()
    found = {}
    for name, word in ROLE_RE.findall(text):
        if name in ROLES and name not in found:
            found[name] = word.upper()
    missing = [r for r in ROLES if r not in found]
    if missing:
        fail('%s: missing role(s) %s' % (path, ', '.join(missing)))
    nested = NESTED_RE.search(text)
    nested_key = NESTED_ELEMENT + '/' + NESTED_ROLE
    if nested:
        for name, word in ROLE_RE.findall(nested.group(1)):
            if name == NESTED_ROLE:
                found[nested_key] = word.upper()
                break
    if nested_key not in found:
        fail('%s: missing nested role %s' % (path, nested_key))
    return found


def rgb(word):
    """A 6- or 8-digit hex word -> (r, g, b, alpha) with alpha in [0, 1]."""
    r = int(word[0:2], 16)
    g = int(word[2:4], 16)
    b = int(word[4:6], 16)
    a = int(word[6:8], 16) / 255.0 if len(word) == 8 else 1.0
    return (r, g, b, a)


def clamp_byte(x):
    return max(0, min(255, int(round(x))))


def over(top, ground):
    """`top` (with alpha) source-over the opaque `ground`, per channel."""
    tr, tg, tb, ta = top
    gr, gg, gb, _ = ground
    return (clamp_byte(tr * ta + gr * (1.0 - ta)),
            clamp_byte(tg * ta + gg * (1.0 - ta)),
            clamp_byte(tb * ta + gb * (1.0 - ta)))


def mix(a, b, t):
    """a + t · (b − a), per channel, rounded."""
    return tuple(clamp_byte(a[i] + t * (b[i] - a[i])) for i in range(3))


def scale(c, ratio):
    """c · ratio, per channel, rounded and clamped (the head's lit press fill)."""
    return tuple(clamp_byte(c[i] * ratio) for i in range(3))


def mean(c):
    return (c[0] + c[1] + c[2]) / 3.0


def srgb_to_linear(byte):
    x = byte / 255.0
    return x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4


def linear_to_srgb(x):
    """A linear channel, clipped to [0, 1], encoded to a float byte."""
    x = max(0.0, min(1.0, x))
    return (12.92 * x if x <= 0.0031308 else 1.055 * x ** (1.0 / 2.4) - 0.055) * 255.0


def cbrt(x):
    return math.copysign(abs(x) ** (1.0 / 3.0), x)


def oklch(c):
    """An opaque sRGB triple -> OKLCH (L in 0..100, C in percent, h in
    degrees), Björn Ottosson's matrices (the VARIANTS' SHADE)."""
    r, g, b = (srgb_to_linear(v) for v in c)
    l = cbrt(0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b)
    m = cbrt(0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b)
    s = cbrt(0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b)
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return L * 100.0, math.hypot(a, bb) * 100.0, math.degrees(math.atan2(bb, a)) % 360.0


def from_oklch(L, C, h):
    """OKLCH (oklch's units) -> an sRGB triple, each linear channel clipped,
    encoded, rounded and clamped."""
    L /= 100.0
    C /= 100.0
    a = C * math.cos(math.radians(h))
    b = C * math.sin(math.radians(h))
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    lin = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
    return tuple(clamp_byte(linear_to_srgb(x)) for x in lin)


def shade(word6, lightness, hue):
    """THE VARIANTS' SHADE: `word6` with its OKLCH lightness set to
    `lightness`, its hue to `hue`, its chroma scaled by lightness / its own;
    an uppercase 6-digit hex word."""
    L0, C0, _ = oklch(opaque(word6))
    if L0 == 0.0:
        fail('shade: %s is black, its chroma has no ratio' % word6)
    r, g, b = from_oklch(lightness, C0 * lightness / L0, hue)
    return '%02X%02X%02X' % (r, g, b)


def word(c):
    return '0x%02X%02X%02X' % (c[0], c[1], c[2])


def opaque(word6):
    r, g, b, _ = rgb(word6)
    return (r, g, b)


def derive(roles):
    """One theme's GuiThemeTones, in MEMBERS' order (the head's DERIVATION)."""
    ground   = opaque(roles['SurfaceArea'])
    desktop  = opaque(roles['Desktop'])
    face     = opaque(roles['SurfaceBackground'])
    contrast = opaque(roles['ControlContrastFrame'])
    text     = opaque(roles['ControlForeground'])
    dim      = opaque(roles['TextDisabled'])
    clip     = opaque(roles['BackgroundClip'])
    on       = opaque(roles['ViewCheckControlEnabledOn'])
    range_   = opaque(roles['RangeDefault'])
    standby  = opaque(roles['StandbySelectionBackground'])
    sel_bg   = opaque(roles['SelectionBackground'])
    sel_fg   = opaque(roles['SelectionForeground'])
    alert    = opaque(roles['Alert'])
    door     = opaque(roles['ScaleAwareness'])
    vu_max   = opaque(roles[NESTED_ELEMENT + '/' + NESTED_ROLE])
    lvl_aud  = opaque(roles['RangeEditField'])
    lvl_noi  = opaque(roles['ChosenPreListen'])
    detail   = rgb(roles['DetailViewBackground'])
    ink      = over(rgb(roles['WaveformColor']), detail)
    canvas   = over(rgb(roles['DetailGridTiles']), detail)
    white    = (255, 255, 255)
    if mean(face) == 0.0:
        fail('SurfaceBackground is black: the lit press fill has no ratio')
    press_ratio = mean(desktop) / mean(face)
    t = {
        'ground':          ground,
        'text':            text,
        'text_dim':        dim,
        'text_on_lit':     contrast,
        'key_dim_ink':     mix(face, dim, 36.0 / 63.0),
        'key_face':        face,
        'lit_fill':        on,
        'lit_press_fill':  scale(on, press_ratio),
        'door_lit_fill':   door,
        'door_lit_press_fill': scale(door, press_ratio),
        'key_press_face':  desktop,
        'well_frame':      dim,
        'ruler_face':      face,
        'ruler_tick':      mix(face, text, 53.0 / 127.0),
        'lane_face':       desktop,
        'trim_bar_face':   desktop,
        'trim_span_face':  mix(face, text, 52.0 / 127.0),
        'selection_fill':  standby,
        'selection_text':  contrast,
        'list_press_fill': sel_bg,
        'list_press_text': sel_fg,
        'pane_face':       face,
        'score_pane_face': ink,
        'card_face':       alert,
        'card_ink':        desktop,
        'clip_lamp':       vu_max,
        'level_audio_fill': lvl_aud,
        'level_noise_fill': lvl_noi,
        'waveform_canvas': canvas,
        'waveform_ink':    ink,
        'waveform_core':   mix(ink, canvas, 5.0 / 32.0),
        'grid':            mix(canvas, ink, 1.0 / 4.0),
        'center':          ink,
        'cue':             on,
        'cue_outline':     contrast,
        'inactive_cue':    clip,
        'playhead_head':   range_,
        'playhead_line':   mix(white, range_, 0.483),
        'scanner':         white,
    }
    return [t[m] for m, _ in MEMBERS]


def walk(ask_dir):
    """(builtin rows, extra rows), each row (slug, group, author, file name,
    tones); the head's WALK and ORDER."""
    if not os.path.isdir(ask_dir):
        fail('not a folder: ' + ask_dir)
    builtin = []
    extra = []
    source_roles = {}
    for entry in sorted(os.listdir(ask_dir)):
        path = os.path.join(ask_dir, entry)
        if os.path.isfile(path) and entry.endswith('.ask'):
            stem = entry[:-4]
            if stem not in BUILTIN_FILES:
                continue
            slug = BUILTIN_FILES[stem] or slugify(stem)
            roles = read_roles(path)
            source_roles[stem] = roles
            builtin.append((slug, 'Builtin', BUILTIN_AUTHOR, entry, derive(roles)))
        elif os.path.isdir(path):
            if entry not in AUTHORS:
                fail('unknown author folder %s (add it to AUTHORS)' % path)
            author = AUTHORS[entry]
            for root, dirs, files in os.walk(path):
                dirs[:] = sorted(d for d in dirs if d != '__MACOSX')
                for name in sorted(files):
                    if not name.endswith('.ask') or name.startswith('._'):
                        continue
                    fpath = os.path.join(root, name)
                    slug = slugify(name[:-4])
                    extra.append((slug, 'Extra', author, name, derive(read_roles(fpath))))
    taken = {r[0] for r in builtin}
    for stem, slug in BUILTIN_FILES.items():
        want = slug or slugify(stem)
        if want not in taken:
            fail('builtin file missing: %s.ask' % stem)
    for slug, (stem, swaps, shades) in VARIANTS.items():
        if stem not in BUILTIN_FILES:
            fail('variant %s: source %s is not a builtin file' % (slug, stem))
        roles = dict(source_roles[stem])
        notes = []
        for r in shades:
            if r not in swaps.values():
                fail('variant %s: shaded role %s is not one the variant reads' % (slug, r))
        for replaced, replacement in swaps.items():
            for r in (replaced, replacement):
                if r not in ROLES:
                    fail('variant %s: role %s is not read (add it to ROLES)' % (slug, r))
            if replacement in shades:
                lightness, hue = shades[replacement]
                roles[replaced] = shade(roles[replacement], lightness, hue)
                notes.append('%s at OKLCH L %g h %g as %s'
                             % (replacement, lightness, hue, replaced))
            else:
                roles[replaced] = roles[replacement]
                notes.append('%s as %s' % (replacement, replaced))
        fname = '%s.ask (variant: %s)' % (stem, ', '.join(notes))
        builtin.append((slug, 'Builtin', BUILTIN_AUTHOR, fname, derive(roles)))
    builtin.sort(key=lambda r: (r[0] != 'default', r[0]))
    extra.sort(key=lambda r: r[0])
    seen = {}
    for r in builtin + extra:
        if r[0] in seen:
            fail('duplicate slug %s (%s and %s)' % (r[0], seen[r[0]], r[3]))
        seen[r[0]] = r[3]
    return builtin, extra


def derivation_block():
    """The head's DERIVATION table, as the header's comment repeats it."""
    doc = __doc__
    start = doc.index('  ROLE-NAMED')
    end = doc.index('Where a theme makes')
    return doc[start:end].rstrip('\n')


def emit(rows):
    lines = []
    w = lines.append
    w('#pragma once')
    w('')
    w('// GENERATED by tools/themes/ask_to_tables.py — NEVER HAND-EDITED. Rerun the')
    w('// tool to regenerate (its head owns the walk, the slugs and the derivation).')
    w('//')
    w('// THE THEME TABLES (architect 2026-11-05, the theme arc): every sRGB word the')
    w('// GUI paints, one row per theme, the numbers Ableton Live 12\'s own theme')
    w('// files\' (.ask) through the derivation below — the stock themes Ableton AG\'s')
    w('// (Live 12\'s Resources/Themes), the extras their authors\' (THIRD_PARTY.md,')
    w('// "The themes"); the files themselves are not in the repository. The')
    w('// `theme` device key names a row by its slug (device_config.h), the')
    w('// Settings page lists the slugs in this order under BUILTIN and EXTRA, and')
    w('// install_palette (render.cpp) fills GuiPalette off the named row, so every')
    w('// painter reads palette() and no painter knows a theme. The Default row is')
    w('// the measured palette of 2026-11-01..05 to the byte (palette_file.h\'s')
    w('// static_asserts, the proof).')
    w('//')
    w('// THE DERIVATION (our Default tone = Live\'s role; the tool\'s head):')
    for line in derivation_block().split('\n'):
        w(('// ' + line).rstrip())
    w('')
    w('#include <cstddef>')
    w('#include <cstdint>')
    w('#include <string_view>')
    w('')
    w('// Every sRGB word the GUI paints, one theme\'s (0xRRGGBB). Beside each member')
    w('// what reads it (the one owners: materials.h\'s head, palette_file.h\'s role')
    w('// list, render.h\'s palette block).')
    w('struct GuiThemeTones {')
    width = max(len(m) for m, _ in MEMBERS)
    for m, what in MEMBERS:
        w('    uint32_t %s;%s // %s' % (m, ' ' * (width - len(m)), what))
    w('};')
    w('')
    w('// THE GROUP the Settings page\'s value list shows a theme under: BUILTIN,')
    w('// Live\'s stock themes and the generator\'s VARIANTS of them')
    w('// (immaterial_gold); EXTRA, the .ask files he drops into the ask folder\'s')
    w('// author subfolders.')
    w('enum class GuiThemeGroup { Builtin, Extra };')
    w('')
    w('struct GuiTheme {')
    w('    const char*   slug;     // the config\'s spelling and the value list\'s row')
    w('    GuiThemeGroup group;')
    w('    const char*   author;   // the credit (THIRD_PARTY.md, "The themes")')
    w('    GuiThemeTones tones;')
    w('};')
    w('')
    w('// THE ROWS, `default` first, then the builtins by slug, then the extras by')
    w('// slug — the value list\'s order. Each row\'s trailing comment names its file.')
    w('inline constexpr GuiTheme kGuiThemes[] = {')
    for slug, group, author, fname, tones in rows:
        w('    {"%s", GuiThemeGroup::%s, "%s",   // %s' % (slug, group, author[0], fname))
        w('     {')
        for (m, _), c in zip(MEMBERS, tones):
            w('         %s,%s // %s' % (word(c), ' ' * (0), m))
        w('     }},')
    w('};')
    w('inline constexpr std::size_t kGuiThemeCount = sizeof(kGuiThemes) / sizeof(kGuiThemes[0]);')
    w('')
    w('// `default`, the templates\' and the construction state\'s row (device_config.h).')
    w('inline constexpr std::size_t kGuiDefaultTheme = 0;')
    w('static_assert(std::string_view(kGuiThemes[kGuiDefaultTheme].slug) == "default");')
    w('')
    w('// THE ONE GRAMMAR OWNER of the `theme` key: the row whose slug is `slug`, or')
    w('// nullptr when no theme bears it (the reader\'s hard fail, the editor\'s')
    w('// refusal; device_config.h\'s kThemeGrammarReason).')
    constexpr_find = [
        'constexpr const GuiTheme* find_gui_theme(std::string_view slug) {',
        '    for (const GuiTheme& t : kGuiThemes)',
        '        if (slug == t.slug) return &t;',
        '    return nullptr;',
        '}',
    ]
    for l in constexpr_find:
        w(l)
    w('')
    w('// No two rows share a slug (the tool refuses a duplicate; this pins it).')
    w('constexpr bool gui_theme_slugs_unique() {')
    w('    for (std::size_t i = 0; i < kGuiThemeCount; ++i)')
    w('        for (std::size_t j = i + 1; j < kGuiThemeCount; ++j)')
    w('            if (std::string_view(kGuiThemes[i].slug) == kGuiThemes[j].slug)')
    w('                return false;')
    w('    return true;')
    w('}')
    w('static_assert(gui_theme_slugs_unique());')
    return '\n'.join(lines) + '\n'


def credits(rows):
    """The credit lines THIRD_PARTY.md's "The themes" section carries: each
    file once — a variant adds no file (VARIANTS), THIRD_PARTY.md's prose
    names it."""
    by_author = {}
    for slug, group, author, fname, _ in rows:
        if slug in VARIANTS:
            continue
        by_author.setdefault(author, []).append(fname[:-4])
    out = []
    for author, names in by_author.items():
        out.append('%s (%s): %s' % (author[0], author[1], ', '.join(names)))
    return out


def main(argv):
    ask_dir = argv[1] if len(argv) > 1 else DEFAULT_ASK_DIR
    if len(argv) > 2:
        fail('usage: ask_to_tables.py [<ask folder>]')
    builtin, extra = walk(ask_dir)
    rows = builtin + extra
    text = emit(rows)
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(text)
    print('wrote %s: %d themes (%d builtin, %d extra)' % (OUT, len(rows), len(builtin), len(extra)))
    print()
    print('%-22s %-8s %-12s %s' % ('slug', 'group', 'author', 'file'))
    for slug, group, author, fname, _ in rows:
        print('%-22s %-8s %-12s %s' % (slug, group, author[0], fname))
    print()
    print('THIRD_PARTY.md credits:')
    for line in credits(rows):
        print('  ' + line)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
