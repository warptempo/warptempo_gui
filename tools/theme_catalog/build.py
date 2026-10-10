#!/usr/bin/env python3
# tools/theme_catalog/build.py — the fetched sources (fetch.py) -> docs/themes/catalog.json: every entry's recorded
# bytes with their provenance (a fetched file, sources.py SOURCES; or a file of a disc image on the build host,
# LOCAL_SOURCES: the squeeze image's Clearlooks), the values its own toolkit computed at import (toolkit_rules.py, the
# rule named), and
# its catalog roles (roles.py), the family rule its flags take (flag_rule) and its display tier (display_tier). THE CATALOG
# RECORDS IMPORTED THEMES ONLY, NO DERIVATION (architect 2026-10-03): nothing here invents a colour; a role a source has
# no word for stays absent. THE CATALOG IS A RECORD, NOT A SHIPPED SET (architect 2026-10-08): no theme file ships; the
# app compiles in the chrome's own theme, Windows 2000's, which the catalog checks, and every entry's twelve scheme keys
# as a built-in scheme (gen_theme_files.py). THE CLEARLOOKS ENGINE'S TONES ARE NOT COMPUTED HERE since the clearlooks
# chrome's removal (architect 2026-10-09 ~21:20): that arithmetic (engine_tones and its helpers) stands in git history
# at d5b91f52^ — the note above the checks below.
# THE CATALOG IS THE CHROME'S ALONE (architect 2026-10-07): the program's colors are the palette's, compiled in
# (src/gui/palette_file.h), and the program's own family of chosen entries (the presets of his retired picker tool)
# left the catalog the same day ("it'll still be in the git history").
# THE PRODUCT'S OWN SCHEMES, THE ONE DESIGNED FAMILY (architect 2026-10-10 ~06:30: "I have created a theme called Cool
# Edit Pro for the Chrome … make it a permanent part of the hard-coded as one of the options alongside Windows 2000
# Standard"): the `warptempo` family — a chrome scheme the architect picked in the app and saved through the picker,
# made a built-in — is recorded as his file spells it, its relief by Windows' Appearance-dialog rule run at import
# (warptempo_entries); it leads the catalog (FAMILIES), so the generated schemes lead with it.
# NOT IMPORTED (architect 2026-10-03, late; NOT_IMPORTED below, each with its reason, recorded in the catalog): the
# schemes no independent source records as Windows', the usability schemes, the KDE schemes KDE 3.5 did not ship, and
# the role-identical duplicates. The
# checks run before the write; the last lines report each family: entries, corroborated, sources.
#
#   python3 tools/theme_catalog/build.py
#   python3 tools/theme_catalog/build.py --check-only
#
# --check-only (2026-10-07, replacing the retired --presets-only) is the road for a host that cannot reach the pinned
# sources (the cloud: the Trinity mirror is outside its egress; the squeeze image off the laptop): it runs the checks
# on the committed catalog.json and writes nothing.
import hashlib, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sources import SOURCES, REPO, local_path, provenance, LOCAL_SOURCES, local_file, local_provenance
from parse_windows import parse_hivedef, parse_hive_colors, parse_hive_schemes, parse_win95_schemes, parse_theme
from parse_windows import parse_hive_scheme_fonts, parse_win95_scheme_fonts, parse_theme_menu_font, read_png_rgb
from parse_kde import parse_kcsrc
from parse_cde import parse_dp
from parse_gtkrc import parse_gtkrc, gtkrc_color, expr_text, parse_metacity, metacity_color, frame_piece, draw_ops_flat
import toolkit_rules as T
from roles import ROLES, MAPPING, map_roles

OUT = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
FAMILIES = ('warptempo', 'windows', 'windows-plus', 'kde3', 'cde', 'gnome2')
# a key's prefix per family; GNOME 2's keys are the GTK theme's own name, lowercase (`clearlooks`; architect
# 2026-10-07: the compiled Clearlooks theme), and the product's own the scheme's name, lowercase (`cool-edit-pro-me`)
KEY_PREFIX = {'warptempo': None, 'windows': 'windows', 'windows-plus': 'plus', 'kde3': 'kde3', 'cde': 'cde',
              'gnome2': None}
# THE FLAGS' RULE per family (architect 2026-10-03, late: a flag's one-line bevel is its theme family's own rule on the
# flag's face, toolkit_rules.flag_bevel): Windows' Appearance dialog for the Windows families and the program's own
# ("take Windows' rule"), KDE 3's at the scheme's contrast, Motif's for CDE, FLAT for GNOME 2 (Clearlooks draws no
# one-line bevel round a raised face: toolkit_rules.flag_bevel states why); the product's own schemes Windows', the
# chrome they are made under.
FLAG_RULE = {'warptempo': 'windows-dialog', 'windows': 'windows-dialog', 'windows-plus': 'windows-dialog',
             'kde3': 'kde3', 'cde': 'motif', 'gnome2': 'flat'}

# NOT IMPORTED (architect 2026-10-03, late), each group with its reason; build.py asserts every named scheme exists in
# its source and, for a duplicate, that its roles equal its twin's, so a re-pinned source cannot change the list
# silently. ReactOS's own schemes are every hivedef.inf scheme no second source corroborates (windows_entries).
REACTOS_ONLY = {
    'Green Olive': ('windows-spruce', 'role-identical to Windows Spruce under another name'),
    'Sand': ('windows-desert', 'role-identical to Windows Desert under another name'),
    'Sky': (None, 'a ReactOS scheme; no independent source records it as Windows\''),
    'High Contrast 1': (None, 'a usability scheme'), 'High Contrast 2': (None, 'a usability scheme'),
    'High Contrast Black': (None, 'a usability scheme'), 'High Contrast White': (None, 'a usability scheme'),
}
KDE_USABILITY = ('High Contrast Black Text', 'High Contrast White Text', 'High Contrast Yellow on Blue')
# THE KDE CATALOG IS WHAT KDE 3.5 SHIPPED (architect 2026-10-03, late): the 21 schemes added later are not imported --
# the six and the fifteen Trinity took from opendesktop.org into tdebase (the commits found through the Trinity gitea
# API, repos/TDE/tdebase/commits?path=kcontrol/krdb/kcs/<file>). Keyed by source and manifest file; build.py asserts
# each is found. (The Q4OS 6.9 TDE image was a second KDE source until 2026-10-03, adding six schemes KDE 3.5 did not
# ship either, one of them a duplicate; no entry depended on it, and the source was dropped.)
TDE_2023 = ('tdebase commit 688aa0fc28d3de8665b7b151eba65fe49e02187f (2023-10-18, "Add six new color schemes taken from '
            'https://www.opendesktop.org."): added by Trinity, not shipped by KDE 3.5')
TDE_2025 = ('tdebase commit 69ac490a9e43b46efbc7fbf3ce32e96365f5805d (2025-01-24, "Add 15 color schemes taken from '
            'https://www.opendesktop.org."): added by Trinity, not shipped by KDE 3.5')
KDE_NOT_35 = {
    'tde_kcs': {f'kcontrol/krdb/kcs/{n}.kcsrc': TDE_2023 for n in ('Human', 'Last.fm', 'Lizard', 'Platinum', 'Sienna', 'WedgieWeb')}
               | {f'kcontrol/krdb/kcs/{n}.kcsrc': TDE_2025 for n in (
                   'Different', 'Jewels-Amethyst', 'Jewels-Aquamarine', 'Jewels-Carbon', 'Jewels-Citrin', 'Jewels-Emerald',
                   'Jewels-Ruby', 'Jewels-Sapphire', 'Jewels-Topaz', 'Lila', 'Pinkie', 'Seasons-Autumn', 'Seasons-Spring',
                   'Seasons-Summer', 'Seasons-Winter')},
}
KDE3_ENTRIES = 25       # KDE 3.5's own schemes, less its three usability schemes
DUPLICATES = {'cde-broica': 'cde-default'}   # key: the twin it repeats

# THE WINDOWS 95 CD'S APPEARANCE SCHEMES (architect 2026-10-09: "the Windows 95 flavour of the schemes whose bytes differ
# from Windows 2000's, under a windows-95- prefix, imported from the CD itself"): shell2.inf carries 27 (sources.py
# win95_shell2inf). build.py accounts for every one, so a re-pinned source cannot change the list silently:
#   WIN95_DIFFERENT: the six whose bytes differ from their Windows 2000 twin (the windows-family entry the ReactOS, XP and
#     Windows 2000 records agree on) -> the CD's name: each an entry of its own, windows-95-<name>, its twin's key here;
#   WIN95_SAME: the ten byte-equal on all 25 COLOR_* values to their twin (and Windows Standard to windows-95-standard):
#     no second entry for the same bytes;
#   WIN95_NOT_ENTRIES: the rest, with the reason the catalog does not carry them.
WIN95_DIFFERENT = {'Maple': 'windows-maple', 'Wheat': 'windows-wheat', 'Marine (high color)': 'windows-marine',
                   'Storm (VGA)': 'windows-storm', 'Rose': 'windows-rose', 'Plum (high color)': 'windows-plum'}
WIN95_SAME = {'Windows Standard': 'windows-95-standard', 'Brick': 'windows-brick', 'Spruce': 'windows-spruce',
              'Teal (VGA)': 'windows-teal', 'Red, White, and Blue (VGA)': 'windows-red-white-and-blue',
              'Pumpkin (large)': 'windows-pumpkin', 'Eggplant': 'windows-eggplant', 'Rainy Day': 'windows-rainy-day',
              'Desert': 'windows-desert', 'Lilac': 'windows-lilac', 'Slate': 'windows-slate'}
WIN95_NOT_ENTRIES = {
    'Windows Standard (large)': 'role-identical to Windows Standard on all 25 values (a size variant)',
    'Windows Standard (extra large)': 'role-identical to Windows Standard on all 25 values (a size variant)',
    'Lilac (large)': 'role-identical to Lilac on all 25 values (a size variant)',
    'Rose (large)': 'a size variant of Rose that differs from it on 13 values (Windows 2000\'s hive carries it too); '
                    'the catalog\'s Windows Rose is the plain scheme',
    'High Contrast Black': 'a usability scheme', 'High Contrast Black (large)': 'a usability scheme',
    'High Contrast Black (extra large)': 'a usability scheme', 'High Contrast White': 'a usability scheme',
    'High Contrast White (large)': 'a usability scheme', 'High Contrast White (extra large)': 'a usability scheme',
}
WIN95_SHA256 = LOCAL_SOURCES['win95_shell2inf']['files']['shell2.inf'][1]

# THE DISPLAY TIER (architect 2026-10-03, late): each entry is tagged by the smallest period colour set holding every
# colour its roles use (display_tier). `vga`: the 16 colours of the VGA / Windows 16-colour palette. `windows-20`: those
# plus the four static colours Windows reserves beside them in the system palette of a 256-colour display (money green,
# sky blue, cream, medium grey), so always solid there, never dithered. Else `high-colour`. In list order, the tiers.
VGA16 = ('#000000', '#800000', '#008000', '#808000', '#000080', '#800080', '#008080', '#C0C0C0',
         '#808080', '#FF0000', '#00FF00', '#FFFF00', '#0000FF', '#FF00FF', '#00FFFF', '#FFFFFF')
WINDOWS_STATIC_EXTRAS = ('#C0DCC0', '#A6CAF0', '#FFFBF0', '#A0A0A4')
DISPLAY_TIERS = (('vga', frozenset(VGA16)), ('windows-20', frozenset(VGA16 + WINDOWS_STATIC_EXTRAS)), ('high-colour', None))


def hx(c): return '#%02X%02X%02X' % tuple(c)
def unhex(s): return tuple(int(s[i:i + 2], 16) for i in (1, 3, 5))


def manifest(src):
    p = os.path.join(REPO, 'tmp', 'theme_sources', src, 'MANIFEST.json')
    if not os.path.exists(p): raise SystemExit(f'build: {p} is missing; run tools/theme_catalog/fetch.py first')
    return json.load(open(p))['files']


def slug(text):
    """A key's words: lowercase ASCII, apostrophes dropped, every other run of non-alphanumerics one hyphen."""
    s = re.sub(r"'", '', text.lower())
    return re.sub(r'[^a-z0-9]+', '-', s).strip('-')


def camel_words(stem):
    """'NorthernSky' -> 'Northern Sky', 'PBNJ' -> 'PBNJ' (a CDE palette's file stem into words for its key)."""
    return re.sub(r'(?<=[a-z])(?=[A-Z])', ' ', stem)


def display_tier(roles):
    """An entry's roles -> its display tier, the first of DISPLAY_TIERS whose set holds every role's colour."""
    used = set(roles.values())
    return next(t for t, cs in DISPLAY_TIERS if cs is None or used <= cs)


def entry(family, key_words, name, prov, raw, computed=None, notes=None, imitates=None, rule=None, flag_rule=None):
    pre, words = KEY_PREFIX[family], slug(key_words)
    if pre and words.startswith(pre + '-'): words = words[len(pre) + 1:]     # 'Windows Classic' -> windows-classic
    key = f'{pre}-{words}' if pre else words
    raw_hex = {k: hx(v) for k, v in raw.items()}
    values = dict(raw_hex); values.update({k: hx(v) for k, v in (computed or {}).items()})
    e = {'key': key, 'name': name, 'family': family}
    if imitates: e['imitates'] = imitates
    e['provenance'] = {'sources': prov if isinstance(prov, list) else [prov]}
    if rule:     # the toolkit rule run at import: its id (the catalog's 'rules'), its parameters, what it computed
        e['provenance']['rule'] = dict(rule)
        e['provenance']['rule']['computed'] = {k: hx(v) for k, v in computed.items()}
    e['raw'] = raw_hex
    e['roles'] = map_roles(family, values)
    e['flag_rule'] = flag_rule or {'id': FLAG_RULE[family]}
    e['display_tier'] = display_tier(e['roles'])
    e['notes'] = notes or []
    return e


# ------------------------------------------------------------------ the product's own (warptempo)
# THE PRODUCT'S OWN SCHEMES (architect 2026-10-10 ~06:30, the head): each the architect's scheme file as the picker's
# Save As wrote it, its twelve chrome keys (kGuiChromeLines, src/gui/palette_file.h) recorded VERBATIM as `raw`, under
# the file's own key names; the relief quartet Windows' Appearance-dialog rule on its ground (toolkit_rules.
# windows_dialog, the `windows-dialog` rule), the derivation the app runs on any scheme that carries keys under the
# windows-2000 chrome (src/gui/chrome_derive.h), so the record is what the screen shows. No sha256 pins the file: the
# twelve lines are the record, kept in the entry's provenance as the file spells them.
# COOL EDIT PRO ME (his file of 2026-10-10 ~08:40, saved on the tablet, replacing his ~06:50 file of the same name,
# which replaced his nine-key "Cool Edit Pro" of ~05:46): Windows Me Standard's caption, inactive caption and selection
# fill (windows-me-standard's bytes) under Cool Edit Pro 2.1's panel tones, as his Wine captures measure them
# (tmp/research/cool_edit/METRICS.md): the ground its PANEL FACE 626C7B (its "Dockable Window 3D Color" default, §1,
# the palette's `face` role) since ~08:45 — "using a lighter chrome makes that line … stand out as a different shade":
# the frame's Shadow line under the menu row then reads as its own shade against Cool Edit's darker band line beneath
# it, where the toolbar RECESS 4E5662 of ~06:50 (§1.1) made "two of the same line above the top row" — the field its
# pane's bottom mid line (§1.1), the text, the selection text and the field text its text white (the tab labels and the
# time field's digits, §4.4).
WARPTEMPO_PROJECT = ('Warptempo: the architect\'s own chrome scheme, picked in the app\'s color picker and saved by its '
                     'Save As (src/gui/color_picker.h)')
WARPTEMPO_SCHEMES = (
    ('Cool Edit Pro ME', 'the tablet, 2026-10-10 ~08:40', (
        ('chrome_ground', '#626C7B'), ('chrome_text', '#EFF0F0'), ('chrome_title_start', '#0A246A'),
        ('chrome_title_end', '#A6CAF0'), ('chrome_title_text', '#FFFFFF'), ('chrome_inactive_title_start', '#808080'),
        ('chrome_inactive_title_end', '#C0C0C0'), ('chrome_inactive_title_text', '#D4D0C8'),
        ('chrome_selection', '#0A246A'), ('chrome_selection_text', '#EFF0F0'), ('chrome_field', '#414751'),
        ('chrome_field_text', '#EFF0F0')), [
        'the architect\'s scheme file, its twelve keys verbatim (architect 2026-10-10 ~06:30: "make it a permanent '
        'part of the hard-coded as one of the options alongside Windows 2000 Standard")',
        'the caption\'s six and the selection fill are Windows Me Standard\'s bytes (windows-me-standard); the ground '
        '#626C7B is Cool Edit Pro 2.1\'s panel face (its "Dockable Window 3D Color" default, the palette\'s face role), '
        'the field #414751 its pane\'s bottom mid line, the text, the selection text and the field text #EFF0F0 its '
        'text white (his Wine captures, tmp/research/cool_edit/METRICS.md sections 1, 1.1 and 4.4)',
        'the ground is the panel face since 2026-10-10 ~08:45 (his file of ~08:40), the toolbar recess tone #4E5662 '
        'that morning (his file of ~06:50): "using a lighter chrome makes that line ... stand out as a different '
        'shade" -- the frame\'s Shadow line under the menu row against Cool Edit\'s darker band line beneath it',
        'the relief quartet is Windows\' Appearance-dialog rule on the ground, as the app derives it under the '
        'windows-2000 chrome (src/gui/chrome_derive.h): the scheme records no 3D colours']),
)
WARPTEMPO_DIALOG_KEYS = ('windows-dialog:hilight', 'windows-dialog:light', 'windows-dialog:shadow',
                         'windows-dialog:dkshadow')


def warptempo_entries():
    """The product's own schemes (the rule above), in WARPTEMPO_SCHEMES' order."""
    out = []
    for name, saved, keys, notes in WARPTEMPO_SCHEMES:
        raw = {k: unhex(v) for k, v in keys}
        computed = dict(zip(WARPTEMPO_DIALOG_KEYS, T.windows_dialog(raw['chrome_ground'])))
        prov = {'project': WARPTEMPO_PROJECT, 'file': f'schemes/{name}.scheme', 'saved': saved,
                'recorded': [f'{k}={v}' for k, v in keys]}
        out.append(entry('warptempo', name, name, prov, raw, computed, notes=notes, rule={'id': 'windows-dialog'}))
        out[-1]['corroborated'] = 0
    return out


# ------------------------------------------------------------------ Windows (windows), windows-plus
def xp_name(n):
    """An XP .theme DisplayName as ReactOS spells the scheme: the (VGA) / (high color) tags dropped, classicthemes8's
    'Red, Blue & White' as Windows' 'Red, White, and Blue'."""
    n = re.sub(r' \((VGA|high color)\)$', '', n)
    return 'Red, White, and Blue' if n == 'Red, Blue & White' else n


def win95_file():
    """The extracted shell2.inf (sources.py LOCAL_SOURCES win95_shell2inf), its sha256 checked: a different byte is a
    hard fail."""
    p = local_file('win95_shell2inf', 'shell2.inf')
    if not os.path.exists(p):
        raise SystemExit(f'build: {p} is missing; extract it from the Windows 95 image (sources.py LOCAL_SOURCES)')
    got = hashlib.sha256(open(p, 'rb').read()).hexdigest()
    if got != WIN95_SHA256: raise SystemExit(f'build: {p} sha256 {got}, pinned {WIN95_SHA256}')
    return p


# THE SCHEME'S FONT RECORD (2026-10-09; THE RECORD ALONE since ~21:20, when the architect retired the scheme's face —
# "the scheme's default font should stop being honored — it should only be honored from the font picker": the app's
# face is its `font` device key, src/gui/gui_font.h, and gen_theme_files.py reads no font): every Windows-family
# entry's `font` record is READ FROM ITS OWN SOURCE — the menu font its scheme records (parse_windows.py's MENU FONTS
# block: the face of the chrome's body text) — and classed by the product's two Windows faces: `tahoma` where the
# source names Tahoma; `ms-sans-serif` where it names MS Sans Serif or its TrueType twin Microsoft Sans Serif, AND FOR
# EVERY OTHER FACE (Arial, Times New Roman, the Plus! themes' display faces). The record keeps the face the source
# named and where it was read. Windows 2000's own entries read Windows 2000's hive (parse_hive_scheme_fonts), the
# Windows 95 CD's its shell2.inf, the Plus! themes their .theme's NonclientMetrics, windows-me-standard the captures
# (the cap measured, winme_captures). No source of the family lacks a font, so no era rule is needed; a family whose
# source names no Windows font (KDE 3, CDE, GNOME 2) carries no record.
def font_record(named, where):
    return {'face': 'tahoma' if named == 'Tahoma' else 'ms-sans-serif', 'named': named, 'from': where}


# THE WINDOWS ME CAPTURES (sources.py winme_captures; architect 2026-10-09): one capture px is one Windows px, WordPad at
# identical metrics under both systems. WINME_SAMPLES: a pixel per value the captures show, in Windows' key names —
# the menu bar's ground, the frame's outer light column, its inner and outer dark right columns, the caption's first
# and last gradient columns. WINME_TITLE_BOX: the caption rows and the columns of the title's first capital ("S" of
# SCRIPT, "D" of Document), whose white rows are the bold face's cap.
WINME_SAMPLES = {'ButtonFace': (300, 30), 'ButtonHilight': (1, 200), 'ButtonShadow': (598, 200),
                 'ButtonDkShadow': (599, 200), 'ActiveTitle': (4, 4), 'GradientActiveTitle': (595, 4)}
WINME_TITLE_BOX = (range(3, 18), range(24, 32))


def winme_captures():
    """-> {file: rows}: the two captures, each sha256 checked (a different byte is a hard fail)."""
    out = {}
    for f, (_, want) in LOCAL_SOURCES['winme_captures']['files'].items():
        p = local_file('winme_captures', f)
        if not os.path.exists(p): raise SystemExit(f'build: {p} is missing (sources.py LOCAL_SOURCES winme_captures)')
        got = hashlib.sha256(open(p, 'rb').read()).hexdigest()
        if got != want: raise SystemExit(f'build: {p} sha256 {got}, pinned {want}')
        out[f] = read_png_rgb(p)[2]
    return out


def title_cap_rows(rows):
    """The number of caption rows holding the title's white in WINME_TITLE_BOX: its first capital's height."""
    ys, xs = WINME_TITLE_BOX
    return sum(1 for y in ys if any(rows[y][x] == (255, 255, 255) for x in xs))


def diff_keys(a, b):
    return sorted(k for k in set(a) & set(b) if a[k] != b[k])


def windows_entries():
    hv = 'boot/bootdata/hivedef.inf'
    ros = parse_hivedef(local_path('reactos', hv))
    seconds = []     # (source id, file, display name, colours)
    for src in ('xp_classic_zkedem', 'xp_classic_8'):
        for f in manifest(src):
            dn, cols = parse_theme(local_path(src, f))
            if dn is None: raise SystemExit(f'{local_path(src, f)}: no DisplayName')
            seconds.append((src, f, dn, cols))
    by_name = {}
    for src, f, dn, cols in seconds: by_name.setdefault(xp_name(dn), []).append((src, f, dn, cols))
    w98 = {os.path.basename(f)[:-len('.theme')]: (f, parse_theme(local_path('win98_themes', f))[1])
           for f in manifest('win98_themes')}
    role_keys = set(MAPPING['windows'].values())
    out = []

    def corroborate(name, primary, cands, notes):
        """Each second source that records the scheme with equal bytes on every role key corroborates it; every raw key
        on which a source differs is noted."""
        provs, n_ok = [], 0
        for src, f, dn, cols in cands:
            d = diff_keys(primary, cols)
            if set(d) & role_keys:
                notes.append(f'{src} {f} ("{dn}") differs on role keys {sorted(set(d) & role_keys)}: not counted')
                continue
            n_ok += 1; provs.append(provenance(src, f))
            if d: notes.append(f'{src} {f} ("{dn}") differs on ' + ', '.join(f'{k} {hx(cols[k])} (here {hx(primary[k])})' for k in d))
            if dn != name: notes.append(f'{src} {f} names it "{dn}"')
        return provs, n_ok

    # THE THREE RELEASES' DEFAULT SCHEMES (architect 2026-10-06, "just make it accurate"): one entry per distinct byte
    # set, keyed by the release whose default scheme the bytes are — windows-95-standard, windows-98-standard,
    # windows-2000-standard — every source's own label for the bytes in the entry's notes, as the source spells it.
    # ReactOS's "ReactOS Standard" and "ReactOS Classic" are Windows 2000's own "Windows Standard" and "Windows
    # Classic" (its hive's Appearance\Schemes names, byte-equal on every role key) under ReactOS names: folded into the
    # 2000 and 98 entries, not doubled. A hivedef.inf scheme no second source corroborates is NOT IMPORTED
    # (REACTOS_ONLY names each and why).
    folded = {'ReactOS Standard': 'Windows 2000 Standard', 'ReactOS Classic': 'Windows 98 Standard'}
    ros_only = {}
    for name, cols in ros.items():
        if name in folded: continue
        notes = []
        cands = by_name.get(name, [])
        provs, n_ok = corroborate(name, cols, cands, notes)
        if not n_ok: ros_only[name] = cols; continue
        out.append(entry('windows', name, name, [provenance('reactos', hv) | {'scheme': name}] + provs, cols, notes=notes))
        out[-1]['corroborated'] = n_ok
    if set(ros_only) != set(REACTOS_ONLY):
        raise SystemExit(f'build: the uncorroborated ReactOS schemes are {sorted(ros_only)}, not REACTOS_ONLY\'s {sorted(REACTOS_ONLY)}')

    # Windows schemes ReactOS lacks, from the two XP records (each corroborating the other).
    zk = {xp_name(dn): (f, dn, cols) for src, f, dn, cols in seconds if src == 'xp_classic_zkedem'}
    for name in ('Desert', 'Spruce'):
        f, dn, cols = zk[name]
        notes = []
        cands = [c for c in by_name.get(name, []) if c[0] != 'xp_classic_zkedem']
        provs, n_ok = corroborate(name, cols, cands, notes)
        out.append(entry('windows', name, name, [provenance('xp_classic_zkedem', f)] + provs, cols, notes=notes))
        out[-1]['corroborated'] = n_ok

    hv2k = 'I386/HIVEDEF.INF'
    w2k_schemes = parse_hive_schemes(local_path('win2000_hivedef', hv2k))
    w2k_fonts = parse_hive_scheme_fonts(local_path('win2000_hivedef', hv2k))

    def hive_font(name):
        r"""The font record of the Windows 2000 entry `name` off its hive's Appearance\Schemes value: the value whose
        name is `name` once its (VGA) / (high color) tag is dropped, else `name (large)` (the hive's only Pumpkin)."""
        hits = [n for n in w2k_fonts if xp_name(n) == name] or [n for n in w2k_fonts if n == name + ' (large)']
        if len(hits) != 1: raise SystemExit(f'build: Windows 2000\'s hive has {hits} for {name!r}')
        return font_record(w2k_fonts[hits[0]], f'lfMenuFont of Windows 2000\'s setup hive, Appearance\\Schemes '
                                               f'"{hits[0]}"')

    # the Windows 2000 schemes above (ReactOS's corroborated ones, Desert and Spruce): their hive's own fonts
    for e in out: e['font'] = hive_font(e['name'])

    def folded_in(name, cols, provs, notes):
        """The ReactOS scheme folded into `name`: -> its provenance appended, its label and any raw difference noted."""
        rname = {v: k for k, v in folded.items()}[name]
        d = diff_keys(cols, ros[rname])
        assert not set(d) & role_keys, (name, d)
        provs.append(provenance('reactos', hv) | {'scheme': rname})
        notes.append(f'ReactOS records it as "{rname}"' + ('' if not d else ', differing on ' + ', '.join(
            f'{k} {hx(ros[rname][k])} (here {hx(cols[k])})' for k in d)))

    def w2k_scheme(scheme, cols, provs, notes):
        r"""Windows 2000's own Appearance\Schemes value `scheme`: -> its provenance appended, its label noted."""
        d = diff_keys(cols, w2k_schemes[scheme])
        assert not set(d) & role_keys, (scheme, d)
        provs.append(provenance('win2000_hivedef', hv2k) | {'scheme': scheme})
        notes.append(f'Windows 2000\'s setup hive names it "{scheme}" (Appearance\\Schemes)' + ('' if not d else
                     ', differing on ' + ', '.join(f'{k} {hx(w2k_schemes[scheme][k])} (here {hx(cols[k])})' for k in d)))

    # WINDOWS 95 STANDARD and the six Windows 95 flavours: the Windows 95 OSR2 CD's own Appearance schemes (sources.py
    # win95_shell2inf; architect 2026-10-09, replacing the hand-recorded entry of 2026-10-03, whose bytes the CD's
    # "Windows Standard" equals on all 25 values). A scheme is 25 COLOR_* values (Windows 95 has no Gradient,
    # MenuHilight or MenuBar), its recorded bytes the colour array of the 492-byte value, kept in the provenance as the
    # INF spells them.
    w95 = parse_win95_schemes(win95_file())
    w95_fonts = parse_win95_scheme_fonts(win95_file())

    def w95_font(scheme):
        return font_record(w95_fonts[scheme], f'lfMenuFont of the Windows 95 CD\'s shell2.inf, Appearance\\Schemes '
                                              f'"{scheme}"')
    w95_prov = local_provenance('win95_shell2inf', 'shell2.inf')
    accounted = set(WIN95_DIFFERENT) | set(WIN95_SAME) | set(WIN95_NOT_ENTRIES)
    if set(w95) != accounted:
        raise SystemExit(f'build: the CD\'s schemes are {sorted(w95)}, not the accounted {sorted(accounted)}')

    def w95_source(scheme):
        return w95_prov | {'scheme': scheme, 'recorded': w95[scheme][1],
                           'layout': 'COLORREF[25], COLOR_SCROLLBAR .. COLOR_INFOBK: the last 100 bytes of the '
                                     '492-byte value, each R, G, B and a flag byte'}

    f98, c98 = w98['Windows Default']
    cols = w95['Windows Standard'][0]
    # the entry's bytes are what the hand-recorded entry held (the retail captures' DFDFDF 3DLight under Windows 98's
    # Windows Default.theme): the CD equals it on every one of the 25 values
    want = dict(c98); want['ButtonLight'] = (0xDF, 0xDF, 0xDF)
    assert cols == want, diff_keys(cols, want)
    assert (cols['ButtonFace'], cols['ButtonHilight'], cols['ButtonShadow'], cols['ButtonDkShadow']) == \
        ((192,) * 3, (255,) * 3, (128,) * 3, (0,) * 3)
    d98 = diff_keys(cols, c98)
    out.append(entry('windows', 'Windows 95 Standard', 'Windows 95 Standard', [w95_source('Windows Standard')], cols, notes=[
        'the CD names it "Windows Standard"; its "Windows Standard (large)" and "(extra large)" are the same 25 values',
        'the sources disagree on ButtonLight (COLOR_3DLIGHT): the CD records DFDFDF, as the Windows 95 retail screen '
        'captures (Toasty Tech: a window frame\'s outer top line) show it; Windows 98\'s Windows Default.theme and '
        'windows-98-standard\'s records (Windows 2000\'s and XP\'s "Windows Classic") record C0C0C0 (the face), and the '
        'early Windows 95 beta captures draw no 3DLight line',
        'Windows 98\'s Windows Default.theme records the same bytes but for ' + ', '.join(
            f'{k} {hx(c98[k])} (here {hx(cols[k])})' for k in d98) + '; a flat caption (no Gradient keys)']))
    out[-1]['corroborated'] = 0
    out[-1]['font'] = w95_font('Windows Standard')
    by_key_now = {e['key']: e for e in out}
    for name, twin in WIN95_SAME.items():     # the byte-equal ones: equal on all 25 values to their entry
        t = by_key_now[twin]['raw']
        got = {k: hx(v) for k, v in w95[name][0].items()}
        if got != {k: t[k] for k in got}: raise SystemExit(f'build: the CD\'s {name} is not equal to {twin}')
    for name in ('Windows Standard (large)', 'Windows Standard (extra large)', 'Lilac (large)'):
        twin = {'Lilac (large)': 'Lilac'}.get(name, 'Windows Standard')
        if w95[name][0] != w95[twin][0]: raise SystemExit(f'build: the CD\'s {name} is not equal to its {twin}')
    for name, twin in WIN95_DIFFERENT.items():
        cols = w95[name][0]
        t = by_key_now[twin]['raw']
        d = sorted(k for k in cols if hx(cols[k]) != t[k])
        assert d, name
        label = f'Windows 95 {xp_name(name)}'     # the (VGA) / (high color) tag dropped, as the Windows 2000 entries'
        out.append(entry('windows', label, label, [w95_source(name)], cols, notes=[
            f'the CD names it "{name}"; the Windows 2000 entry {twin} (ReactOS, XP and Windows 2000\'s own hive agree) '
            f'differs on ' + ', '.join(f'{k} {hx(cols[k])} (there {t[k]})' for k in d) + '; the other values equal it',
            'no Gradient, MenuHilight or MenuBar key (Windows 95 has none): a flat caption']))
        out[-1]['corroborated'] = 0
        out[-1]['font'] = w95_font(name)

    # WINDOWS 98 STANDARD: Windows 98's default scheme, the C0C0C0 face under the navy-to-#1084D0 gradient caption.
    # Its bytes are XP's saved scheme (zkedem's classic.theme, saved from WEPOS 2009's Display Properties, DisplayName
    # "Windows Classic"); Windows 2000's own hive records them byte for byte as its "Windows Classic" scheme, ReactOS as
    # "ReactOS Classic", and Windows 98's Windows Default.theme on every key it records but the desktop's Background
    # (its .theme carries no Gradient key). Role by role it is windows-95-standard but for the 3DLight (C0C0C0, the
    # face, against Windows 95's retail DFDFDF) and the caption's two gradient ends (#1084D0 and #B5B5B5, where
    # Windows 95 drew a flat caption).
    f, dn, cols = zk['Windows Classic']
    notes = [f'xp_classic_zkedem {f} names it "{dn}"']
    provs, n_ok = [], 0
    w2k_scheme('Windows Classic', cols, provs, notes); n_ok += 1
    folded_in('Windows 98 Standard', cols, provs, notes); n_ok += 1
    d = diff_keys(cols, c98)
    assert not set(d) & role_keys, d
    assert not any(k.startswith('Gradient') for k in c98), f98
    provs.append(provenance('win98_themes', f98)); n_ok += 1
    notes.append('Windows 98\'s Windows Default.theme (its default scheme) records the same bytes' + ('' if not d else
                 ', but for ' + ', '.join(f'{k} {hx(c98[k])} (here {hx(cols[k])})' for k in d)) +
                 ', and no Gradient key (the caption\'s two ends are Windows 2000\'s and XP\'s records of the scheme)')
    out.append(entry('windows', 'Windows 98 Standard', 'Windows 98 Standard', [provenance('xp_classic_zkedem', f)] + provs,
                     cols, notes=notes))
    out[-1]['corroborated'] = n_ok
    # its face: Windows 2000's record of the scheme ("Windows Classic"), which Windows 98's Windows Default.theme agrees
    # with (its NonclientMetrics' menu font MS Sans Serif)
    out[-1]['font'] = hive_font('Windows Classic')
    assert parse_theme_menu_font(local_path('win98_themes', f98)) == 'MS Sans Serif', f98

    # WINDOWS 2000 STANDARD (architect 2026-10-06, the chrome's theme and the compiled built-in): Windows 2000's own
    # default colours, its setup hive's HKCU "Control Panel\Colors", read off the retail disc image (sources.py
    # win2000_hivedef); the hive's Appearance\Schemes value "Windows Standard" carries the same 29. XP's saved scheme
    # (zkedem's standard.theme, DisplayName "Windows Standard"), classicthemes8's "Windows XP Classic" and ReactOS's
    # "ReactOS Standard" corroborate it on every role key.
    cols = parse_hive_colors(local_path('win2000_hivedef', hv2k))
    notes = ['Windows 2000\'s default scheme as its own setup hive records it (HKCU "Control Panel\\Colors")']
    provs, n_ok = [], 0
    w2k_scheme('Windows Standard', {('AppWorkspace' if k == 'AppWorkSpace' else k): v for k, v in cols.items()},
               provs, notes)
    zf, zdn, zcols = zk['Windows Standard']
    cands = [('xp_classic_zkedem', zf, zdn, zcols)] + [c for c in seconds if c[2] == 'Windows XP Classic']
    p2, n2 = corroborate('Windows 2000 Standard', cols, cands, notes)
    provs += p2; n_ok += n2
    folded_in('Windows 2000 Standard', cols, provs, notes); n_ok += 1
    out.append(entry('windows', 'Windows 2000 Standard', 'Windows 2000 Standard', [provenance('win2000_hivedef', hv2k)]
                     + provs, cols, notes=notes))
    out[-1]['corroborated'] = n_ok
    out[-1]['font'] = hive_font('Windows Standard')
    w2k_raw = out[-1]['raw']

    # WINDOWS ME STANDARD (architect 2026-10-09): Windows Me's classic desktop is Windows 2000's chrome and Windows 2000
    # Standard's colours set in MS Sans Serif — the face being the one difference, as his guidebookgallery captures of
    # WordPad under the two systems show (sources.py winme_captures): Windows 2000 Standard's 29 bytes under the font
    # record ms-sans-serif (a record alone since ~21:20: the app's face is the `font` device key, so the entry is
    # Windows 2000 Standard's twelve under Me's name). The captures are checked against those bytes value by value
    # where they show one
    # (WINME_SAMPLES), and the face is measured off them: the title's first capital stands 9 rows in the Me capture
    # (MS Sans Serif 8's cap) and 8 in the Windows 2000 one (Tahoma 8's).
    caps = winme_captures()
    for f, rows in caps.items():
        for k, (x, y) in WINME_SAMPLES.items():
            if rows[y][x] != cols[k]:
                raise SystemExit(f'build: {f} ({x}, {y}) is {hx(rows[y][x])}, not Windows 2000 Standard\'s {k} '
                                 f'{hx(cols[k])}')
    me_cap, w2k_cap = title_cap_rows(caps['winme.png']), title_cap_rows(caps['win2000pro.png'])
    assert (me_cap, w2k_cap) == (9, 8), (me_cap, w2k_cap)
    out.append(entry('windows', 'Windows Me Standard', 'Windows Me Standard',
                     [local_provenance('winme_captures', 'winme.png'), local_provenance('winme_captures', 'win2000pro.png'),
                      provenance('win2000_hivedef', hv2k)], cols, notes=[
        'Windows Me ships Windows 2000\'s "Windows Standard" colours under MS Sans Serif: the bytes are Windows 2000\'s '
        'setup hive\'s (HKCU "Control Panel\\Colors"), windows-2000-standard\'s exactly',
        'the two captures (WordPad at identical metrics) both show ' + ', '.join(
            f'{k} {hx(cols[k])}' for k in WINME_SAMPLES) + ' byte for byte; the caption\'s interior ramp differs by a few '
        'levels between them (column 500: 97BAE2 under Me, 99BCE5 under 2000), the two systems\' gradient arithmetic, '
        'not the scheme',
        f'the face: the title\'s first capital stands {me_cap} rows in the Me capture and {w2k_cap} in the Windows 2000 '
        'one, and the menu bar\'s capitals likewise (rows 27..35 against 28..35): MS Sans Serif 8 against Tahoma 8']))
    out[-1]['corroborated'] = 0
    out[-1]['font'] = {'face': 'ms-sans-serif', 'named': 'MS Sans Serif',
                       'from': f'measured on the winme_captures: the cap {me_cap} rows (MS Sans Serif 8) where Windows '
                               f'2000\'s Tahoma 8 stands {w2k_cap}'}
    assert out[-1]['raw'] == w2k_raw
    by_key = {e['key']: e for e in out}
    for name, (twin, _) in REACTOS_ONLY.items():     # "role-identical": the roles the twin entry maps, byte for byte
        if twin and map_roles('windows', {k: hx(v) for k, v in ros_only[name].items()}) != by_key[twin]['roles']:
            raise SystemExit(f'build: ReactOS {name} is not role-identical to {twin}')

    # THE WINDOWS 98 / PLUS! DESKTOP THEMES (one source each: 1j01/98's copies of the shipped files).
    dup = 'Copy of Dangerous Creatures (256 color)'
    assert w98[dup][1] == w98['Dangerous Creatures (256 color)'][1]
    for stem, (f, cols) in sorted(w98.items()):
        if stem in ('Windows Default', dup): continue
        notes = []
        if stem == 'Dangerous Creatures (256 color)':
            notes.append(f'1j01/98 also carries "{dup}.theme", byte-identical; not a second entry')
        missing = [k for k in ('ButtonHilight', 'ButtonLight', 'ButtonShadow', 'ButtonDkShadow') if k not in cols]
        if missing: notes.append(f'the 3D colours {missing} are absent')
        words = re.sub(r' \((256|high) color\)$', '', stem)
        out.append(entry('windows-plus', words, stem, provenance('win98_themes', f), cols, notes=notes))
        out[-1]['corroborated'] = 0
        face = parse_theme_menu_font(local_path('win98_themes', f))
        if face is None: raise SystemExit(f'build: {f} carries no NonclientMetrics')
        out[-1]['font'] = font_record(face, f'lfMenuFont of its .theme\'s [Metrics] NonclientMetrics')
    return out, sorted(ros_only)


# ------------------------------------------------------------------ KDE 3
KDE_IMITATES = {'Redmond 95': 'Windows 95', 'Redmond 2000': 'Windows 2000', 'Redmond XP': 'Windows XP', 'CDE': 'CDE',
                'Digital CDE': 'CDE (Digital UNIX)', 'Solaris': 'CDE (Solaris)', 'BeOS': 'BeOS', 'Next': 'NeXTSTEP'}
KDE_RULE_SOURCES = [provenance('tde_rules', 'tdecore/tdeapplication.cpp'), provenance('tqt_rules', 'src/kernel/tqcolor.cpp'),
                    provenance('tqt_rules', 'src/kernel/tqpalette.cpp')]


def kde_entries():
    """-> (entries, {scheme name: reason} of the schemes KDE 3.5 did not ship, KDE_NOT_35)."""
    out, usability, later = [], [], {}
    for src in ('tde_kcs',):
        for f in manifest(src):
            name, cols, contrast = parse_kcsrc(local_path(src, f))
            if name in KDE_USABILITY: usability.append(name); continue     # not imported: usability schemes
            if f in KDE_NOT_35[src]: later[(src, f)] = name; continue      # not imported: not shipped by KDE 3.5
            c = T.KDE3_DEFAULT_CONTRAST if contrast is None else contrast
            p = T.kde3_palette(cols['background'], cols['foreground'], c)
            computed = {f'kde3:{k}': v for k, v in p.items()}
            prov = provenance(src, f)
            if contrast is not None: prov['contrast'] = contrast
            rule = {'id': 'kde3', 'contrast': c}
            out.append(entry('kde3', name, name, prov, cols, computed, imitates=KDE_IMITATES.get(name), rule=rule,
                             flag_rule={'id': 'kde3', 'contrast': c}))
            out[-1]['corroborated'] = 0
    if sorted(usability) != sorted(KDE_USABILITY):
        raise SystemExit(f'build: the KDE usability schemes found are {sorted(usability)}, not {sorted(KDE_USABILITY)}')
    want = {(src, f) for src, fs in KDE_NOT_35.items() for f in fs}
    if set(later) != want:
        raise SystemExit(f'build: the KDE schemes not shipped by KDE 3.5 found differ from KDE_NOT_35: missing '
                         f'{sorted(want - set(later))}')
    return out, {later[(src, f)]: KDE_NOT_35[src][f] for src, f in sorted(later, key=lambda k: (k[0] != 'tde_kcs', k[1].lower()))}


# ------------------------------------------------------------------ CDE
CDE_RULE_SOURCES = [provenance('motif_rules', 'lib/Xm/Color.c'), provenance('motif_rules', 'lib/Xm/ColorP.h'),
                    provenance('motif_rules', 'lib/Xm/Xm.h.in'), provenance('cde_rules', 'cde/programs/dtsession/SrvFile_io.c')]
CDE_SET_SOURCES = [provenance('motif_rules', 'lib/Xm/ColorObj.c'), provenance('cde_rules', 'cde/programs/dtsession/SrvPalette.c'),
                   provenance('cde_rules', 'cde/programs/dtwm/WmResource.c'), provenance('cde_rules', 'cde/programs/dtwm/Dtwm.defs.src')]
CDE_SETS = ('1 active window frame', '2 inactive window frame', '3 workspace backdrop / switch', '4 text and lists',
            '5 primary (application background)', '6 secondary (menus, dialogs)', '7 workspace backdrop / switch',
            '8 front panel')
RULES = {
    'windows-dialog': {'name': 'Windows\' Appearance dialog on a 3D face, in shlwapi\'s 240-scale integer HLS: Hilight = the '
                               'lightness halfway to white (the half rounded up), Shadow = two thirds of it (floored), hue and '
                               'saturation kept, 3DLight the face, DkShadow black; read by the flags\' one-line bevel '
                               '(Hilight top and left, Shadow bottom and right) of the windows and windows-plus '
                               'families, and run at import for the relief quartet of the product\'s own schemes, '
                               'the warptempo family, as the app derives it (tools/theme_catalog/toolkit_rules.py '
                               'windows_dialog, flag_bevel)',
                       'sources': [provenance('wine_rules', 'dlls/shlwapi/ordinal.c')]},
    'kde3': {'name': 'KDE 3 createApplicationPalette over Qt 3\'s integer HSV at the scheme\'s contrast (default 7): '
                     'light = background.light(100 + (2c + 4) x 16 / 10), midlight = background.light(110), dark = '
                     'background.dark(100 + (2c + 4) x 10), mid = background.dark(120), shadow black; the disabled '
                     'foreground; the Windows quartet (light, midlight, dark, shadow) as Qt\'s Windows bevel draws it '
                     '(tools/theme_catalog/toolkit_rules.py kde3_palette)', 'sources': KDE_RULE_SOURCES},
    'motif': {'name': 'Motif CalculateColorsRGB on each colour set\'s 16-bit background (dark below 20 %, light above '
                      '93 %, medium between; the foreground black above 70 %), each channel shown as its top byte; Motif '
                      'paints two shadows, so the quartet is (ts, ts, bs, bs) (tools/theme_catalog/toolkit_rules.py '
                      'motif_colors)', 'sources': CDE_RULE_SOURCES,
              'colour_sets': list(CDE_SETS), 'colour_set_sources': CDE_SET_SOURCES},
    'gtk2-clearlooks': {'name': 'GTK 2\'s shade (the Clearlooks engine\'s ge_shade_color = GTK\'s gtk_style_shade = '
                                'metacity\'s copy: HLS lightness and saturation both x k, clamped, in doubles) and '
                                'metacity\'s blend (color_composite on 16-bit colours), run on the gtkrc\'s '
                                'gtk-color-scheme as the engine, GTK and metacity run them, each value the byte the '
                                'program paints: the engine\'s through cairo 1.8 and pixman (floor of 256 x the double, '
                                'capped at 255), GTK\'s and metacity\'s through a GdkColor (x 65535 truncated, its top '
                                'byte); the one-line relief quartet (light, light, dark, dark) = the engine\'s inset pair '
                                '1.06 / 0.94 of the background (tools/theme_catalog/toolkit_rules.py gtk2_shade, '
                                'cairo_byte, gdk16, gdk_byte, metacity_blend; each entry\'s provenance.rule.derivations)',
                        'sources': [local_provenance('gtk2_rules', f) for f in LOCAL_SOURCES['gtk2_rules']['files']]
                                   + [local_provenance('metacity_rules', 'src/ui/theme.c')]},
    'flat': {'name': 'no bevel: the flags\' one-line bevel is the face itself on both sides, for a family whose toolkit '
                     'draws no one-line light / dark bevel round a raised face (GNOME 2: Clearlooks\' gummy button is a '
                     'four-stop ramp in a 1-px border mixed from the theme\'s shade[6] and the face, '
                     'clearlooks_gummy_draw_button; tools/theme_catalog/toolkit_rules.py flag_bevel)', 'sources': []},
}


def cde_entries():
    out, mono = [], []
    for f in manifest('cde_palettes'):
        stem = os.path.basename(f)[:-len('.dp')]
        r = parse_dp(local_path('cde_palettes', f), stem)
        if r is None: mono.append(stem); continue
        lines, sets = r
        raw = {f'set{i + 1}': T.x_to_8bit(s) for i, s in enumerate(sets)}
        computed, branches = {}, []
        for i, s in enumerate(sets):
            m, br = T.motif_colors(s); branches.append(br)
            for k in ('fg', 'sel', 'ts', 'bs'): computed[f'motif:set{i + 1}.{k}'] = T.x_to_8bit(m[k])
        prov = provenance('cde_palettes', f); prov['recorded'] = lines
        rule = {'id': 'motif', 'branches': branches}
        out.append(entry('cde', camel_words(stem), stem, prov, raw, computed, rule=rule))
        out[-1]['corroborated'] = 0
    return out, mono


# ------------------------------------------------------------------ GNOME 2 (gnome2)
# THE CLEARLOOKS ENTRY (architect 2026-10-07: the second chrome vocabulary is Debian 6 squeeze's GNOME 2.30 desktop
# taken whole, his captures tmp/squeeze/ the law): key `clearlooks`, display name "Clearlooks" (the GTK theme's own
# name, its index.theme and the metacity theme's <name>), the squeeze defaults (GConf: gtk_theme Clearlooks, metacity
# theme Clearlooks, font "Sans 10"). Its sources are the squeeze image's own bytes (sources.py LOCAL_SOURCES): the
# gtkrc of gtk2-engines 1:2.20.1-1 (byte-identical to gtk-engines 2.20.0's; 2.20.2's differs only in two Evolution
# widget_class lines) and the metacity theme of gnome-themes 2.30.2-1 (byte-identical to the 2.30.2 tarball's). THE
# ROLES (roles.py MAPPING['gnome2']): the scheme's colours as recorded, and five values each program's own rule computes
# from them, recorded with their derivation (GNOME2_DERIVATIONS): the engine's one-line edge pair, the tooltip's
# border, the insensitive text, the unfocused title. The engine's further tones (its shade table, the gummy ramps,
# metacity's band) are not catalog values: the Clearlooks painters, removed with the clearlooks chrome 2026-10-09,
# read them by need (git history, d5b91f52^).
GNOME2_GTKRC = 'usr/share/themes/Clearlooks/gtk-2.0/gtkrc'
GNOME2_METACITY = 'usr/share/themes/Clearlooks/metacity-1/metacity-theme-1.xml'
GNOME2_DERIVATIONS = {
    'gtk2:inset_light': 'ge_shade_color (bg_color, 1.06) through cairo: the light line of the engine\'s one-line edge '
                        '(gtk-engines 2.20.2 engines/clearlooks/src/clearlooks_draw.c clearlooks_draw_inset line 63, '
                        'drawn by the gummy button, entry and scale trough, clearlooks_draw_gummy.c lines 189, 271, 595; '
                        'clearlooks_draw_highlight_and_shade line 184, a frame\'s inner line, line 1235)',
    'gtk2:inset_dark': 'ge_shade_color (bg_color, 0.94) through cairo: the dark line of the same edge (clearlooks_draw.c '
                       'clearlooks_draw_inset line 62; clearlooks_draw_highlight_and_shade line 185)',
    'gtk2:tooltip_border': 'ge_shade_color (tooltips bg[NORMAL] = tooltip_bg_color, 0.6) through cairo: the tooltip\'s '
                           '1-px border on all four sides (clearlooks_draw.c clearlooks_draw_tooltip line 1999)',
    'gtkrc:fg[INSENSITIVE]': 'the default style\'s fg[INSENSITIVE] = darker (@bg_color) = gtk_style_shade (bg_color, '
                             '0.7) as a GdkColor, drawn through the style\'s fg_gc (clearlooks_style.c '
                             'clearlooks_style_draw_layout line 1846; the etched copy under it is shade 1.2 of the parent '
                             'bg)',
    'metacity:title_unfocused': 'the unfocused title\'s colour, blend/gtk:fg[NORMAL]/gtk:bg[NORMAL]/0.45 (the metacity '
                                'theme\'s draw_ops title_text_unfocused, the frame style "normal"\'s title piece): '
                                'fg_color + (bg_color - fg_color) x 0.45 by metacity 2.30 theme.c color_composite',
}


def squeeze_file(path):
    """A squeeze image file on the build host, its sha256 checked against the pin -> its path."""
    p = local_file('squeeze_live', path)
    if not os.path.exists(p):
        raise SystemExit(f'build: {p} is missing; extract it from the squeeze image (sources.py LOCAL_SOURCES)')
    got, want = hashlib.sha256(open(p, 'rb').read()).hexdigest(), LOCAL_SOURCES['squeeze_live']['files'][path][1]
    if got != want: raise SystemExit(f'build: {p} has sha256 {got}, not the pinned {want}')
    return p


def gnome2_entries():
    g = parse_gtkrc(squeeze_file(GNOME2_GTKRC))
    m = parse_metacity(squeeze_file(GNOME2_METACITY))
    sc, default = g['color_scheme'], g['styles']['default']
    eng = default['engines']['clearlooks']
    assert eng['style'] == 'GUMMY', eng
    # the default style's colours as GTK stores them (the gtk:<component>[<state>] colours metacity reads)
    gtk = {(k[:k.index('[')], k[k.index('[') + 1:-1]): gtkrc_color(v, sc, GNOME2_GTKRC) for k, v in default['colors'].items()}
    unit = lambda c16: tuple(v / 65535.0 for v in c16)
    bg = unit(gtk[('bg', 'NORMAL')])
    tip = g['styles']['tooltips']['colors']
    assert (tip['bg[NORMAL]'], tip['fg[NORMAL]']) == ('@tooltip_bg_color', '@tooltip_fg_color'), tip
    tip_bg = unit(gtkrc_color(tip['bg[NORMAL]'], sc))
    focused = draw_ops_flat(m, frame_piece(m, 'focused', 'title'))
    unfocused = draw_ops_flat(m, frame_piece(m, 'normal', 'title'))
    assert focused[-1]['op'] == 'title' and focused[-1]['color'][0] == 'rgb', focused[-1]   # the text over its shadows
    assert len(unfocused) == 1 and unfocused[0]['op'] == 'title', unfocused
    raw = dict(sc)
    raw['title_text'] = focused[-1]['color'][1]
    computed = {
        'gtk2:inset_light': T.cairo_byte(T.gtk2_shade(bg, 1.06)),
        'gtk2:inset_dark': T.cairo_byte(T.gtk2_shade(bg, 0.94)),
        'gtk2:tooltip_border': T.cairo_byte(T.gtk2_shade(tip_bg, 0.6)),
        'gtkrc:fg[INSENSITIVE]': T.gdk_byte(gtk[('fg', 'INSENSITIVE')]),
        'metacity:title_unfocused': T.gdk_byte(metacity_color(unfocused[0]['color'], gtk)),
    }
    assert set(computed) == set(GNOME2_DERIVATIONS)
    styles = ('button', 'menu', 'menu_item', 'entry', 'tooltips')     # the per-style overrides the roles' painters read
    rule = {'id': 'gtk2-clearlooks',
            'engine': 'Clearlooks of gtk2-engines 1:2.20.1-1 (squeeze), its arithmetic cited at gtk-engines 2.20.2 '
                      '(ge_shade_color and clearlooks_draw.c are byte-identical in 2.20.0 and 2.20.2)',
            'window_manager': 'metacity 1:2.30.1-3 (squeeze), its colour arithmetic cited at metacity 2.30.3 theme.c',
            'clearlooks': dict(eng),
            'styles': {n: {'colors': {k: expr_text(v) for k, v in g['styles'][n]['colors'].items()},
                           'clearlooks': {k: expr_text(v) for k, v in g['styles'][n]['engines'].get('clearlooks', {}).items()}}
                       for n in styles},
            'derivations': dict(GNOME2_DERIVATIONS)}
    prov = [local_provenance('squeeze_live', GNOME2_GTKRC), local_provenance('squeeze_live', GNOME2_METACITY)]
    notes = ['Debian 6 squeeze\'s GNOME 2.30 default (GConf: gtk_theme Clearlooks, metacity theme Clearlooks, font_name '
             '"Sans 10", titlebar_font "Sans Bold 10"); the gtkrc sets style = GUMMY, radius 3.0, menubarstyle 2, '
             'toolbarstyle 1, reliefstyle 1, colorize_scrollbar TRUE, animation FALSE',
             'the gtkrc is byte-identical to gtk-engines 2.20.0\'s; 2.20.2\'s differs only in two Evolution widget_class '
             'lines (ETable / ETree); the metacity theme is byte-identical to the gnome-themes 2.30.2 tarball\'s',
             'the relief quartet is the engine\'s one-line edge (light, light, dark, dark): Clearlooks draws no two-line '
             'Windows edge; the caption is flat in the catalog (metacity\'s band is a ramp of shades of the one colour)',
             'the captures (tmp/squeeze/, 2026-10-06/07) show every computed byte as recorded: the inset ring #FBFBFA / '
             '#E0DEDD, the tooltip border #BABA45, the insensitive text #A9A5A2, the unfocused title #6B6A6A']
    e = entry('gnome2', 'Clearlooks', 'Clearlooks', prov, raw, computed, notes=notes, rule=rule)
    e['corroborated'] = 0
    return [e]


# NO CLEARLOOKS PAINTER TONES ARE COMPUTED HERE (the clearlooks chrome removed, architect 2026-10-09 ~21:20: "you can
# definitely drop Clearlooks and CDE"): the gummy engine's and metacity's arithmetic that gave every `cl_` tone a
# Clearlooks painter put down (engine_tones at the clearlooks ChromeSpec's geometry, with its ramp-fit check, and its
# helpers clearlooks_geometry, mc_eval, mc_render, ramp_rule, Tones and the caption and button state tables) fed the
# generated includes that left with that chrome and read three files that left with it (chrome_spec.h's
# kChromeSpecClearlooks, src/gui/clearlooks_derive.h, src/gui/clearlooks_paint.h); it stands in git history, at
# d5b91f52^ (tools/theme_catalog/build.py). The clearlooks entry keeps its recorded colours and its five import-time
# derivations (GNOME2_DERIVATIONS) as the catalog's record, and carries no `engine_tones`.


# ------------------------------------------------------------------ the checks and the write
def checks(entries):
    by = {e['key']: e for e in entries}
    keys = [e['key'] for e in entries]
    assert len(keys) == len(set(keys)), sorted(x for x in keys if keys.count(x) > 1)
    for x in keys: assert re.fullmatch(r'[a-z0-9]+(-[a-z0-9]+)*', x) and x.isascii(), x
    rd = by['windows-rainy-day']
    want = ['#8399B1', '#C1CCD9', '#8399B1', '#4F657D', '#000000']
    assert [rd['raw'][x] for x in ('ButtonFace', 'ButtonHilight', 'ButtonLight', 'ButtonShadow', 'ButtonDkShadow')] == want
    assert [rd['roles'][x] for x in ('ground', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow')] == want
    w95 = by['windows-95-standard']['roles']
    assert [w95[x] for x in ('ground', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow')] == \
        ['#C0C0C0', '#FFFFFF', '#DFDFDF', '#808080', '#000000']
    assert T.kde3_quartet((0x30,) * 3, 7) == ((0x3D,) * 3, (0x34,) * 3, (0x11,) * 3, (0, 0, 0))
    assert T.motif_pair_8bit((0x30,) * 3) == ((0x98,) * 3, (0x6E,) * 3, 'dark')
    assert T.motif_pair_8bit((0x41, 0x52, 0x5C)) == ((0xA6, 0xAE, 0xB3), (0x1E, 0x25, 0x2A), 'medium')
    for e in entries:      # every entry names its flags' rule, its family's, and the rule runs
        assert e['flag_rule']['id'] == FLAG_RULE[e['family']], e['key']
        if e['family'] == 'kde3': assert e['flag_rule']['contrast'] == e['provenance']['rule']['contrast'], e['key']
        T.flag_bevel(e['flag_rule'], (0x8A, 0x5E, 0xAC))
    assert T.windows_dialog((0xD4, 0xD0, 0xC8))[0] == (0xEA, 0xE8, 0xE3)
    assert T.windows_dialog((0x83, 0x99, 0xB1)) == ((0xC1, 0xCC, 0xD9), (0x83, 0x99, 0xB1), (0x4F, 0x65, 0x7D), (0, 0, 0))
    # Windows 2000 Standard is the hive's bytes (the windows-2000 chrome's compiled theme: the chrome roles of the ReactOS
    # captures); Windows 98 Standard is Windows 95 Standard's roles but for the 3DLight, under a gradient caption
    w2k = by['windows-2000-standard']['roles']
    assert [w2k[x] for x in ('ground', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow', 'selected_fill',
                             'info_ground', 'title_active', 'title_inactive')] == \
        ['#D4D0C8', '#FFFFFF', '#D4D0C8', '#808080', '#404040', '#0A246A', '#FFFFE1', '#0A246A', '#808080']
    w98 = by['windows-98-standard']
    assert {r: v for r, v in w98['roles'].items() if w95[r] != v} == {'bevel_light': '#C0C0C0'}
    assert (w98['raw']['GradientActiveTitle'], w98['raw']['GradientInactiveTitle']) == ('#1084D0', '#B5B5B5')
    assert 'GradientActiveTitle' not in by['windows-95-standard']['raw']
    # the Windows 95 CD's six (architect 2026-10-09): right after windows-95-standard, in this order, 25 values each, and
    # each differing from its Windows 2000 twin on exactly the values the CD's shell2.inf records differently
    w95_six = {'windows-95-maple': ('windows-maple', ['ActiveTitle', 'AppWorkspace', 'InactiveTitle']),
               'windows-95-wheat': ('windows-wheat', ['AppWorkspace']),
               'windows-95-marine': ('windows-marine', ['TitleText']),
               'windows-95-storm': ('windows-storm', ['InactiveTitleText']),
               'windows-95-rose': ('windows-rose', ['InactiveTitleText']),
               'windows-95-plum': ('windows-plum', ['Hilight', 'TitleText'])}
    at = keys.index('windows-95-standard')
    assert keys[at + 1:at + 7] == list(w95_six), keys[at:at + 8]
    for k, (twin, diff) in w95_six.items():
        assert len(by[k]['raw']) == 25 and by[k]['family'] == 'windows', k
        assert sorted(x for x in by[k]['raw'] if by[k]['raw'][x] != by[twin]['raw'][x]) == diff, k
    for gone in ('windows-classic', 'windows-standard'): assert gone not in by, gone
    # the font records (2026-10-09; the record alone since ~21:20, windows_entries' font_record): every Windows-family
    # entry carries one read off its source, and no other family does; Windows 2000 Standard names Tahoma, Windows Me
    # Standard (right after it) is its bytes in MS Sans Serif, and the Windows 95 CD's and 98's defaults name MS Sans
    # Serif
    for e in entries:
        assert ('font' in e) == (e['family'] in ('windows', 'windows-plus')), e['key']
        if 'font' in e:
            assert e['font']['face'] == ('tahoma' if e['font']['named'] == 'Tahoma' else 'ms-sans-serif'), e['key']
    assert by['windows-2000-standard']['font']['face'] == 'tahoma'
    at = keys.index('windows-2000-standard')
    assert keys[at + 1] == 'windows-me-standard', keys[at:at + 2]
    me = by['windows-me-standard']
    assert me['raw'] == by['windows-2000-standard']['raw'] and me['font']['face'] == 'ms-sans-serif'
    for k in ('windows-95-standard', 'windows-98-standard'): assert by[k]['font']['face'] == 'ms-sans-serif', k
    for d in DUPLICATES: assert d not in by, d
    assert sum(1 for e in entries if e['family'] == 'kde3') == KDE3_ENTRIES
    assert len(KDE_NOT_35['tde_kcs']) == 21
    # the display tiers: Windows Storm, Teal and Red, White, and Blue and Windows 95 Storm are the only `vga` entries, none `windows-20`
    # (Windows 98 Standard misses `vga` only by its tooltip ground #FFFFE1, Windows 95 Standard by that and its 3DLight
    # #DFDFDF)
    assert sorted(e['key'] for e in entries if e['display_tier'] == 'vga') == \
        ['windows-95-storm', 'windows-red-white-and-blue', 'windows-storm', 'windows-teal']
    assert not [e['key'] for e in entries if e['display_tier'] == 'windows-20']
    assert display_tier(w98['roles']) == 'high-colour' and \
        display_tier({r: v for r, v in w98['roles'].items() if r != 'info_ground'}) == 'vga'
    # Clearlooks (squeeze): the scheme's bytes and the engine's own shades of them, each as his captures show it
    cl = by['clearlooks']
    assert cl['roles'] == {'ground': '#EDECEB', 'label': '#000000', 'bevel_hilight': '#FBFBFA', 'bevel_light': '#FBFBFA',
                           'bevel_shadow': '#E0DEDD', 'bevel_dkshadow': '#E0DEDD', 'selected_fill': '#86ABD9',
                           'selected_text': '#FFFFFF', 'info_ground': '#F5F5B5', 'info_text': '#000000',
                           'info_frame': '#BABA45', 'field_ground': '#FFFFFF', 'field_text': '#1A1A1A',
                           'disabled_text': '#A9A5A2', 'title_active': '#86ABD9', 'title_inactive': '#EDECEB'}, cl['roles']
    assert cl['raw']['title_text'] == '#FFFFFF' and cl['provenance']['rule']['computed']['metacity:title_unfocused'] == '#6B6A6A'
    assert cl['flag_rule'] == {'id': 'flat'} and cl['display_tier'] == 'high-colour'
    # the product's own (2026-10-10): Cool Edit Pro ME leads the catalog, its raw his file's twelve, its relief the
    # dialog rule's on its ground (src/gui/chrome_derive.h asserts the same quartet off the same ground)
    assert keys[0] == 'cool-edit-pro-me', keys[:2]
    cep = by['cool-edit-pro-me']
    assert cep['raw'] == dict(WARPTEMPO_SCHEMES[0][2]) and cep['family'] == 'warptempo'
    assert [cep['roles'][x] for x in ('ground', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow')] == \
        ['#626C7B', '#AEB5BF', '#626C7B', '#414752', '#000000']
    assert cep['display_tier'] == 'high-colour'
    # the catalog is the chrome's alone (2026-10-07): no entry carries program roles, and the program's own family of
    # palette entries is gone with them (its colors are the palette's, src/gui/palette_file.h); every family is one of
    # FAMILIES, the product's own chrome schemes among them
    for e in entries: assert 'program_roles' not in e and e['family'] in FAMILIES, e['key']


def drop_duplicates(entries):
    """DUPLICATES out of the entries, each after asserting its roles equal its twin's -> (entries, {key: twin})."""
    by = {e['key']: e for e in entries}
    for key, twin in DUPLICATES.items():
        if key not in by or twin not in by: raise SystemExit(f'build: duplicate {key} or its twin {twin} is not in the sources')
        if by[key]['roles'] != by[twin]['roles']: raise SystemExit(f'build: {key} is not role-identical to {twin}')
    return [e for e in entries if e['key'] not in DUPLICATES], dict(DUPLICATES)


def document(entries, not_imported):
    """The catalog document over the entries (in their written order) and the not-imported record: both roads' one
    writer of everything else in the file."""
    return {
        'what': 'The Warptempo theme catalog (tools/theme_catalog/build.py; architect 2026-10-03: imported themes only, '
                'no derivation). Every colour is a recorded byte with its provenance; where the source records only base '
                'colours and its toolkit computed the rest at run time (KDE 3, CDE / Motif, GNOME 2\'s Clearlooks and '
                'metacity), that toolkit\'s own rule ran once at import and its rule and sources are named in the '
                'entry\'s provenance. "raw" holds every value '
                'the source records under its own key names; "roles" the catalog roles (tools/theme_catalog/roles.py); '
                'a role a source has no word for is absent and the app\'s own value applies. Bytes are #RRGGBB as the '
                'source records them (the renderer takes a theme byte as a Display-P3 byte as-is). The catalog is the '
                'chrome\'s alone (2026-10-07): the program\'s colors are the app\'s compiled palettes '
                '(src/gui/palette_file.h). One family is designed, not imported (2026-10-10): `warptempo`, the '
                'product\'s own chrome schemes, each the architect\'s scheme file recorded verbatim, its relief by '
                'Windows\' Appearance-dialog rule as the app derives it.',
        'roles': list(ROLES),
        'rules': RULES,
        'display_tiers': {'what': 'each entry\'s display_tier: the smallest of these period colour sets holding every '
                                  'colour its roles use, else high-colour (architect 2026-10-03, late)',
                          'vga': list(VGA16),
                          'windows-20': {'adds': list(WINDOWS_STATIC_EXTRAS),
                                         'what': 'the VGA 16 plus the four static colours Windows reserves in a '
                                                 '256-colour display\'s system palette, always solid there'}},
        'not_imported': not_imported,
        'entries': [{x: v for x, v in e.items() if x != 'corroborated'} for e in entries],
    }


def write(doc):
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'w').write(json.dumps(doc, indent=1, ensure_ascii=True) + '\n')
    print(f'wrote {os.path.relpath(OUT, REPO)}: {len(doc["entries"])} entries')


def check_only():
    """The --check-only road (the head): the checks on the committed catalog, nothing written."""
    checks(json.load(open(OUT))['entries'])
    print(f'checked {os.path.relpath(OUT, REPO)}: the checks pass')


def main():
    if sys.argv[1:] == ['--check-only']: return check_only()
    if sys.argv[1:]: raise SystemExit('usage: python3 tools/theme_catalog/build.py [--check-only]')
    win, ros_only = windows_entries()
    kde, kde_later = kde_entries()
    entries = warptempo_entries() + win + kde
    cde, mono = cde_entries(); entries += cde
    entries += gnome2_entries()
    entries, dups = drop_duplicates(entries)
    entries.sort(key=lambda e: FAMILIES.index(e['family']))
    checks(entries)
    write(document(entries, {
        'cde_monochrome': {'files': [f'{m}.dp' for m in mono],
                           'reason': 'X colour names for monochrome displays; dtsession refuses them on a colour display '
                                     '(SrvFile_io.c ParsePaletteInfo)'},
        'reactos': {'schemes': {n: REACTOS_ONLY[n][1] for n in ros_only},
                    'reason': 'ReactOS hivedef.inf schemes no independent source records as Windows\' (architect '
                              '2026-10-03, late: the whole family is dropped)'},
        'kde3_usability': {'schemes': list(KDE_USABILITY),
                           'reason': 'usability schemes (architect 2026-10-03, late)'},
        'kde3_not_kde35': {'schemes': kde_later,
                           'reason': 'KDE colour schemes KDE 3.5 did not ship, added later by Trinity; the '
                                     'catalog keeps what KDE 3.5 shipped (architect 2026-10-03, late)'},
        'windows_95_cd': {'schemes': WIN95_NOT_ENTRIES,
                          'byte_equal_to_an_entry': WIN95_SAME,
                          'reason': 'the Windows 95 CD\'s 27 Appearance schemes: 6 are entries (windows-95-maple, '
                                    '-wheat, -marine, -storm, -rose, -plum: their bytes differ from Windows 2000\'s '
                                    'of the same name), 11 are byte-equal on all 25 values to an existing entry (the '
                                    'second map) and the rest are listed first with their reasons (architect '
                                    '2026-10-09)'},
        'duplicates': {'keys': dups,
                       'reason': 'role-identical to the named entry, which is kept (architect 2026-10-03, late)'}}))
    for fam in FAMILIES:
        es = [e for e in entries if e['family'] == fam]
        srcs = set()
        for e in es:
            for q in e['provenance']['sources']:
                srcs.add(q.get('repository', q.get('item', q.get('project'))) + '@' + str(q.get('commit', q.get('image_sha1', ''))))
        print(f'{fam:13s} entries {len(es):3d}  corroborated {sum(1 for e in es if e["corroborated"]):3d}  '
              f'sources {len(srcs)}: ' + ', '.join(sorted(s.split("@")[0] for s in srcs)))
    print('display tiers: ' + ', '.join(f'{t} {sum(1 for e in entries if e["display_tier"] == t)}' for t, _ in DISPLAY_TIERS))
    if mono: print(f'not imported: {", ".join(m + ".dp" for m in mono)} (monochrome palettes, refused on a colour display)')
    print(f'not imported: ReactOS {", ".join(ros_only)} (no independent source); KDE 3 {", ".join(KDE_USABILITY)} '
          f'(usability); {", ".join(f"{a} (= {b})" for a, b in dups.items())} (role-identical)')
    for why in (TDE_2023, TDE_2025):
        ns = [n for n, r in kde_later.items() if r == why]
        print(f'not imported: KDE {len(ns)}, {", ".join(ns)} ({why.split(": added")[0]})')


if __name__ == '__main__':
    main()
