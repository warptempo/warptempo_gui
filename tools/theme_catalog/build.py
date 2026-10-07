#!/usr/bin/env python3
# tools/theme_catalog/build.py — the fetched sources (fetch.py) -> docs/themes/catalog.json: every entry's recorded
# bytes with their provenance (a fetched file, sources.py SOURCES; or a file of a disc image on the build host,
# LOCAL_SOURCES: the squeeze image's Clearlooks), the values its own toolkit computed at import (toolkit_rules.py, the
# rule named), and
# its catalog roles (roles.py), the family rule its flags take (flag_rule) and its display tier (display_tier). THE APP CARRIES IMPORTED THEMES ONLY,
# NO DERIVATION (architect 2026-10-03): nothing here invents a colour; a role a source has no word for stays absent.
# THE ONE EXCEPTION IS THE PROGRAM'S OWN FAMILY, `warptempo`, and it derives nothing either: `warptempo` is THE
# ARCHITECT'S PICK, CHOSEN, NOT IMPORTED (chosen_entry: his ruling of 2026-10-03 on the colour loop's mock sets, its bytes his, recorded as ruled),
# and so is each `warptempo-preset-<n>`, THE ARCHITECT'S PRESET <n> ON THE COLOUR PICKER (preset_entries, architect
# 2026-10-04: his saved looks carried into the product, the picker's chrome rule applied here, at generation, so the
# app derives nothing).
# NOT IMPORTED (architect 2026-10-03, late; NOT_IMPORTED below, each with its reason, recorded in the catalog): the
# schemes no independent source records as Windows', the usability schemes, the KDE schemes KDE 3.5 did not ship, and
# the role-identical duplicates. The
# checks run before the write; the last lines report each family: entries, corroborated, sources.
#
#   python3 tools/theme_catalog/build.py
#   python3 tools/theme_catalog/build.py --presets-only
#
# --presets-only (architect 2026-10-04) is the road for a host that cannot reach the pinned sources (the cloud: the
# Trinity mirror is outside its egress): it re-derives only what needs no fetched file -- the program's own family
# (the chosen entry, recomputed and asserted equal to the committed one, and the preset entries from presets.json) and
# the header -- and carries every imported entry and the not-imported record from the committed catalog.json byte for
# byte; the checks run on the whole. The full run writes the same bytes where the
# sources are at hand (both roads build the document through one function, document()).
import hashlib, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sources import SOURCES, REPO, local_path, provenance, LOCAL_SOURCES, local_file, local_provenance
sys.path.insert(0, os.path.join(REPO, 'tools', 'palette'))
import colour as CL      # the picker's chrome rule (windows95_chrome), the one the picker paints with
from parse_windows import parse_hivedef, parse_hive_colors, parse_hive_schemes, parse_theme
from parse_kde import parse_kcsrc
from parse_cde import parse_dp
from parse_gtkrc import parse_gtkrc, gtkrc_color, expr_text, parse_metacity, metacity_color, frame_piece, draw_ops_flat
import toolkit_rules as T
from roles import ROLES, MAPPING, map_roles

OUT = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
FAMILIES = ('windows', 'windows-plus', 'kde3', 'cde', 'gnome2', 'warptempo')
# a key's prefix per family; GNOME 2's keys are the GTK theme's own name, lowercase (`clearlooks`), as the program's
# own family's are its own (architect 2026-10-07: the bundled clearlooks.theme)
KEY_PREFIX = {'windows': 'windows', 'windows-plus': 'plus', 'kde3': 'kde3', 'cde': 'cde', 'gnome2': None}
# THE FLAGS' RULE per family (architect 2026-10-03, late: a flag's one-line bevel is its theme family's own rule on the
# flag's face, toolkit_rules.flag_bevel): Windows' Appearance dialog for the Windows families and the program's own
# ("take Windows' rule"), KDE 3's at the scheme's contrast, Motif's for CDE, FLAT for GNOME 2 (Clearlooks draws no
# one-line bevel round a raised face: toolkit_rules.flag_bevel states why).
FLAG_RULE = {'windows': 'windows-dialog', 'windows-plus': 'windows-dialog', 'warptempo': 'windows-dialog',
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


# ------------------------------------------------------------------ Windows (windows), windows-plus
def xp_name(n):
    """An XP .theme DisplayName as ReactOS spells the scheme: the (VGA) / (high color) tags dropped, classicthemes8's
    'Red, Blue & White' as Windows' 'Red, White, and Blue'."""
    n = re.sub(r' \((VGA|high color)\)$', '', n)
    return 'Red, White, and Blue' if n == 'Red, Blue & White' else n


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

    # WINDOWS 95 STANDARD, hand-recorded: the retail picture.
    f98, c98 = w98['Windows Default']
    cols = dict(c98); cols['ButtonLight'] = (0xDF, 0xDF, 0xDF)
    assert (cols['ButtonFace'], cols['ButtonHilight'], cols['ButtonShadow'], cols['ButtonDkShadow']) == \
        ((192,) * 3, (255,) * 3, (128,) * 3, (0,) * 3)
    out.append(entry('windows', 'Windows 95 Standard', 'Windows 95 Standard', [
        {'project': 'hand-recorded (architect 2026-10-03)', 'keys': ['ButtonFace', 'ButtonHilight', 'ButtonLight',
         'ButtonShadow', 'ButtonDkShadow'], 'record': 'the Windows 95 retail screen captures (Toasty Tech): a window '
         'frame\'s outer top line is DFDFDF, COLOR_3DLIGHT; face C0C0C0, Hilight FFFFFF, Shadow 808080, DkShadow 000000'},
        provenance('win98_themes', f98) | {'keys': 'every other key'}], cols, notes=[
        'the sources disagree on ButtonLight (COLOR_3DLIGHT): the Windows 95 retail captures show DFDFDF; Windows 98\'s '
        'Windows Default.theme and windows-98-standard\'s records (Windows 2000\'s and XP\'s "Windows Classic") record '
        'C0C0C0 (the face), and the early Windows 95 beta captures draw no 3DLight line. The entry is the retail '
        'Windows 95 picture, a flat caption (no Gradient keys).']))
    out[-1]['corroborated'] = 0

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
                               '(Hilight top and left, Shadow bottom and right) of the windows, windows-plus and warptempo '
                               'families (tools/theme_catalog/toolkit_rules.py windows_dialog, flag_bevel)',
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
# metacity's band) are not catalog values: the Clearlooks painters read them by need, each in its own round.
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


# THE CLEARLOOKS ENTRY'S PROGRAM ROLES (architect 2026-10-07 ~05:30, CL9's question answered: each chrome's theme file
# names its own program colours, the one built-in staying win2000's): the waveform, the flags, the playhead's stem and
# the scanner as he picked them on the CL10 sheets (tmp/clearlooks/report_CL10.md) — sheet a's third band (black
# canvas, bg[SELECTED] the lit outline) and its two flags, the ink the selection blue's 1.3 stop (architect 2026-10-07
# ~09:00, spot[1] too dark on black: the selected flag's byte); ONE PAIR FOR WARP AND
# PHASE RESET (a scene shows one column, report CL10 section 4); sheet d's F1 history pair, the GNOME HIG green and red
# lit by the engine's 1.3; black labels on every face; the flag outline the canvas's own colour (architect 2026-10-07
# ~06:10: "otherwise when it reaches the canvas it's noticeable"). Each value is (a gtkrc colour-scheme key or a
# literal byte, a ge_shade_color factor through cairo or None, the provenance), recorded with the entry
# (program_roles, provenance.rule.program_roles) and named by gen_theme_files.py in clearlooks.theme.
GNOME2_HIG_GREEN = '#83A67F'   # the GNOME HIG's palette green (report CL10 section 2)
GNOME2_HIG_RED = '#C1665A'     # the GNOME HIG's palette red (report CL10 section 2)
GNOME2_PROGRAM_ROLES = {
    'waveform_canvas': ('#000000', None, 'black, his pick (CL10a band 3: the canvas under the accent ink)'),
    'waveform_ink': ('selected_bg_color', 1.3, 'the engine\'s 1.3 stop of bg[SELECTED], the selected flag\'s byte: '
                                              'spot[1] read too dark on the black canvas (architect 2026-10-07 '
                                              '~09:00)'),
    'waveform_outline': ('selected_bg_color', None, 'bg[SELECTED], the lit outline (CL10a band 3)'),
    'warp_flag': ('selected_bg_color', None, 'bg[SELECTED], the flag at rest (CL10a)'),
    'warp_flag_selected': ('selected_bg_color', 1.3, 'the flag selected, the engine\'s 1.3 stop (CL10a)'),
    'phase_reset_flag': ('selected_bg_color', None, 'the warp flag\'s byte: one pair for both kinds (CL10 section 4)'),
    'phase_reset_flag_selected': ('selected_bg_color', 1.3, 'the warp flag\'s selected byte (one pair)'),
    'added_flag': (GNOME2_HIG_GREEN, None, 'the GNOME HIG green (CL10d F1)'),
    'added_flag_selected': (GNOME2_HIG_GREEN, 1.3, 'the HIG green lit by the engine\'s 1.3 (CL10d F1)'),
    'removed_flag': (GNOME2_HIG_RED, None, 'the GNOME HIG red (CL10d F1)'),
    'removed_flag_selected': (GNOME2_HIG_RED, 1.3, 'the HIG red lit by the engine\'s 1.3 (CL10d F1)'),
    'flag_label': ('fg_color', None, 'black on every resting face'),
    'warp_label_selected': ('fg_color', None, 'black on the lit face'),
    'phase_reset_label_selected': ('fg_color', None, 'black on the lit face'),
    'added_label_selected': ('fg_color', None, 'black on the lit face'),
    'removed_label_selected': ('fg_color', None, 'black on the lit face'),
    'flag_outline': ('#000000', None, 'the canvas\'s own colour, so the stem\'s flanks vanish where they enter the '
                                      'well (architect 2026-10-07 ~06:10)'),
    'playhead_stem': ('#FFFFFF', None, 'white, his ruling (CL10a)'),
    'scanner': ('#FFFFFF', None, 'white, his ruling (CL10a)'),
}
GNOME2_PROGRAM_NOTE = ('the program roles are the architect\'s picks on the CL10 sheets (2026-10-07 ~05:30): the '
                       'waveform black under the selection blue lit to its 1.3 shade, the selected flag\'s byte '
                       '(architect 2026-10-07 ~09:00: spot[1] too dark on black), bg[SELECTED] the lit outline; one '
                       'flag pair for warp and phase reset, bg[SELECTED] lit to its 1.3 shade under black labels; the '
                       'history\'s GNOME HIG green and red lit to 1.3, the invalid flag wearing the red pair; '
                       'the playhead stem and the scanner white; under '
                       'Clearlooks the flag outline is the canvas\'s colour, so the stem\'s flanks vanish where they '
                       'enter the well (architect 2026-10-07 ~06:10)')


def gnome2_program_roles(sc):
    """The colour scheme `sc` (gtkrc key -> '#RRGGBB') -> ({role: '#RRGGBB'}, {role: provenance}) by
    GNOME2_PROGRAM_ROLES, each shade ge_shade_color through cairo (T.gtk2_shade, T.cairo_byte)."""
    unit = lambda h: tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))
    out, why = {}, {}
    for role, (src, k, prov) in GNOME2_PROGRAM_ROLES.items():
        literal = src.startswith('#')
        base = src if literal else sc[src]
        out[role] = base.upper() if k is None else hx(T.cairo_byte(T.gtk2_shade(unit(base), k)))
        why[role] = (src if literal else f'gtkrc {src} {base}') + \
            ('' if k is None else f', ge_shade_color x {k}') + f': {prov}'
    return out, why


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
    # THE PAINTERS' TONES (engine_tones, above): every ramp's rule must land within one level of the period's rows
    tones = engine_tones(g, m, gtk, sc, clearlooks_geometry())
    off = {k: v for k, v in tones.fit.items() if v > 1}
    if off: raise SystemExit(f'build: the app\'s ramp rule misses the period\'s rows by more than one level: {off}')
    e['engine_tones'] = {role: hx(v) for role, (v, _) in tones.tones.items()}
    e['provenance']['rule']['engine_tones'] = {role: why for role, (_, why) in tones.tones.items()}
    e['program_roles'], e['provenance']['rule']['program_roles'] = gnome2_program_roles(raw)
    e['notes'].append(GNOME2_PROGRAM_NOTE)
    print(f'gnome2: {len(tones.tones)} engine tones; the ramp rule off the period by at most '
          f'{max(tones.fit.values())} level(s) ({sum(1 for v in tones.fit.values() if v == 0)} of {len(tones.fit)} ramps exact)')
    return [e]


# ------------------------------------------------------------------ the Clearlooks painters' tones (gnome2)
# THE ENGINE'S TONES (architect 2026-10-07, the painters round; src/gui/clearlooks_paint.h states the rules the app
# paints them by): every colour a Clearlooks painter puts down is a theme role `cl_<element>_<stop>`, and its byte is
# the engine's own arithmetic on the gtkrc's colours run ONCE here — gtk-engines 2.20.2's Clearlooks (GUMMY) through
# cairo 1.8 / pixman 0.16 for the GTK widgets, metacity 2.30's draw_ops through GDK for the caption — at THE
# PRODUCT'S GEOMETRY, read off src/gui/chrome_spec.h's kChromeSpecClearlooks (clearlooks_geometry), so a length
# changed there is re-run here and nowhere else. THREE KINDS OF TONE:
#   A SOLID — a shade, mix or blend of a scheme colour, painted as one opaque byte (a line, a ring, a border, a text).
#   A RAMP'S ENDS — a vertical ramp is recorded as the FIRST AND LAST ROWS THE PERIOD PAINTED of each segment the
#     product paints (the renderer's own arithmetic: toolkit_rules.pixman_vertical_ramp_row, the GTK ramps' pixman
#     sample at the row centre; metacity_vertical_gradient_rows, metacity's 16.16 stepping), and the app interpolates
#     between them by the one rule (paint_cl_ramp: round(s + (e − s)·i/(n − 1)) per device row), so at 1 W px the end
#     rows are the period's pixels and every row between lands within one level of them (ramp_fit, reported by
#     build.py and asserted <= 1).
#   A BAKED COMPOSITE — where the engine strokes a translucent colour over a known fill (the gummy button's 0.4
#     top-left highlight, the pressed button's 0.58 -> 0 inner shadow), the composite over that fill's recorded row,
#     by pixman's OVER (toolkit_rules.cairo_solid_over, pixman_over): the app paints opaque bytes only (render.h's
#     palette head). Over a ramp the composite is itself a ramp's ends, per row or per column.
# The role names are the C++ painters' (clearlooks_paint.cpp reads each by name; a renamed tone is a compile error
# there), and they are written into the clearlooks entry's `engine_tones`, which gen_theme_files.py carries into
# assets/themes/clearlooks.theme and the two generated includes.
CHROME_SPEC_H = os.path.join(REPO, 'src', 'gui', 'chrome_spec.h')


def clearlooks_geometry():
    """kChromeSpecClearlooks's lengths, in W px (= GTK px), read off src/gui/chrome_spec.h."""
    text = open(CHROME_SPEC_H).read()
    body = text[text.index('kChromeSpecClearlooks = {'):]
    body = body[:body.index('};')]
    f = dict(re.findall(r'\.(\w+)\s*=\s*([^,\n]+),', body))
    i = lambda k: int(f[k])
    case_h = i('toolbar_case_lead_px') + i('toolbar_glyph_px') + i('toolbar_case_trail_y_px')
    # the icon row's band (row 8 is GTK's status bar since the painters round's last part, its line the separator's
    # pair, so the toolbar tones are the icon row's alone)
    band = 2 * i('icon_row_air_px') + case_h
    # the push button's height is a double on the spec (push_button_box_px); a whole W px here
    push_h = float(f['push_button_box_px'])
    if push_h != int(push_h): raise SystemExit(f'build: push_button_box_px {push_h} is not a whole W px')
    # THE TWO LENGTHS THE SPEC DOES NOT CARRY (both vocabularies' own, one constant at its owner): the dialog
    # field's height (paint_handler.cpp kModalFieldHeightPx: Windows' 23, GTK's entry at the 13-row cell, 13 + 2 x
    # (ythickness 3 + inner-border 2)) and the list row (folder_overlay.h kRowHeightPx)
    const = lambda path, name: float(re.search(name + r'\s*=\s*([0-9.]+)', open(path).read()).group(1))
    entry_h = const(os.path.join(REPO, 'src', 'gui', 'paint_handler.cpp'), 'kModalFieldHeightPx')
    row_h = const(os.path.join(REPO, 'src', 'gui', 'folder_overlay.h'), 'kRowHeightPx')
    assert entry_h == int(entry_h) and row_h == int(row_h), (entry_h, row_h)
    # THE PAINTERS ROUND'S LAST PART'S LENGTHS: the trim lane's height (render.h kTrimLaneHeightPx, Windows' 16, the
    # scroll bar's thickness), the restored laptop's sizing frame (render.h kWindowFramePx, the band metacity's title
    # bar starts in) and GtkScale's three (clearlooks_paint.h: TROUGH_SIZE, slider-length and slider-width)
    render_h = os.path.join(REPO, 'src', 'gui', 'render.h')
    cl_h = os.path.join(REPO, 'src', 'gui', 'clearlooks_paint.h')
    trim_h, frame_w = const(render_h, 'kTrimLaneHeightPx'), const(render_h, 'kWindowFramePx')
    scale = {k: const(cl_h, n) for k, n in (('trough', 'kClScaleTroughPx'), ('len', 'kClScaleSliderLengthPx'),
                                            ('wid', 'kClScaleSliderWidthPx'))}
    assert all(v == int(v) for v in (trim_h, frame_w, *scale.values())), (trim_h, frame_w, scale)
    return {'caption_h': i('caption_height_px'), 'cbtn_w': i('caption_button_w_px'), 'cbtn_h': i('caption_button_h_px'),
            'menu_head': i('menu_row_head_px'), 'menu_content': i('menu_row_content_px'),
            'menu_foot': i('menu_row_foot_px'), 'case_h': case_h, 'band_h': band, 'push_h': int(push_h),
            'entry_h': int(entry_h), 'row_h': int(row_h), 'menu_item_h': i('popup_item_height_px'),
            'trim_h': int(trim_h), 'frame_w': int(frame_w), 'scale_trough': int(scale['trough']),
            'scale_len': int(scale['len']), 'scale_wid': int(scale['wid'])}


def mc_eval(expr, env):
    """A metacity position expression (theme.c's integer arithmetic: + - * /, parentheses, `max`; / truncates)."""
    toks = re.findall(r'\d+|[A-Za-z_]+|`max`|[-+*/()]', expr)
    pos = [0]
    peek = lambda: toks[pos[0]] if pos[0] < len(toks) else None
    def take():
        pos[0] += 1
        return toks[pos[0] - 1]
    def prim():
        t = take()
        if t == '(':
            v = top(); take(); return v
        if t == '-': return -prim()
        return int(t) if t.isdigit() else env[t]
    def mul():
        v = prim()
        while peek() in ('*', '/'):
            op, r = take(), prim()
            v = v * r if op == '*' else int(v / r)
        return v
    def add():
        v = mul()
        while peek() in ('+', '-'):
            op, r = take(), mul()
            v = v + r if op == '+' else v - r
        return v
    def top():
        v = add()
        while peek() == '`max`':
            take(); v = max(v, add())
        return v
    v = top()
    assert pos[0] == len(toks), expr
    return v


def mc_byte(spec, gtk):
    return T.gdk_byte(metacity_color(spec, gtk))


def mc_render(ops, w, h, gtk, env_extra, ramp_rule=None):
    """metacity's axis-aligned draw ops (<line> of width 1, vertical <gradient>) on a w x h grid of 8-bit pixels, in
    order — the period's picture, or, with `ramp_rule`, the app's (each gradient the one rule between its end rows)."""
    env = dict(width=w, height=h, Bmin=7, Bpad=6, **env_extra)
    grid = [[None] * w for _ in range(h)]
    for op in ops:
        if op['op'] == 'line':
            assert op.get('width', '1') in ('0', '1'), op
            x1, y1, x2, y2 = (mc_eval(op[a], env) for a in ('x1', 'y1', 'x2', 'y2'))
            assert x1 == x2 or y1 == y2, op
            for y in range(min(y1, y2), max(y1, y2) + 1):
                for x in range(min(x1, x2), max(x1, x2) + 1):
                    if 0 <= x < w and 0 <= y < h: grid[y][x] = mc_byte(op['color'], gtk)
        elif op['op'] == 'gradient':
            assert op['type'] == 'vertical' and len(op['colors']) == 2, op
            x, y, gw, gh = (mc_eval(op[a], env) for a in ('x', 'y', 'width', 'height'))
            rows = T.metacity_vertical_gradient_rows(*(metacity_color(c, gtk) for c in op['colors']), gh)
            if ramp_rule: rows = ramp_rule(rows[0], rows[-1], gh)
            for i in range(gh):
                for xx in range(x, x + gw):
                    if 0 <= xx < w and 0 <= y + i < h: grid[y + i][xx] = rows[i]
        else:
            raise SystemExit(f'build: metacity op <{op["op"]}> is not one the caption painter draws')
    return grid


def ramp_rule(first, last, n):
    """THE APP'S RAMP RULE (paint_cl_ramp, clearlooks_paint.cpp) at 1 W px: n rows from `first` to `last`, each
    channel round(s + (e − s)·i/(n − 1)), C's nearbyint (half to even, Python's round)."""
    if n == 1: return [first]
    return [tuple(int(round(s + (e - s) * i / (n - 1))) for s, e in zip(first, last)) for i in range(n)]


class Tones:
    """The ordered tones: role -> (8-bit rgb, its derivation), and each ramp's fit of the app's rule."""
    def __init__(self):
        self.tones, self.fit = {}, {}

    def add(self, role, rgb, rule):
        assert re.fullmatch(r'cl_[a-z0-9_]+', role) and role not in self.tones, role
        self.tones[role] = (tuple(int(v) for v in rgb), rule)

    def ramp(self, role, rows, first, last, rule):
        """A ramp segment's two ends (rows[first], rows[last]) as `role`_0 / _1, and the fit of the rule between them."""
        seg = rows[first:last + 1]
        self.add(role + '_0', seg[0], f'{rule}, its row {first}')
        self.add(role + '_1', seg[-1], f'{rule}, its row {last}')
        pred = ramp_rule(seg[0], seg[-1], len(seg))
        self.fit[role] = max(max(abs(a - b) for a, b in zip(p, q)) for p, q in zip(pred, seg))


def engine_tones(g, m, gtk, sc, geo):
    """The Clearlooks painters' tones (the head) -> Tones."""
    t = Tones()
    unit = lambda c16: tuple(v / 65535.0 for v in c16)
    cb = T.cairo_byte
    sh, mix = T.gtk2_shade, T.gtk2_mix
    style_bg = lambda style, state: unit(gtkrc_color(g['styles'][style]['colors'].get(
        f'bg[{state}]', g['styles']['default']['colors'][f'bg[{state}]']), sc))
    style_c16 = lambda style, comp, state: gtkrc_color(g['styles'][style]['colors'].get(
        f'{comp}[{state}]', g['styles']['default']['colors'][f'{comp}[{state}]']), sc)
    bg, sel = unit(gtk[('bg', 'NORMAL')]), unit(gtk[('bg', 'SELECTED')])
    SH = [sh(bg, k) for k in (1.15, 0.95, 0.896, 0.82, 0.7, 0.665, 0.475, 0.45, 0.4)]   # clearlooks_style_realize
    SP = [sh(sel, k) for k in (1.25, 1.05, 0.65)]
    gummy = lambda c, dis: [(0.0, sh(c, 1.04 if dis else 1.08)), (0.5, sh(c, 1.01 if dis else 1.02)),
                            (0.5, sh(c, 0.99) if dis else c), (1.0, sh(c, 0.96 if dis else 0.94))]   # gummy_gradient

    # THE CAPTION (metacity: the maximised frame's bevel, the title, the buttons and their glyphs; GDK's byte road)
    H = geo['caption_h']
    env = dict(top_height=H, title_height=H - 7, mini_icon_width=16, mini_icon_height=16)
    for state, ops_name, light in (('', 'bevel_maximized', False), ('unfocused_', 'bevel_maximized_unfocused', True)):
        ops = draw_ops_flat(m, ops_name)
        col = [r[4] for r in mc_render(ops, 9, 1000, gtk, env)][:H]
        rule = f'metacity draw_ops {ops_name} at a {H}-row caption'
        t.add(f'cl_caption_{state}edge', col[0], rule + ', its row 0')
        if light: t.add(f'cl_caption_{state}light', col[1], rule + ', its row 1')
        t.ramp(f'cl_caption_{state}upper', col, 2 if light else 1, H // 2 - 1, rule + ': the upper gradient')
        t.ramp(f'cl_caption_{state}lower', col, H // 2, H - 2, rule + ': the lower gradient')
        t.add(f'cl_caption_{state}foot', col[H - 1], rule + f', its row {H - 1}')
    title = draw_ops_flat(m, 'title_text')
    t.add('cl_title_text', mc_byte(title[-1]['color'], gtk), 'metacity draw_ops title_text: the title')
    t.add('cl_title_shadow', mc_byte(title[0]['color'], gtk), 'metacity draw_ops title_text: its four shadow copies')
    t.add('cl_title_unfocused', mc_byte(draw_ops_flat(m, 'title_text_unfocused')[0]['color'], gtk),
          'metacity draw_ops title_text_unfocused')
    close, close_un = draw_ops_flat(m, 'close_button_icon'), draw_ops_flat(m, 'close_button_icon_unfocused')
    t.add('cl_cglyph_dark', mc_byte(close[0]['color'], gtk), 'metacity close_button_icon: the outline (and every glyph\'s)')
    t.add('cl_cglyph_light', mc_byte(close[-1]['color'], gtk), 'metacity close_button_icon: the cross (and every glyph\'s)')
    t.add('cl_cglyph_unfocused', mc_byte(close_un[0]['color'], gtk), 'metacity close_button_icon_unfocused (every glyph\'s)')
    bw, bh = geo['cbtn_w'], geo['cbtn_h']
    for state, ops_name in CAPTION_BUTTON_STATES:
        ops = draw_ops_flat(m, ops_name)
        period = mc_render(ops, bw, bh, gtk, env)
        app = mc_render(ops, bw, bh, gtk, env, ramp_rule)
        n = 0
        for op in ops:
            rule = f'metacity draw_ops {ops_name}'
            if op['op'] == 'line':
                role = caption_button_line_role(state, op)
                if role not in t.tones and role[len(f'cl_cbtn_{state}_'):] in CAPTION_BUTTON_DRAWN[state]:
                    t.add(role, mc_byte(op['color'], gtk), f'{rule}: <line> {op["color"]}')
            else:
                gh = mc_eval(op['height'], dict(width=bw, height=bh))
                rows = T.metacity_vertical_gradient_rows(*(metacity_color(c, gtk) for c in op['colors']), gh)
                t.ramp(f'cl_cbtn_{state}_ramp{n}', rows, 0, gh - 1, f'{rule}: <gradient> {n}')
                n += 1
        t.fit[f'cl_cbtn_{state} (whole box)'] = max(max(abs(a - b) for a, b in zip(p, q))
                                                     for pr, ar in zip(period, app) for p, q in zip(pr, ar) if p)

    # THE MENU BAR (clearlooks_draw_menubar2) and its OPEN TITLE (clearlooks_gummy_draw_menubaritem), the texts
    bar_h = geo['menu_head'] + geo['menu_content'] + geo['menu_foot']
    rows = [T.pixman_vertical_ramp_row([(0.0, bg), (1.0, sh(bg, 0.96))], 0, bar_h, r) for r in range(bar_h)]
    t.ramp('cl_menubar_ramp', rows, 0, bar_h - 2, f'clearlooks_draw_menubar2: bg -> 0.96 over the {bar_h}-row bar')
    t.add('cl_menubar_shadow', cb(SH[3]), 'clearlooks_draw_menubar2: its last row, shade[3]')
    t.add('cl_menubar_text', T.gdk_byte(gtk[('fg', 'NORMAL')]), 'the default style\'s fg[NORMAL]')
    t.add('cl_text_insensitive', T.gdk_byte(gtk[('fg', 'INSENSITIVE')]),
          'fg[INSENSITIVE] = darker (bg_color) (clearlooks_style_draw_layout\'s layout)')
    t.add('cl_text_insensitive_etch', tuple(int(v * 65535) >> 8 for v in sh(bg, 1.2)),
          'clearlooks_style_draw_layout: the etched copy at (+1, +1), shade (parentbg, 1.2) as a GdkColor')
    y0, ih = geo['menu_head'], geo['menu_content'] + 1
    rows = {r: T.pixman_vertical_ramp_row(gummy(SP[1], False), y0, y0 + ih, r) for r in range(y0, y0 + ih)}
    step = T.pixman_step_row(y0, y0 + ih)
    assert step == y0 + (ih + 1) // 2, step
    rule = f'clearlooks_gummy_draw_menubaritem: spot[1]\'s gummy ramp over rows {y0}..{y0 + ih - 1}'
    seg = lambda a, b: [rows[r] for r in range(a, b + 1)]
    t.ramp('cl_menubaritem_upper', seg(y0 + 1, step - 1), 0, step - 2 - y0, rule + ' (above the step)')
    t.ramp('cl_menubaritem_lower', seg(step, y0 + ih - 2), 0, y0 + ih - 2 - step, rule + ' (below it)')
    t.add('cl_menubaritem_border', cb(SP[2]), 'clearlooks_gummy_draw_menubaritem: the border, spot[2]')
    t.add('cl_menubaritem_text', T.gdk_byte(gtkrc_color(g['styles']['menu_item']['colors']['fg[PRELIGHT]'], sc)),
          'the menu_item style\'s fg[PRELIGHT] (selected_fg_color)')

    # THE TOOLBAR BAND (clearlooks_gummy_draw_toolbar, toolbarstyle 1, not topmost) and its SEPARATOR
    bh_ = geo['band_h']
    stops = [(0.0, sh(bg, 1.04)), (0.5, sh(bg, 1.01)), (0.5, bg), (1.0, sh(bg, 0.97))]
    rows = [T.pixman_vertical_ramp_row(stops, 0, bh_, r) for r in range(bh_)]
    step = T.pixman_step_row(0, bh_)
    rule = f'clearlooks_gummy_draw_toolbar: 1.04 | 1.01 / 1.0 | 0.97 of bg over the {bh_}-row band'
    t.add('cl_toolbar_light', cb(sh(bg, 1.1)), 'clearlooks_gummy_draw_toolbar: its first row, shade (bg, 1.1)')
    t.ramp('cl_toolbar_upper', rows, 1, step - 1, rule + ' (above the step)')
    t.ramp('cl_toolbar_lower', rows, step, bh_ - 2, rule + ' (below it)')
    t.add('cl_toolbar_shadow', cb(SH[3]), 'clearlooks_gummy_draw_toolbar: its last row, shade[3]')
    t.add('cl_separator_dark', cb(SH[3]), 'clearlooks_gummy_draw_separator: shade[3]')
    t.add('cl_separator_light', cb(sh(SH[3], 1.3)), 'clearlooks_gummy_draw_separator: shade (shade[3], 1.3)')

    # THE TOOL BUTTON (clearlooks_gummy_draw_button on the "button" style, xthickness 3, relief none): drawn HOT
    # (prelight), PRESSED or CHECKED (active), HOT AND CHECKED (prelight + active) and DEAD AND CHECKED (insensitive +
    # active); at rest and dead nothing (GtkButton paints no box for relief none)
    ch = geo['case_h']
    pbg = bg                                               # the toolbar's bg[NORMAL], the button's parentbg
    # THE BUTTON STYLE'S OWN SHADE TABLE: clearlooks_style_realize shades each style's bg[NORMAL], and the button
    # style's is 1.04 of bg, so its borders' shade[6] / shade[4] are not the window's (his capture: the hot border
    # #928F8D, Nautilus' toolbar, 23-23-16)
    BSH = [sh(style_bg('button', 'NORMAL'), k) for k in (1.15, 0.95, 0.896, 0.82, 0.7, 0.665, 0.475, 0.45, 0.4)]
    t.add('cl_button_ring_outer', cb(sh(pbg, 0.97)), 'gummy button, reliefstyle 1: the outer ring, shade (parentbg, 0.97)')
    t.add('cl_button_ring_inner', cb(sh(pbg, 0.93)), 'gummy button, reliefstyle 1: the inner ring, shade (parentbg, 0.93)')
    t.add('cl_button_inset_dark', cb(sh(pbg, 0.94)), 'clearlooks_draw_inset: its top-left half, shade (parentbg, 0.94)')
    t.add('cl_button_inset_light', cb(sh(pbg, 1.06)), 'clearlooks_draw_inset: its bottom-right half, shade (parentbg, 1.06)')
    # ONE GUMMY FACE (clearlooks_gummy_draw_button's fill, border and highlight or inner shadow) on a box `h` rows
    # tall: the ramp over its rows 2 .. h - 3 with its step where pixman put it, the border, and then either the
    # top-left highlight (not active) or the pressed inner shadow (active), each baked over the ramp's rows
    def gummy_face(prefix, h, fill, active, disabled, border, rule):
        y0, y1 = 2, h - 2
        step = T.pixman_step_row(y0, y1)
        assert step == y0 + (y1 - y0 + 1) // 2, (h, step)     # the painter's step (clearlooks_paint.cpp paint_gummy)
        ramp = {r: T.pixman_vertical_ramp_row(gummy(fill, disabled), y0, y1, r) for r in range(y0, y1)}
        first = 5 if active else 2
        segs = ((first, step - 1), (step, h - 3))
        for name, (a, b) in zip(('upper', 'lower'), segs):
            t.ramp(f'{prefix}_{name}', [ramp[r] for r in range(a, b + 1)], 0, b - a,
                   f'{rule}: its ramp, rows {a}..{b}')
        t.add(f'{prefix}_border', border[0], rule + border[1])
        if not active:                                     # the top-left highlight, shade (fill, 1.3) at 0.4
            hi = sh(fill, 1.3)
            over = lambda r: T.cairo_solid_over(hi, 0.4, ramp[r])
            t.add(f'{prefix}_highlight_row', over(y0), rule + ': the top-left highlight over its first row')
            for name, (a, b) in zip(('upper', 'lower'), ((6, step - 1), (step, h - 6))):
                t.ramp(f'{prefix}_highlight_{name}', [over(r) for r in range(a, b + 1)], 0, b - a,
                       rule + f': the top-left highlight down its left column, rows {a}..{b}')
            return
        shadow = cb(sh(fill, 0.92))                         # the pressed shadow, 0.58 -> 0 over three px
        alpha = [T.pixman_alpha_ramp_alpha(0.58, 2, 5, k) for k in (2, 3, 4)]
        under = {k: T.pixman_over(shadow, alpha[k - 2], ramp[k]) for k in (2, 3, 4)}
        for k in (2, 3, 4):
            t.add(f'{prefix}_shadow_row{k - 2}', under[k], rule + f': the inner shadow\'s row {k} over the ramp')
        for c in (2, 3, 4):
            for name, (a, b) in zip(('upper', 'lower'), segs):
                t.ramp(f'{prefix}_shadow_col{c - 2}_{name}',
                       [T.pixman_over(shadow, alpha[c - 2], ramp[r]) for r in range(a, b + 1)], 0, b - a,
                       rule + f': the inner shadow\'s column {c} over the ramp, rows {a}..{b}')
            for k in (2, 3, 4):
                t.add(f'{prefix}_shadow_corner{k - 2}{c - 2}', T.pixman_over(shadow, alpha[c - 2], under[k]),
                      rule + f': the inner shadow\'s column {c} over its row {k}')

    border_of = lambda fill, disabled: ((cb(BSH[4]), ': the border, the button style\'s shade[4]') if disabled else
                                        (cb(mix(BSH[6], fill, 0.2)),
                                         ': the border, mix (the button style\'s shade[6], fill, 0.2)'))
    for state, style_state, active, disabled in TOOL_BUTTON_STATES:
        fill = style_bg('button', style_state)
        gummy_face(f'cl_button_{state}', ch, fill, active, disabled, border_of(fill, disabled),
                   f'gummy button {state} (bg[{style_state}] of the button style)')

    # THE PUSH BUTTON (clearlooks_gummy_draw_button on the "button" style, reliefstyle 1) at the spec's push button
    # height: NORMAL (bg[NORMAL] 1.04, the two shadow rings, the highlight), PRESSED (bg[ACTIVE] 0.85, the inset ring,
    # the inner shadow), DISABLED (bg[INSENSITIVE], the disabled ramp, the inset ring, the highlight, the shade[4]
    # border); THE DEFAULT BUTTON (is_default) one ring mix (parentbg, spot[1], 0.5) and the border mix (spot[2],
    # fill, 0.2) — pressed, the inset over its ring and that border. The rings and the inset are the tool button's
    # (the same parentbg). The label fg[NORMAL] of the button style.
    ph = geo['push_h']
    for state, style_state, active, disabled in PUSH_BUTTON_STATES:
        fill = style_bg('button', style_state)
        gummy_face(f'cl_push_{state}', ph, fill, active, disabled, border_of(fill, disabled),
                   f'gummy push button {state} at {ph} rows (bg[{style_state}] of the button style)')
    t.add('cl_push_default_ring', cb(mix(pbg, SP[1], 0.5)),
          'gummy push button, is_default: its one ring, mix (parentbg, spot[1], 0.5)')
    for state, style_state in (('normal', 'NORMAL'), ('pressed', 'ACTIVE')):
        t.add(f'cl_push_{state}_default_border', cb(mix(SP[2], style_bg('button', style_state), 0.2)),
              f'gummy push button {state}, is_default: the border, mix (spot[2], bg[{style_state}], 0.2)')
    t.add('cl_push_text', T.gdk_byte(style_c16('button', 'fg', 'NORMAL')), 'the button style\'s fg[NORMAL]')

    # THE ENTRY (clearlooks_gummy_draw_entry, the "entry" style: xthickness 3, its focus_color) at the dialog field's
    # height: the inset ring is the tool button's (draw_inset on the same parentbg); base[NORMAL] inside; unfocused the
    # shade[6] border and the 0.18 inner shadow baked over base; focused the focus_color border and its inner ring.
    # THE TEXT AND THE SELECTION are GtkEntry's own (gtkentry.c draws the selection in base[SELECTED] under
    # text[SELECTED] while the entry has the focus, base[ACTIVE] under text[ACTIVE] while it has not; the entry
    # style's bg[SELECTED] / fg[SELECTED] are its progress bar's); the list's rows read the same base and text.
    base = unit(gtk[('base', 'NORMAL')])
    assert gtk[('text', 'SELECTED')] == gtk[('text', 'ACTIVE')]
    t.add('cl_base', T.gdk_byte(gtk[('base', 'NORMAL')]), 'base[NORMAL] (the entry\'s and the list\'s ground)')
    t.add('cl_text', T.gdk_byte(gtk[('text', 'NORMAL')]), 'text[NORMAL] (the entry\'s and the list\'s text)')
    t.add('cl_text_selected', T.gdk_byte(gtk[('text', 'SELECTED')]),
          'text[SELECTED] = text[ACTIVE] (a selection\'s text, focused or not)')
    t.add('cl_selection', T.gdk_byte(gtk[('base', 'SELECTED')]), 'base[SELECTED] (GtkEntry\'s selection, focused)')
    t.add('cl_selection_unfocused', T.gdk_byte(gtk[('base', 'ACTIVE')]),
          'base[ACTIVE] (GtkEntry\'s selection, unfocused)')
    eh = geo['entry_h']
    ent = g['styles']['entry']
    assert 'bg[NORMAL]' not in ent['colors'], ent                  # the entry's shade table is the window's
    focus = unit(gtkrc_color(ent['engines']['clearlooks']['focus_color'], sc))
    t.add('cl_entry_border', cb(SH[6]), 'gummy entry: the border, shade[6]')
    t.add('cl_entry_shadow', T.cairo_solid_over(sh(SH[6], 0.92), 0.18, cb(base)),
          'gummy entry, unfocused: the inner shadow, shade (border, 0.92) at 0.18 over base')
    t.add('cl_entry_focus_border', cb(focus), 'gummy entry, focused: the border, the entry style\'s focus_color')
    t.add('cl_entry_focus_ring', cb(mix(base, sh(focus, 1.61), 0.5)),
          'gummy entry, focused: the inner ring, mix (base, shade (focus_color, 1.61), 0.5)')
    assert eh >= 8, eh

    # THE DROPDOWN (the "menu" style: bg 1.08, x/ythickness 0, radius 0): its ground, clearlooks_draw_menu_frame's
    # shade[5] of THE MENU STYLE'S table, the separator item's one shade[5] row
    # (clearlooks_draw_menu_item_separator), and the lit item (clearlooks_gummy_draw_menuitem: spot[1]'s gummy ramp
    # over the item's whole height in a spot[2] border at the menu's radius 0) at the spec's item height
    menu_bg = style_bg('menu', 'NORMAL')
    t.add('cl_menu_ground', T.gdk_byte(style_c16('menu', 'bg', 'NORMAL')), 'the menu style\'s bg[NORMAL], shade (bg, 1.08)')
    t.add('cl_menu_frame', cb(sh(menu_bg, 0.665)), 'clearlooks_draw_menu_frame: the menu style\'s shade[5]')
    t.add('cl_menu_separator', cb(sh(menu_bg, 0.665)),
          'clearlooks_draw_menu_item_separator: the menu style\'s shade[5], one row')
    t.add('cl_menu_text', T.gdk_byte(style_c16('menu_item', 'fg', 'NORMAL')), 'the menu_item style\'s fg[NORMAL]')
    mi = geo['menu_item_h']
    rows = {r: T.pixman_vertical_ramp_row(gummy(SP[1], False), 0, mi, r) for r in range(mi)}
    step = T.pixman_step_row(0, mi)
    assert step == (mi + 1) // 2, step
    rule = f'clearlooks_gummy_draw_menuitem: spot[1]\'s gummy ramp over the {mi}-row item'
    t.ramp('cl_menuitem_upper', [rows[r] for r in range(1, step)], 0, step - 2, rule + ' (above the step)')
    t.ramp('cl_menuitem_lower', [rows[r] for r in range(step, mi - 1)], 0, mi - 2 - step, rule + ' (below it)')
    t.add('cl_menuitem_border', cb(SP[2]), 'clearlooks_gummy_draw_menuitem: the border, spot[2]')
    t.add('cl_menuitem_text', T.gdk_byte(style_c16('menu_item', 'fg', 'PRELIGHT')),
          'the menu_item style\'s fg[PRELIGHT] (selected_fg_color)')

    # THE LIST (a GtkTreeView in a GtkScrolledWindow, shadow IN): the scrolled window's one shade[5] line
    # (clearlooks_style_draw_shadow's "scrolled_window" arm, GUMMY), base[NORMAL] inside, the selected row
    # clearlooks_gummy_draw_selected_cell — the gummy ramp of base[SELECTED] with the focus, of base[ACTIVE] without,
    # over the row's whole height, no border
    t.add('cl_list_frame', cb(SH[5]), 'the scrolled window\'s shadow IN: one shade[5] line')
    rh = geo['row_h']
    step = T.pixman_step_row(0, rh)
    assert step == (rh + 1) // 2, step
    for name, state in (('selected', 'SELECTED'), ('selected_unfocused', 'ACTIVE')):
        fill = unit(gtk[('base', state)])
        rows = [T.pixman_vertical_ramp_row(gummy(fill, False), 0, rh, r) for r in range(rh)]
        rule = f'clearlooks_gummy_draw_selected_cell: base[{state}]\'s gummy ramp over the {rh}-row row'
        t.ramp(f'cl_list_{name}_upper', rows, 0, step - 1, rule + ' (above the step)')
        t.ramp(f'cl_list_{name}_lower', rows, step, rh - 1, rule + ' (below it)')

    # THE TRIM LANE = GTK'S HORIZONTAL SCROLL BAR (the painters round's last part; the GtkRange style properties of the
    # default style, slider-width / stepper-size 15, trough-border 0, colorize_scrollbar TRUE) at the lane's own
    # thickness, Windows' 16 (geo['trim_h']): every ramp below runs across those 16 rows.
    th = geo['trim_h']
    # THE TROUGH (clearlooks_draw_scrollbar_trough, the classic one gummy keeps, its vertical frame's axes exchanged):
    # shade[2] inside a one-px shade[5] rectangle, the 0.95 -> 1.0 shadow a linear gradient from row 1 to row 3 over
    # the fill's rows 1..4 — rows 1 and 2 are the gradient's (their centres at 1/4 and 3/4 of it), row 3 on is shade[2]
    t.add('cl_trough_fill', cb(SH[2]), 'clearlooks_draw_scrollbar_trough: the fill, shade[2]')
    t.add('cl_trough_border', cb(SH[5]), 'clearlooks_draw_scrollbar_trough: the border, shade[5]')
    shadow_stops = [(0.0, sh(SH[2], 0.95)), (1.0, SH[2])]
    srows = [T.pixman_vertical_ramp_row(shadow_stops, 1, 3, r) for r in range(1, 4)]
    assert srows[2] == tuple(T._cairo_short(v) >> 8 for v in SH[2]), srows          # row 3 is the fill's own byte
    t.ramp('cl_trough_shadow', srows[:2], 0, 1,
           'clearlooks_draw_scrollbar_trough: the shadow, shade (shade[2], 0.95) -> shade[2] from row 1 to row 3, '
           'its rows 1..2')
    # THE STEPPERS (clearlooks_gummy_draw_scrollbar_stepper, the bar horizontal): bg[state]'s gummy ramp from row 0 to
    # row th over the fill's rows 1 .. th - 2, the top-left highlight (shade (fill, 1.3) at 0.4, gummy's constants)
    # along row 1 and down column 1, the border mix (shade[7], fill, 0.2) (colorize_scrollbar: has_color). RESTING
    # bg[NORMAL]; PRESSED bg[ACTIVE] = shade (0.9, bg), the default style's (the scrollbar style overrides no colour) —
    # his capture 00-12-13's pressed up stepper. No prelight (the product draws one hover face, the toolbars').
    step = T.pixman_step_row(0, th)
    for state, style_state in (('normal', 'NORMAL'), ('pressed', 'ACTIVE')):
        fill = style_bg('scrollbar', style_state)
        ramp = {r: T.pixman_vertical_ramp_row(gummy(fill, False), 0, th, r) for r in range(1, th - 1)}
        rule = f'clearlooks_gummy_draw_scrollbar_stepper {state} (bg[{style_state}]) across the {th}-row bar'
        t.ramp(f'cl_stepper_{state}_upper', [ramp[r] for r in range(1, step)], 0, step - 2, rule + ', rows 1..'
               f'{step - 1}')
        t.ramp(f'cl_stepper_{state}_lower', [ramp[r] for r in range(step, th - 1)], 0, th - 2 - step,
               rule + f', rows {step}..{th - 2}')
        t.add(f'cl_stepper_{state}_border', cb(mix(SH[7], fill, 0.2)), rule + ': the border, mix (shade[7], fill, 0.2)')
        hi = sh(fill, 1.3)
        over = lambda r: T.cairo_solid_over(hi, 0.4, ramp[r])
        t.add(f'cl_stepper_{state}_highlight_row', over(1), rule + ': the top-left highlight over its row 1')
        t.ramp(f'cl_stepper_{state}_highlight_upper', [over(r) for r in range(2, step)], 0, step - 3,
               rule + f': the top-left highlight down its column 1, rows 2..{step - 1}')
        t.ramp(f'cl_stepper_{state}_highlight_lower', [over(r) for r in range(step, th - 1)], 0, th - 2 - step,
               rule + f': the top-left highlight down its column 1, rows {step}..{th - 2}')
    t.add('cl_stepper_arrow', T.gdk_byte(gtk[('fg', 'NORMAL')]),
          'clearlooks_draw_arrow: fg[state] (fg[NORMAL] = fg[ACTIVE] = fg_color), the normal arrow')
    assert gtk[('fg', 'NORMAL')] == gtk[('fg', 'ACTIVE')]
    # THE SLIDER IS THE PRODUCT'S OWN (architect 2026-10-07, the CL15 sheets: "Clearlooks is anonymous enough that we
    # can get away with our own scroll bar"): the window ground between the gummy separator's two lines, roles already
    # made above (cl_separator_light along its top row, cl_separator_dark along its bottom), so it adds no tone here —
    # gummy's spot[1] slider and its twelve tones retired with their painter (paint_cl_slider, clearlooks_paint.h).

    # THE SCRUB = GtkScale (the "scale" style: hint scale, the default style's slider-length 23, slider-width 15,
    # trough-side-details 1) at geo['scale_*']: THE TROUGH TROUGH_SIZE rows tall (clearlooks_gummy_draw_scale_trough):
    # draw_inset's ring (the tool button's inset tones, the same parentbg), then the gradient's rect at (1, 1) inside
    # a one-px inner rectangle of mix (border, fill, 0.2); the gradient from row 0.5 to row (TROUGH_SIZE - 2) + 1 over
    # the rows 1 .. TROUGH_SIZE - 2, of which the rows inside the border, 2 .. TROUGH_SIZE - 3, show. THE UPPER part
    # (right of the thumb) fill shade (parentbg, 0.896) under the "in" ramp 0.95 -> 1.05, border shade[6]; THE LOWER
    # (trough-lower, left of the thumb's centre) spot[1] under the "out" ramp 1.1 -> 0.9, border spot[2].
    st_ = geo['scale_trough']
    for name, fill, border, (k0, k1) in (('upper', sh(bg, 0.896), SH[6], (0.95, 1.05)),
                                         ('lower', SP[1], SP[2], (1.1, 0.9))):
        rows = [T.pixman_vertical_ramp_row([(0.0, sh(fill, k0)), (1.0, sh(fill, k1))], 0.5, st_ - 2 + 1.0, r)
                for r in range(2, st_ - 2)]
        rule = (f'clearlooks_gummy_draw_scale_trough {name} (trough-{name}): {k0} -> {k1} of its fill over the '
                f'{st_}-row trough')
        t.ramp(f'cl_scale_{name}', rows, 0, len(rows) - 1, rule + f', rows 2..{st_ - 3}')
        t.add(f'cl_scale_{name}_border', cb(mix(border, fill, 0.2)), rule + ': the border, mix (border, fill, 0.2)')
        if name == 'upper': upper_rows, upper_border = rows, cb(mix(border, fill, 0.2))
    # THE THUMB (clearlooks_gummy_draw_slider_button, slider-length x slider-width): draw_shadow — shade (shade[6],
    # 0.92) at 0.1 down its last column and along its last row, round the bottom-right corner at radius 3 — then
    # clearlooks_gummy_draw_slider inset 1 on bg[NORMAL]: the gummy ramp from row 1 to row h - 2 of the slider, the
    # border mix (shade[7], fill, 0.2) at radius 2.5, three shade[7] grip bars, the top-left highlight at radius 2.
    # THE SHADOW IS BAKED over what it crosses: the ground above and below the trough, and the upper trough's rows
    # (the thumb's right edge always stands right of its centre, over trough-upper): the inset ring's dark top row,
    # the border, the "in" ramp's rows and the inset ring's light bottom row.
    sw = sh(SH[6], 0.92)
    shadow = lambda dst: T.cairo_solid_over(sw, 0.1, dst)
    t.add('cl_scale_shadow', shadow(cb(bg)), 'clearlooks_draw_shadow: shade (shade[6], 0.92) at 0.1 over bg')
    t.add('cl_scale_shadow_inset_dark', shadow(cb(sh(bg, 0.94))), 'the thumb\'s shadow over the trough\'s inset row 0')
    t.add('cl_scale_shadow_border', shadow(upper_border), 'the thumb\'s shadow over the upper trough\'s border rows')
    t.ramp('cl_scale_shadow_ramp', [shadow(r) for r in upper_rows], 0, len(upper_rows) - 1,
           'the thumb\'s shadow over the upper trough\'s ramp rows')
    t.add('cl_scale_shadow_inset_light', shadow(cb(sh(bg, 1.06))), 'the thumb\'s shadow over the trough\'s inset row '
          f'{st_ - 1}')
    sh_h = geo['scale_wid'] - 2                                             # the slider inside the button, inset 1
    rule = f'clearlooks_gummy_draw_slider (bg[NORMAL]) on the {geo["scale_len"] - 2} x {sh_h} slider'
    ramp = {r: T.pixman_vertical_ramp_row(gummy(bg, False), 1, sh_h - 2, r) for r in range(1, sh_h - 1)}
    kstep = T.pixman_step_row(1, sh_h - 2)
    t.ramp('cl_scale_thumb_upper', [ramp[r] for r in range(1, kstep)], 0, kstep - 2, rule + f': rows 1..{kstep - 1}')
    t.ramp('cl_scale_thumb_lower', [ramp[r] for r in range(kstep, sh_h - 1)], 0, sh_h - 2 - kstep,
           rule + f': rows {kstep}..{sh_h - 2}')
    t.add('cl_scale_thumb_border', cb(mix(SH[7], bg, 0.2)), rule + ': the border, mix (shade[7], fill, 0.2)')
    t.add('cl_scale_thumb_grip', cb(SH[7]), rule + ': the grip bars, shade[7]')
    over = lambda r: T.cairo_solid_over(sh(bg, 1.3), 0.4, ramp[r])
    t.add('cl_scale_thumb_highlight_row', over(1), rule + ': the top-left highlight over its row 1')
    t.ramp('cl_scale_thumb_highlight_upper', [over(r) for r in range(3, kstep)], 0, kstep - 4,
           rule + f': the top-left highlight down its column 1, rows 3..{kstep - 1}')
    t.ramp('cl_scale_thumb_highlight_lower', [over(r) for r in range(kstep, sh_h - 3)], 0, sh_h - 4 - kstep,
           rule + f': the top-left highlight down its column 1, rows {kstep}..{sh_h - 4}')

    # THE RESTORED LAPTOP'S FRAME (gnome-themes 2.30.2's Clearlooks metacity theme, the frame style set `normal`:
    # `focused` = round_bevel, `normal` = round_bevel_unfocused, on the `normal` geometry, left / right / bottom 4)
    # with the title bar THE SIZING FRAME'S TOP BAND AND THE CAPTION LANE TOGETHER, top_height = kWindowFramePx +
    # caption_height_px, title_height = top_height - 7 (title_border 4 + 3): each <line> and <rectangle> a role named
    # by its shade (cl_frame_<state>_<sel|bg><factor x 1000>), each <gradient> a ramp; THE CORNER ART's cells are
    # drawn as arcs (clearlooks_paint.h's frame painter): its dark cells' tone and its highlight's, the 0.73 / 0.68
    # in-between cells being the antialiasing the arc's edge does itself.
    TT = geo['frame_w'] + geo['caption_h']
    fenv = dict(top_height=TT, title_height=TT - 7, width=64, height=64)
    def frame_role(state, c):
        assert c[0] == 'shade' and c[1][0] == 'gtk' and c[1][1] == 'bg', c
        return f'cl_frame_{state}_{"sel" if c[1][2] == "SELECTED" else "bg"}{int(round(c[2] * 1000)):04d}'
    for state, ops_name in (('focused', 'round_bevel'), ('unfocused', 'round_bevel_unfocused')):
        n = 0
        for op in draw_ops_flat(m, ops_name):
            rule = f'metacity draw_ops {ops_name}, top_height {TT}'
            if op['op'] in ('line', 'rectangle'):
                if op['op'] == 'rectangle' and op.get('filled') == 'true':
                    assert op['color'] == ('gtk', 'bg', 'NORMAL'), op          # window_bg: the ground role
                    continue
                if op['color'][0] == 'shade' and round(op['color'][2], 2) in (0.73, 0.68):
                    continue                                                   # the corner art's in-between cells
                if op['op'] == 'line' and op['y1'] == op['y2'] == 'title_height + 5':
                    assert (op['x1'], op['x2']) == ('2', 'width - 3'), op       # under the lower gradient, x 2 .. w - 3
                    continue
                role = frame_role(state, op['color'])
                if role not in t.tones: t.add(role, mc_byte(op['color'], gtk), f'{rule}: <{op["op"]}> {op["color"]}')
            elif op['op'] == 'gradient':
                gh = mc_eval(op['height'], fenv)
                rows = T.metacity_vertical_gradient_rows(*(metacity_color(c, gtk) for c in op['colors']), gh)
                t.ramp(f'cl_frame_{state}_ramp{n}', rows, 0, gh - 1, f'{rule}: <gradient> {n}, {gh} rows')
                n += 1
            else:
                raise SystemExit(f'build: metacity op <{op["op"]}> in {ops_name} is not one the frame painter draws')
    return t


# The caption buttons' four draw_ops (metacity's button_bg family; the frame style focused_maximized's and
# normal_maximized's buttons: the prelight is not drawn, the product's caption having no hover face) and their
# role prefixes.
CAPTION_BUTTON_STATES = (('focused', 'button_bg'), ('pressed', 'button_bg_pressed'),
                         ('unfocused', 'button_bg_unfocused'), ('unfocused_pressed', 'button_bg_unfocused_pressed'))
# THE LINES THE CAPTION BUTTON PAINTER DRAWS (architect 2026-10-07, his glass verdict on the pixelated boxes:
# clearlooks_paint.cpp's paint_cl_caption_button draws each button_bg as concentric antialiased rounded rings —
# the halo, the border, the inner bevel — and its fills): per state, the shade factors x 1000 of the lines whose
# tones it paints, the halo's top and bottom, the border, the inner ring's sides; the staircase's corner cells (the
# art's hand antialiasing between those lines) are the arcs' own antialiasing and are not roles. The gradients are
# every one of them.
CAPTION_BUTTON_DRAWN = {
    'focused': {'s0980', 's1060', 's0600', 's1180', 's1100', 's1000', 's0920'},
    'unfocused': {'s0910', 's0960', 's0600', 's1200', 's1100', 's1050', 's0970'},
    'pressed': {'s1000', 's0550', 's0900', 's0850'},
    'unfocused_pressed': {'s1050', 's0550', 's0800', 's0750', 's0850'},
}
# The tool button's drawn states: (role prefix, the GTK state whose bg fills it, active (shadow IN), insensitive).
TOOL_BUTTON_STATES = (('hot', 'PRELIGHT', False, False), ('pressed', 'ACTIVE', True, False),
                      ('hot_checked', 'PRELIGHT', True, False), ('dead_checked', 'INSENSITIVE', True, True))
# The push button's drawn states (the same tuple): the product's dialog buttons have no hover face (the toolbars' HOT
# case is the one hover face, clearlooks_paint.h's head), so no prelit state is recorded.
PUSH_BUTTON_STATES = (('normal', 'NORMAL', False, False), ('pressed', 'ACTIVE', True, False),
                      ('disabled', 'INSENSITIVE', False, True))


def caption_button_line_role(state, op):
    """A caption button line's role: the state and its shade factor x 1000 (every button_bg line is a shade)."""
    assert op['color'][0] == 'shade', op
    return f'cl_cbtn_{state}_s{int(round(op["color"][2] * 1000)):04d}'


# ------------------------------------------------------------------ the app
# THE CHOSEN ENTRY (architect 2026-10-03, the colour loop's mock sets BA..BX): key and display name `warptempo` (all
# lowercase, one word, his spelling), the app's default theme 2026-10-03..04. CHOSEN, NOT IMPORTED — no desktop of
# the era recorded it, so it carries no source file: its record is the ruling. Its chrome is set BA03, Windows 95
# Standard darkened in proportion to a ground of relative luminance 0.010 under white text (the quartet and the field
# ground scaled with the face, DkShadow black), and its selection the grey of set BM02 under white; each byte below is
# the ruled one, in the catalog's role order (its emboss's light copy the recorded Hilight, roles.light_roles' rule, as
# for every entry). The info pair is not ruled, so it is absent and its bundled theme file names no card (the
# built-in's card applies, gen_theme_files.py); the flags' rule is the family's.
CHOSEN_ROLES = {'ground': '#191919', 'label': '#FFFFFF', 'bevel_hilight': '#212121', 'bevel_light': '#1D1D1D',
                'bevel_shadow': '#111111', 'bevel_dkshadow': '#000000', 'selected_fill': '#666666',
                'selected_text': '#FFFFFF', 'field_ground': '#212121', 'field_text': '#FFFFFF'}


def chosen_entry():
    e = {'key': 'warptempo', 'name': 'warptempo', 'family': 'warptempo',
         'provenance': {'sources': [{
             'project': 'chosen, not imported: the architect\'s ruling (2026-10-03)',
             'record': 'the colour loop\'s mock sets: BA03 for the chrome (Windows 95 Standard darkened in proportion '
                       'to a ground of relative luminance 0.010, white text), BM02 for the selection grey; the bytes '
                       'as ruled'}]},
         'raw': dict(CHOSEN_ROLES), 'roles': {r: CHOSEN_ROLES[r] for r in ROLES if r in CHOSEN_ROLES},
         'flag_rule': {'id': FLAG_RULE['warptempo']}}
    e['display_tier'] = display_tier(e['roles'])
    e['notes'] = ['chosen by the architect on the colour loop, not imported from a desktop of the era; the app\'s '
                  'default theme at its light level (2026-10-03)']
    e['corroborated'] = 0
    return e


# THE PRESET ENTRIES (architect 2026-10-04): each preset the colour picker saved (tools/palette/picker/, README "THE
# PRESETS"; the repository's copy of the tablet's presets.json) becomes `warptempo-preset-<n>`, display name
# "Warptempo Preset <n>", CHOSEN, NOT IMPORTED, its record the preset. Its chrome is exactly what the picker painted:
# the preset's chrome ground through the picker's chrome rule (colour.windows95_chrome: the relief lines, the field
# ground and the emboss's light copy from the ground, DkShadow black) and THE LABEL, the chrome's text colour: the
# preset's `label` when it records one (the picker's Label element, architect 2026-10-04: text is a pickable element,
# not a black / white switch and not automatic contrast -- the product imports each theme's recorded text colour and
# has no contrast rule), else white, the picker's starting colour and the label every preset saved before the Label
# round was painted under. THE FIELD TEXT IS THE LABEL TOO: the chrome rule makes the field ground the Hilight, a
# ground-family colour, so its text is the label's case (the picker's theme states field_text "@label"). THE SELECTED
# PAIR the preset's `selected_fill` and `selected_text` when it records them (the picker's Selection and Selected Text,
# the open-flag round, architect 2026-10-04), else #666666 under white, the picker's starting colours and the pair
# every preset saved before that round was painted under. The preset's other elements (the canvas, the ink, the
# flags, the playhead, the invalid flags -- the invalid-flags round, architect 2026-10-04) are not catalog roles: the
# preset's bundled theme file names them as the program's roles (tools/theme_catalog/gen_theme_files.py, which owns
# that mapping). The entries follow presets.json: a
# new copy adds its new presets on the next run, and a preset's number is never reused (the picker only appends). A
# colour key the preset names that is neither the chrome, a theme role it picks (PRESET_THEME_KEYS) nor a program key
# is a hard fail: a newly pickable chrome role changes what a preset's theme is, which is a ruling, not a silent drop.
PRESETS = os.path.join(REPO, 'tools', 'palette', 'picker', 'presets', 'presets.json')
PRESET_PREFIX = 'warptempo-preset-'
# the theme roles at a preset that records none of the picker's theme elements (each at its starting colour)
PRESET_FIXED = {'label': '#FFFFFF', 'selected_fill': '#666666', 'selected_text': '#FFFFFF', 'field_text': '#FFFFFF'}
# the picker's theme elements -> the roles each gives its colour (the label also the field text)
PRESET_THEME_KEYS = {'label': ('label', 'field_text'), 'selected_fill': ('selected_fill',),
                     'selected_text': ('selected_text',)}
# (the Outline's and the flag kinds' keys since 2026-10-05, and the four a preset saved before then names, which
# gen_theme_files.py's OLD_PRESET_KEYS reads as the picker does; `playhead_head` the picker's own element, colouring no
# product role since 2026-10-05, accepted here and dropped by gen_theme_files.py's IGNORED_PRESET_KEYS)
PRESET_PROGRAM_KEYS = ('canvas', 'ink', 'waveform_outline', 'warp_flag', 'warp_flag_selected', 'phase_reset_flag',
                       'phase_reset_flag_selected', 'added_flag', 'added_flag_selected', 'removed_flag', 'removed_flag_selected', 'playhead_head',
                       'playhead_stem', 'unselected_flag', 'selected_flag', 'unselected_invalid_flag',
                       'selected_invalid_flag')


def preset_fixed(picked):
    """PRESET_FIXED with each role a picked theme element ({key: '#RRGGBB'}, PRESET_THEME_KEYS) gives its colour."""
    return PRESET_FIXED | {r: picked[k] for k, rs in PRESET_THEME_KEYS.items() if k in picked for r in rs}


def preset_roles(ground, picked={}):
    """A preset's chrome ground and its picked theme elements ('#RRGGBB'; {key: hex}) -> its catalog roles: the chrome
    rule's lines, the picked roles, and the picker's starting colours for the rest."""
    ch = {r: hx(c) for r, c in CL.windows95_chrome(unhex(ground)).items()}
    # roles.light_roles takes the emboss's light copy as the recorded Hilight; the chrome rule's is the same
    assert ch['emboss_hilight'] == ch['bevel_hilight'], ground
    fixed = preset_fixed(picked)
    return {r: ch[r] if r in ch else fixed[r] for r in ROLES if r in ch or r in fixed}


def preset_entries():
    ps = json.load(open(PRESETS))['presets']
    nums = [p['number'] for p in ps]
    if any(not isinstance(n, int) or isinstance(n, bool) or n < 1 for n in nums) or len(nums) != len(set(nums)):
        raise SystemExit(f'build: {PRESETS}: the preset numbers {nums} are not distinct whole numbers from 1')
    out = []
    for p in sorted(ps, key=lambda p: p['number']):
        n, cols = p['number'], p['colours']
        bad = sorted(set(cols) - {'chrome'} - set(PRESET_THEME_KEYS) - set(PRESET_PROGRAM_KEYS))
        if bad: raise SystemExit(f'build: Preset {n} names {bad}, neither the chrome, a theme element nor a program key')
        ground = cols['chrome']
        picked = {k: cols[k] for k in PRESET_THEME_KEYS if k in cols}
        given = {r for k in picked for r in PRESET_THEME_KEYS[k]}
        roles = preset_roles(ground, picked)
        raw = {'chrome': ground} | preset_fixed(picked)
        record = (f'the chrome ground {ground}; the relief quartet, the field ground and the emboss\'s light '
                  f'copy the picker\'s chrome rule over it (tools/palette/colour.py windows95_chrome: Windows '
                  f'95\'s proportions, Hilight / 3DLight / Shadow the ground x 255 / 223 / 128 over 192 per '
                  f'channel, half to even, capped; DkShadow black; the field ground and the emboss the Hilight)')
        if 'label' in picked: record += f'; the label {picked["label"]}, the field text the label (the picker\'s Label element)'
        if 'selected_fill' in picked: record += f'; the selected fill {picked["selected_fill"]} (the picker\'s Selection)'
        if 'selected_text' in picked:
            record += f'; the selected text {picked["selected_text"]} (the picker\'s Selected Text)'
        e = {'key': f'{PRESET_PREFIX}{n}', 'name': f'Warptempo Preset {n}', 'family': 'warptempo',
             'provenance': {'sources': [
                 {'project': f'chosen, not imported: the architect\'s Preset {n} on the colour picker (architect 2026-10-04)',
                  'file': os.path.relpath(PRESETS, REPO), 'preset': n, 'saved': p['saved'],
                  'keys': ['chrome'] + list(picked), 'record': record},
                 {'project': 'the colour picker\'s theme, the roles it shows fixed (2026-10-04)',
                  'file': 'tools/palette/themes/picker.json',
                  'keys': [r for r in PRESET_FIXED if r not in given]}]},
             'raw': raw, 'roles': roles, 'flag_rule': {'id': FLAG_RULE['warptempo']}}
        e['display_tier'] = display_tier(e['roles'])
        e['notes'] = [f'chosen by the architect on the colour picker, not imported from a desktop of the era: his '
                      f'Preset {n}\'s chrome, exactly as the picker painted it (2026-10-04); its canvas, ink and other '
                      f'elements are the program\'s roles in its bundled theme file '
                      f'(tools/theme_catalog/gen_theme_files.py)']
        e['corroborated'] = 0
        out.append(e)
    return out


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
    # Windows 2000 Standard is the hive's bytes (the chrome's theme and the built-in: the chrome roles of the ReactOS
    # captures); Windows 98 Standard is Windows 95 Standard's roles but for the 3DLight, under a gradient caption
    w2k = by['windows-2000-standard']['roles']
    assert [w2k[x] for x in ('ground', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow', 'selected_fill',
                             'info_ground', 'title_active', 'title_inactive')] == \
        ['#D4D0C8', '#FFFFFF', '#D4D0C8', '#808080', '#404040', '#0A246A', '#FFFFE1', '#0A246A', '#808080']
    w98 = by['windows-98-standard']
    assert {r: v for r, v in w98['roles'].items() if w95[r] != v} == {'bevel_light': '#C0C0C0'}
    assert (w98['raw']['GradientActiveTitle'], w98['raw']['GradientInactiveTitle']) == ('#1084D0', '#B5B5B5')
    assert 'GradientActiveTitle' not in by['windows-95-standard']['raw']
    for gone in ('windows-classic', 'windows-standard'): assert gone not in by, gone
    for d in DUPLICATES: assert d not in by, d
    assert sum(1 for e in entries if e['family'] == 'kde3') == KDE3_ENTRIES
    assert len(KDE_NOT_35['tde_kcs']) == 21
    # the display tiers: Windows Storm, Teal and Red, White, and Blue are the only `vga` entries, none `windows-20`
    # (Windows 98 Standard misses `vga` only by its tooltip ground #FFFFE1, Windows 95 Standard by that and its 3DLight
    # #DFDFDF)
    assert sorted(e['key'] for e in entries if e['display_tier'] == 'vga') == \
        ['windows-red-white-and-blue', 'windows-storm', 'windows-teal']
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
    # its program roles as he picked them on the CL10 sheets (2026-10-07 ~05:30; the outline ~06:10; the ink ~09:00)
    assert cl['program_roles'] == {
        'waveform_canvas': '#000000', 'waveform_ink': '#D2E3F7', 'waveform_outline': '#86ABD9',
        'warp_flag': '#86ABD9', 'warp_flag_selected': '#D2E3F7', 'phase_reset_flag': '#86ABD9',
        'phase_reset_flag_selected': '#D2E3F7', 'added_flag': '#83A67F', 'added_flag_selected': '#B3CEB0',
        'removed_flag': '#C1665A', 'removed_flag_selected': '#E2988E', 'flag_label': '#000000',
        'warp_label_selected': '#000000', 'phase_reset_label_selected': '#000000', 'added_label_selected': '#000000',
        'removed_label_selected': '#000000', 'flag_outline': '#000000',
        'playhead_stem': '#FFFFFF',
        'scanner': '#FFFFFF'}, cl['program_roles']
    assert by['warptempo']['roles'] == CHOSEN_ROLES and by['warptempo']['display_tier'] == 'high-colour'
    # the preset road at the neutral ground #191919 is the chosen `warptempo` exactly (the picker's default chrome)
    assert preset_roles('#191919') == CHOSEN_ROLES
    # a preset recording a label carries it into the label and the field text, and nothing else moves (a synthetic
    # preset: the neutral ground under a black label, the light variant a theme strip's #000000 makes)
    assert preset_roles('#191919', {'label': '#000000'}) == CHOSEN_ROLES | {'label': '#000000', 'field_text': '#000000'}
    # a preset recording the selected pair (the picker's Selection and Selected Text, architect 2026-10-04) carries it
    # into its two roles alone (a synthetic preset: a Windows-blue selection under yellow text)
    assert preset_roles('#191919', {'selected_fill': '#000080', 'selected_text': '#FFFF00'}) == \
        CHOSEN_ROLES | {'selected_fill': '#000080', 'selected_text': '#FFFF00'}
    for e in entries:
        if e['key'].startswith(PRESET_PREFIX):
            picked = {k: e['raw'][k] for k in PRESET_THEME_KEYS}
            assert e['family'] == 'warptempo' and e['roles'] == preset_roles(e['raw']['chrome'], picked), e['key']


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
                'source records them (the renderer takes a theme byte as a Display-P3 byte as-is). The one family '
                'that imports nothing is the program\'s own, "warptempo": the architect\'s chosen `warptempo` and his presets saved on the colour picker '
                '(`warptempo-preset-<n>`, architect 2026-10-04: the preset\'s chrome ground through the picker\'s chrome '
                'rule, the roles the picker shows fixed), chosen, not imported (their provenance is his ruling or his '
                'preset).',
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


def presets_only():
    """The --presets-only road (the head): the committed catalog's imported entries and not-imported record carried,
    the program's own family and the header written anew."""
    old = json.load(open(OUT))
    carried = [e for e in old['entries'] if e['family'] != 'warptempo']
    by = {e['key']: e for e in old['entries']}
    chosen = chosen_entry()
    if {x: v for x, v in chosen.items() if x != 'corroborated'} != by[chosen['key']]:
        raise SystemExit(f'build: {chosen["key"]} recomputed differs from the committed catalog; run the full build')
    entries = carried + [chosen] + preset_entries()
    entries.sort(key=lambda e: FAMILIES.index(e['family']))
    checks(entries)
    write(document(entries, old['not_imported']))
    print(f'carried {len(carried)} entries from the committed catalog; presets: '
          + ', '.join(e['key'] for e in entries if e['key'].startswith(PRESET_PREFIX)))


def main():
    if sys.argv[1:] == ['--presets-only']: return presets_only()
    if sys.argv[1:]: raise SystemExit('usage: python3 tools/theme_catalog/build.py [--presets-only]')
    win, ros_only = windows_entries()
    kde, kde_later = kde_entries()
    entries = win + kde
    cde, mono = cde_entries(); entries += cde
    entries += gnome2_entries()
    entries.append(chosen_entry())
    entries += preset_entries()
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
