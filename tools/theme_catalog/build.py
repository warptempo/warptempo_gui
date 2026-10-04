#!/usr/bin/env python3
# tools/theme_catalog/build.py — the fetched sources (fetch.py) -> docs/themes/catalog.json: every entry's recorded
# bytes with their provenance, the values its own toolkit computed at import (toolkit_rules.py, the rule named), and
# its catalog roles (roles.py), the family rule its flags take (flag_rule) and its display tier (display_tier). THE APP CARRIES IMPORTED THEMES ONLY,
# NO DERIVATION (architect 2026-10-03): nothing here invents a colour; a role a source has no word for stays absent.
# THE ONE EXCEPTION IS THE PROGRAM'S OWN FAMILY, `warptempo`, and it derives nothing either: `warptempo-2026-10-03` is
# the app's look recorded off render.h's constants (app_entry), and `warptempo` is THE ARCHITECT'S PICK, CHOSEN, NOT
# IMPORTED (chosen_entry: his ruling of 2026-10-03 on the colour loop's mock sets, its bytes his, recorded as ruled),
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
# Trinity mirror is outside its egress): it re-derives only what needs no fetched file -- the preset entries from
# presets.json and the header -- and carries every other entry and the not-imported record from the committed
# catalog.json byte for byte, after recomputing the two entries that need no source (app_entry, chosen_entry) and
# asserting they equal the carried ones; the checks run on the whole. The full run writes the same bytes where the
# sources are at hand (both roads build the document through one function, document()).
import json, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sources import SOURCES, APP, REPO, local_path, provenance
sys.path.insert(0, os.path.join(REPO, 'tools', 'palette'))
import colour as CL      # the picker's chrome rule (windows95_chrome), the one the picker paints with
from parse_windows import parse_hivedef, parse_theme
from parse_kde import parse_kcsrc
from parse_cde import parse_dp
import toolkit_rules as T
from roles import ROLES, MAPPING, map_roles

OUT = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
FAMILIES = ('windows', 'windows-plus', 'kde3', 'cde', 'warptempo')
KEY_PREFIX = {'windows': 'windows', 'windows-plus': 'plus', 'kde3': 'kde3', 'cde': 'cde', 'warptempo': 'warptempo'}
# THE FLAGS' RULE per family (architect 2026-10-03, late: a flag's one-line bevel is its theme family's own rule on the
# flag's face, toolkit_rules.flag_bevel): Windows' Appearance dialog for the Windows families and the app's own look
# ("take Windows' rule"), KDE 3's at the scheme's contrast, Motif's for CDE.
FLAG_RULE = {'windows': 'windows-dialog', 'windows-plus': 'windows-dialog', 'warptempo': 'windows-dialog',
             'kde3': 'kde3', 'cde': 'motif'}

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
    if words.startswith(pre + '-'): words = words[len(pre) + 1:]     # 'Windows Classic' -> windows-classic
    key = f'{pre}-{words}'
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

    # ReactOS's 23 schemes. Its "ReactOS Standard" and "ReactOS Classic" are Windows Classic and Windows Standard
    # under ReactOS names (byte-equal on every role key below); they are folded into those two entries, not doubled.
    # A scheme no second source corroborates is NOT IMPORTED (REACTOS_ONLY names each and why).
    folded = {'ReactOS Standard': 'Windows Classic', 'ReactOS Classic': 'Windows Standard'}
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
    # zkedem's standard.theme ("Windows Standard") holds Windows Classic's bytes (D4D0C8) and its classic.theme
    # ("Windows Classic") Windows Standard's (C0C0C0): its labels are swapped against XP's Appearance dialog, as
    # classicthemes8's "Windows XP Classic" (D4D0C8) and Windows 98's Default.theme (C0C0C0) show. The entries go by
    # the bytes.
    swap = {'Windows Classic': 'Windows Standard', 'Windows Standard': 'Windows Classic'}
    for name in ('Desert', 'Spruce', 'Windows Standard', 'Windows Classic'):
        zname = swap.get(name, name)
        f, dn, cols = zk[zname]
        notes = []
        if name in swap: notes.append(f'xp_classic_zkedem {f} carries DisplayName "{dn}" over these bytes (its two '
                                      f'labels swapped against XP\'s dialog); the entry is named by its bytes')
        cands = [c for c in by_name.get(name, []) if c[0] != 'xp_classic_zkedem']
        if name == 'Windows Classic': cands += [c for c in seconds if c[2] == 'Windows XP Classic']
        provs, n_ok = corroborate(name, cols, cands, notes)
        rname = {v: k for k, v in folded.items()}.get(name)
        if rname:
            d = diff_keys(cols, ros[rname])
            assert not set(d) & role_keys, (name, d)
            n_ok += 1; provs.append(provenance('reactos', hv) | {'scheme': rname})
            notes.append(f'ReactOS records it as "{rname}"' + ('' if not d else ', differing on ' + ', '.join(
                f'{k} {hx(ros[rname][k])} (here {hx(cols[k])})' for k in d)))
        if name == 'Windows Standard':
            f98, c98 = w98['Windows Default']
            d = diff_keys(cols, c98)
            assert not set(d) & role_keys, d
            n_ok += 1; provs.append(provenance('win98_themes', f98))
            notes.append('Windows 98\'s Windows Default.theme records the same bytes' + ('' if not d else ', but for ' + ', '.join(
                f'{k} {hx(c98[k])} (here {hx(cols[k])})' for k in d)))
        out.append(entry('windows', name, name, [provenance('xp_classic_zkedem', f)] + provs, cols, notes=notes))
        out[-1]['corroborated'] = n_ok
    by_key = {e['key']: e for e in out}
    for name, (twin, _) in REACTOS_ONLY.items():     # "role-identical": the roles the twin entry maps, byte for byte
        if twin and map_roles('windows', {k: hx(v) for k, v in ros_only[name].items()}) != by_key[twin]['roles']:
            raise SystemExit(f'build: ReactOS {name} is not role-identical to {twin}')

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
        'Windows Default.theme and the Windows 2000 / XP "Windows Standard" record C0C0C0 (the face), and the early '
        'Windows 95 beta captures draw no 3DLight line. The entry is the retail Windows 95 picture.']))
    out[-1]['corroborated'] = 0

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


# ------------------------------------------------------------------ the app
def render_h_constants():
    """src/gui/render.h at APP's commit -> {constant: rgb}: a hex(0x..) literal, a uint32 0x.. literal, an alias of
    another constant, hex() of one, or a derived word whose static_assert states its value (the compiler checks
    those, so they are the bytes)."""
    txt = subprocess.run(['git', '-C', REPO, 'show', f"{APP['commit']}:{APP['file']}"], capture_output=True, text=True,
                         check=True).stdout
    defs = dict((n, e.strip()) for n, e in re.findall(r'inline constexpr (?:GuiColor|uint32_t)\s+(k\w+)\s*=\s*([^;]+);', txt))
    asserted = {n: int(v, 16) for n, v in re.findall(r'static_assert\((k\w+)\s*==\s*0x([0-9A-Fa-f]{6})\)', txt)}
    memo = {}

    def val(n):
        if n in memo: return memo[n]
        e = re.sub(r'\s+', ' ', defs[n])
        if m := re.fullmatch(r'hex\(0x([0-9A-Fa-f]{6})\)', e): v = int(m.group(1), 16)
        elif m := re.fullmatch(r'0x([0-9A-Fa-f]{6})', e): v = int(m.group(1), 16)
        elif m := re.fullmatch(r'hex\((k\w+)\)', e): v = val(m.group(1))
        elif re.fullmatch(r'k\w+', e) and e in defs: v = val(e)
        elif n in asserted: v = asserted[n]
        else: return None
        memo[n] = v; return v
    out = {}
    for n in defs:
        v = val(n)
        if v is not None: out[n] = ((v >> 16) & 255, (v >> 8) & 255, v & 255)
    return out


APP_RAW = ('kRedesignContentGround', 'kRedesignLabel', 'kRedesignAccent', 'kRedesignHighlightLabel',
           'kRedesignAccentInactive', 'kReliefHilight', 'kRelief3DLight', 'kReliefShadow', 'kReliefDkShadow',
           'kInfoGround', 'kInfoText', 'kModalFieldGround', 'kRulerLabel', 'kRulerTick', 'kPlayheadHead',
           'kPlayheadStem', 'kMarkerFlagBorder', 'kMarkerFlagFill', 'kMarkerFlagEdge', 'kMarkerFlagFillSel',
           'kMarkerFlagEdgeSel', 'kMarkerFlagLabel', 'kMarkerFlagFillRed', 'kWaveformInk', 'kWaveformCanvas',
           'kTrimArrowGlyph')


# THE CHOSEN ENTRY (architect 2026-10-03, the colour loop's mock sets BA..BX): key and display name `warptempo` (all
# lowercase, one word, his spelling), the app's default theme (device_config.h). CHOSEN, NOT IMPORTED — no desktop of
# the era recorded it, so it carries no source file: its record is the ruling. Its chrome is set BA03, Windows 95
# Standard darkened in proportion to a ground of relative luminance 0.010 under white text (the quartet and the field
# ground scaled with the face, DkShadow black), and its selection the grey of set BM02 under white; each byte below is
# the ruled one, in levels.py's role order (its LIGHT row; its emboss's light copy is the recorded Hilight there, as
# for every entry). Its DARK row is whatever levels.py's dark rule makes of these bytes, untuned (architect: "if the
# user sets this theme and puts dark, it's going to look bad -- that's fine"). The info pair is not ruled (the app
# carries none: render.h's THE INFO FACE), so it is absent; the flags' rule is the family's.
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
# every preset saved before that round was painted under. The theme's DARK row is levels.py's dark rule over these
# bytes, whatever the preset picked (the selected fill darkened in proportion, the label and the selected text the
# level's white). The preset's other elements (the canvas, the ink, the flags, the playhead, the invalid flags -- the
# invalid-flags round, architect 2026-10-04) are program keys, not theme roles: tools/theme_catalog/preset_keys.py
# prints their device-config lines. The entries follow presets.json: a
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
PRESET_PROGRAM_KEYS = ('canvas', 'ink', 'unselected_flag', 'selected_flag', 'playhead_head', 'playhead_stem',
                       'unselected_invalid_flag', 'selected_invalid_flag')


def preset_fixed(picked):
    """PRESET_FIXED with each role a picked theme element ({key: '#RRGGBB'}, PRESET_THEME_KEYS) gives its colour."""
    return PRESET_FIXED | {r: picked[k] for k, rs in PRESET_THEME_KEYS.items() if k in picked for r in rs}


def preset_roles(ground, picked={}):
    """A preset's chrome ground and its picked theme elements ('#RRGGBB'; {key: hex}) -> its catalog roles: the chrome
    rule's lines, the picked roles, and the picker's starting colours for the rest."""
    ch = {r: hx(c) for r, c in CL.windows95_chrome(unhex(ground)).items()}
    # levels.py's LIGHT row takes the emboss's light copy as the recorded Hilight; the chrome rule's is the same
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
                      f'Preset {n}\'s chrome at its light level, exactly as the picker painted it (2026-10-04); its '
                      f'canvas, ink and other elements are program keys (tools/theme_catalog/preset_keys.py)']
        e['corroborated'] = 0
        out.append(e)
    return out


def app_entry():
    k = render_h_constants()
    k['kTrimArrowGlyph'] = k['kRedesignLabel']   # highlight_text_ink(the ground): the label white on #303030
    raw = {n: k[n] for n in APP_RAW}
    prov = {'project': APP['project'], 'file': APP['file'], 'commit': APP['commit'],
            'record': 'the constants src/gui/render.h compiles to (its hex() literals and the static_asserts of its '
                      'derived words); kTrimArrowGlyph is highlight_text_ink(kRedesignContentGround)'}
    e = entry('warptempo', '2026-10-03', 'Warptempo 2026-10-03', prov, raw, notes=[
        'the look the app paints on 2026-10-03, its relief derived from the ground by the app\'s ratio rule at that '
        'commit; recorded here so it stays selectable as bytes'])
    e['corroborated'] = 0
    return e, k


# ------------------------------------------------------------------ the checks and the write
def checks(entries, k):
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
    for d in DUPLICATES: assert d not in by, d
    assert sum(1 for e in entries if e['family'] == 'kde3') == KDE3_ENTRIES
    assert len(KDE_NOT_35['tde_kcs']) == 21
    # the display tiers: Windows Storm, Teal and Red, White, and Blue are the only `vga` entries, none `windows-20`
    # (Windows Standard misses `vga` only by its tooltip ground #FFFFE1, Windows 95 Standard by that and its 3DLight #DFDFDF)
    assert sorted(e['key'] for e in entries if e['display_tier'] == 'vga') == \
        ['windows-red-white-and-blue', 'windows-storm', 'windows-teal']
    assert not [e['key'] for e in entries if e['display_tier'] == 'windows-20']
    assert display_tier(by['windows-standard']['roles']) == 'high-colour' and \
        display_tier({r: v for r, v in by['windows-standard']['roles'].items() if r != 'info_ground'}) == 'vga'
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
    app = by['warptempo-2026-10-03']
    for n, v in app['raw'].items(): assert v == hx(k[n]), n
    assert [app['roles'][x] for x in ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow',
                                      'selected_fill', 'selected_text', 'info_ground', 'info_text', 'field_ground')] == \
        ['#303030', '#FCFCFC', '#5E5E5E', '#434343', '#1E1E1E', '#0A0A0A', '#96BFDA', '#000000', '#FFFFE1', '#000000', '#141618']


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
                'colours and its toolkit computed the rest at run time (KDE 3, CDE / Motif), that toolkit\'s own rule ran '
                'once at import and its rule and sources are named in the entry\'s provenance. "raw" holds every value '
                'the source records under its own key names; "roles" the catalog roles (tools/theme_catalog/roles.py); '
                'a role a source has no word for is absent and the app\'s own value applies. Bytes are #RRGGBB as the '
                'source records them (the renderer takes a theme byte as a Display-P3 byte as-is). The one family '
                'that imports nothing is the program\'s own, "warptempo": the app\'s look of 2026-10-03 recorded off '
                'render.h, the architect\'s chosen default `warptempo`, and his presets saved on the colour picker '
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
    """The --presets-only road (the head): the committed catalog's other entries and not-imported record carried, the
    preset entries and the header written anew."""
    old = json.load(open(OUT))
    carried = [e for e in old['entries'] if not e['key'].startswith(PRESET_PREFIX)]
    by = {e['key']: e for e in carried}
    app, k = app_entry()
    for e in (app, chosen_entry()):
        if {x: v for x, v in e.items() if x != 'corroborated'} != by[e['key']]:
            raise SystemExit(f'build: {e["key"]} recomputed differs from the committed catalog; run the full build')
    entries = carried + preset_entries()
    entries.sort(key=lambda e: FAMILIES.index(e['family']))
    checks(entries, k)
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
    app, k = app_entry(); entries.append(app)
    entries.append(chosen_entry())
    entries += preset_entries()
    entries, dups = drop_duplicates(entries)
    entries.sort(key=lambda e: FAMILIES.index(e['family']))
    checks(entries, k)
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
                srcs.add(q.get('repository', q.get('project')) + '@' + str(q.get('commit', '')))
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
