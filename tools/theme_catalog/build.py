#!/usr/bin/env python3
# tools/theme_catalog/build.py — the fetched sources (fetch.py) -> docs/themes/catalog.json: every entry's recorded
# bytes with their provenance, the values its own toolkit computed at import (toolkit_rules.py, the rule named), and
# its catalog roles (roles.py). THE APP CARRIES IMPORTED THEMES ONLY, NO DERIVATION (architect 2026-10-03): nothing
# here invents a colour; a role a source has no word for stays absent. The checks of the brief run before the write;
# the last lines report each family: entries, corroborated, sources.
#
#   python3 tools/theme_catalog/build.py
import json, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from sources import SOURCES, APP, REPO, local_path, provenance
from parse_windows import parse_hivedef, parse_theme
from parse_kde import parse_kcsrc
from parse_cde import parse_dp
import toolkit_rules as T
from roles import ROLES, MAPPING, map_roles

OUT = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
FAMILIES = ('windows', 'windows-plus', 'reactos', 'kde3', 'cde', 'warptempo')
KEY_PREFIX = {'windows': 'windows', 'windows-plus': 'plus', 'reactos': 'reactos', 'kde3': 'kde3', 'cde': 'cde',
              'warptempo': 'warptempo'}


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


def entry(family, key_words, name, prov, raw, computed=None, notes=None, imitates=None, rule=None):
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
    e['notes'] = notes or []
    return e


# ------------------------------------------------------------------ Windows (windows, reactos), windows-plus
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
    folded = {'ReactOS Standard': 'Windows Classic', 'ReactOS Classic': 'Windows Standard'}
    for name, cols in ros.items():
        if name in folded: continue
        notes = []
        cands = by_name.get(name, [])
        provs, n_ok = corroborate(name, cols, cands, notes)
        fam = 'windows' if n_ok else 'reactos'
        if fam == 'reactos':
            notes.append('no second source records this scheme' + (
                ' (Windows 2000 / XP ship High Contrast schemes of these names; no independent record of their bytes was '
                'found)' if name.startswith('High Contrast') else ' (a ReactOS scheme; no Windows release ships it)'))
        out.append(entry(fam, name, name, [provenance('reactos', hv) | {'scheme': name}] + provs, cols, notes=notes))
        out[-1]['corroborated'] = n_ok

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
    return out


# ------------------------------------------------------------------ KDE 3
KDE_IMITATES = {'Redmond 95': 'Windows 95', 'Redmond 2000': 'Windows 2000', 'Redmond XP': 'Windows XP', 'CDE': 'CDE',
                'Digital CDE': 'CDE (Digital UNIX)', 'Solaris': 'CDE (Solaris)', 'BeOS': 'BeOS', 'Next': 'NeXTSTEP',
                'Platinum': 'Mac OS 8 (Platinum)'}
KDE_RULE_SOURCES = [provenance('tde_rules', 'tdecore/tdeapplication.cpp'), provenance('tqt_rules', 'src/kernel/tqcolor.cpp'),
                    provenance('tqt_rules', 'src/kernel/tqpalette.cpp')]


def kde_entries():
    out = []
    for src in ('tde_kcs', 'q4os_kcs'):
        for f in manifest(src):
            name, cols, contrast = parse_kcsrc(local_path(src, f))
            c = T.KDE3_DEFAULT_CONTRAST if contrast is None else contrast
            p = T.kde3_palette(cols['background'], cols['foreground'], c)
            computed = {f'kde3:{k}': v for k, v in p.items()}
            prov = provenance(src, f)
            if contrast is not None: prov['contrast'] = contrast
            rule = {'id': 'kde3', 'contrast': c}
            out.append(entry('kde3', name, name, prov, cols, computed, imitates=KDE_IMITATES.get(name), rule=rule))
            out[-1]['corroborated'] = 0
    return out


# ------------------------------------------------------------------ CDE
CDE_RULE_SOURCES = [provenance('motif_rules', 'lib/Xm/Color.c'), provenance('motif_rules', 'lib/Xm/ColorP.h'),
                    provenance('motif_rules', 'lib/Xm/Xm.h.in'), provenance('cde_rules', 'cde/programs/dtsession/SrvFile_io.c')]
CDE_SET_SOURCES = [provenance('motif_rules', 'lib/Xm/ColorObj.c'), provenance('cde_rules', 'cde/programs/dtsession/SrvPalette.c'),
                   provenance('cde_rules', 'cde/programs/dtwm/WmResource.c'), provenance('cde_rules', 'cde/programs/dtwm/Dtwm.defs.src')]
CDE_SETS = ('1 active window frame', '2 inactive window frame', '3 workspace backdrop / switch', '4 text and lists',
            '5 primary (application background)', '6 secondary (menus, dialogs)', '7 workspace backdrop / switch',
            '8 front panel')
RULES = {
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
    app = by['warptempo-2026-10-03']
    for n, v in app['raw'].items(): assert v == hx(k[n]), n
    assert [app['roles'][x] for x in ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow',
                                      'selected_fill', 'selected_text', 'info_ground', 'info_text', 'field_ground')] == \
        ['#303030', '#FCFCFC', '#5E5E5E', '#434343', '#1E1E1E', '#0A0A0A', '#96BFDA', '#000000', '#FFFFE1', '#000000', '#141618']


def main():
    entries = windows_entries() + kde_entries()
    cde, mono = cde_entries(); entries += cde
    app, k = app_entry(); entries.append(app)
    entries.sort(key=lambda e: FAMILIES.index(e['family']))
    checks(entries, k)
    doc = {
        'what': 'The Warptempo theme catalog (tools/theme_catalog/build.py; architect 2026-10-03: imported themes only, '
                'no derivation). Every colour is a recorded byte with its provenance; where the source records only base '
                'colours and its toolkit computed the rest at run time (KDE 3, CDE / Motif), that toolkit\'s own rule ran '
                'once at import and its rule and sources are named in the entry\'s provenance. "raw" holds every value '
                'the source records under its own key names; "roles" the catalog roles (tools/theme_catalog/roles.py); '
                'a role a source has no word for is absent and the app\'s own value applies. Bytes are #RRGGBB as the '
                'source records them (the renderer takes a theme byte as a Display-P3 byte as-is).',
        'roles': list(ROLES),
        'rules': RULES,
        'not_imported': {'cde_monochrome': [f'{m}.dp' for m in mono],
                         'reason': 'X colour names for monochrome displays; dtsession refuses them on a colour display '
                                   '(SrvFile_io.c ParsePaletteInfo)'},
        'entries': [{x: v for x, v in e.items() if x != 'corroborated'} for e in entries],
    }
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, 'w').write(json.dumps(doc, indent=1, ensure_ascii=True) + '\n')
    print(f'wrote {os.path.relpath(OUT, REPO)}: {len(entries)} entries')
    for fam in FAMILIES:
        es = [e for e in entries if e['family'] == fam]
        srcs = set()
        for e in es:
            for q in e['provenance']['sources']:
                srcs.add(q.get('repository', q.get('image', q.get('project'))) + '@' + str(q.get('commit', '')))
        print(f'{fam:13s} entries {len(es):3d}  corroborated {sum(1 for e in es if e["corroborated"]):3d}  '
              f'sources {len(srcs)}: ' + ', '.join(sorted(s.split("@")[0] for s in srcs)))
    if mono: print(f'not imported: {", ".join(m + ".dp" for m in mono)} (monochrome palettes, refused on a colour display)')


if __name__ == '__main__':
    main()
