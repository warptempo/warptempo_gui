#!/usr/bin/env python3
# tools/theme_catalog/gen_theme_files.py — docs/themes/catalog.json -> THE BUILT-IN SCHEMES' GENERATED INCLUDES (below),
# with the check of the compiled Windows 2000 caption against its catalog entry.
# A SCHEME IS THE CAPTION ALONE (architect 2026-10-10: "the caption remains the only thing that's outside of Cool Edit";
# "scheme files shrink to the caption's keys"): every other chrome color inherits the live palette's Cool Edit tones
# (src/gui/chrome_derive.h), so what ships of a catalog entry is its six caption keys. The catalog stays the RECORD —
# every entry's bytes with their provenance, its chrome, relief, selection, field and info roles among them — and the
# SOURCE of each built-in scheme's caption; nothing else ships from it. One chrome, one compiled caption (architect
# 2026-10-09, the Windows line alone):
#   WINDOWS 2000's, `windows-2000-standard`, HAND-RECORDED as kGuiCaptionWin2000 (src/gui/theme_file.h) and CHECKED
#     here against its catalog entry's caption.
#
# THE CAPTION AN ENTRY NAMES (architect 2026-10-05), the six caption roles = the entry's recorded title colours
# (roles.caption_roles, the one caption mapping: Windows' ActiveTitle / GradientActiveTitle / TitleText and the
# inactive three, KDE 3's active / inactive background and foreground, CDE's colour sets 1 and 2 under their Motif
# foregrounds), A GRADIENT END ONLY WHERE THE ENTRY RECORDS ONE. THE FLAT CAPTION (architect 2026-10-05): an entry
# naming a caption's start and not its end gets the end EQUAL TO THE START — a flat caption, as a theme recording no
# gradient drew one — never the Windows 2000 end under the entry's start, a gradient no one recorded
# (CAPTION_GRADIENTS). Nothing derived.
#
# THE BUILT-IN SCHEMES (architect 2026-10-08 ~11:00: "the catalog's schemes are transcribed to the used keys and
# compiled in"; the caption's six since 2026-10-10): EVERY catalog entry but the DROPPED ones, in the catalog's order,
# written as src/gui/chrome_schemes.inc — the rows of kGuiChromeSchemes (src/gui/palette_file.h, which owns what a
# scheme is and does) — each its key verbatim (the `scheme` device key's word for it), its DISPLAY NAME and THE SIX
# CAPTION KEYS (kGuiChromeLines, palette_file.h, in that order): THE TITLE = caption_active, its END
# caption_active_gradient where the entry records one, else THE START (the flat caption), its TEXT caption_active_text,
# and the inactive three the same off the inactive roles. THE DROPPED ENTRY (DROPPED): `clearlooks`, GNOME 2's, whose
# chrome left the product 2026-10-09 (architect 2026-10-10: "we can remove the Clearlooks theme"); it stays in the
# catalog as its record. A SCHEME CARRIES NO FACE (architect 2026-10-09 ~21:20: "the scheme's default font should stop
# being honored — it should only be honored from the font picker"; the face is the device config's `font` key,
# src/gui/gui_font.h): an entry's `font` record stays the catalog's RECORD and this script reads none of it.
# WINDOWS ME STANDARD's six are checked equal to Windows 2000 Standard's (Me's one difference from 2000 was its font).
# THE DISPLAY NAME is the family's word, then the catalog name in Title Case (SCHEME_FAMILY_WORD,
# scheme_display_name): "Windows Rainy Day", "Plus Space" (the Plus! themes' colour-depth tag dropped), "KDE 3 Storm",
# "CDE Northern Sky" (CDE's run-together names parted at their capitals); a name that already opens with the family's
# word takes it once ("Windows 2000 Standard"), and the product's own are their names alone ("Cool Edit Pro ME").
# Keys and display names are checked unique, and every display name printable ASCII within the palette name grammar's
# 40 bytes (a typed preset name equal to one is refused as taken, color_picker.cpp's commit_name).
# THE PRODUCT'S OWN SCHEMES (architect 2026-10-10, build.py's `warptempo` family: his schemes made built-ins): their
# rows stand in the include like any entry's, and their KEYS are written a second time, in the catalog's order, as
# src/gui/chrome_schemes_product.inc — the rows of kGuiProductSchemeKeys (palette_file.h), which seats them in the
# picker's Chrome menu in its first group, after the chromes' own (color_picker.cpp's preset_menu_rows); the scheme
# rows carry no family, so the family is read here, once, and the menu's group stays derived from the catalog.
#
# BYTE-STABLE: the keys in kGuiChromeLines' order (read off palette_file.h and checked against SCHEME_KEYS), the
# schemes in the catalog's, uppercase hex, LF, no timestamp; run it twice and the include is identical. THE TWO
# INCLUDES IT WRITES ARE ITS OUTPUT, NEVER HAND-EDITED: a catalog change is build.py, then this, the output committed
# with it.
#
#   python3 -I tools/theme_catalog/gen_theme_files.py
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import roles                                                           # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
THEME_FILE_H = os.path.join(REPO, 'src', 'gui', 'theme_file.h')
WIN2000 = 'windows-2000-standard'   # the hand-recorded caption's catalog entry
SCHEMES_INC = os.path.join(REPO, 'src', 'gui', 'chrome_schemes.inc')
GENERATED = '// GENERATED by tools/theme_catalog/gen_theme_files.py; never hand-edit.\n'
# THE DROPPED ENTRIES (the head): catalog keys no built-in scheme is generated from
DROPPED = ('clearlooks',)

# THE CAPTION'S SIX ROLES (src/gui/theme_file.h's caption block), in GuiChromePick's order
CAPTION_ROLES = ('caption_active', 'caption_active_gradient', 'caption_active_text', 'caption_inactive',
                 'caption_inactive_gradient', 'caption_inactive_text')
# THE FLAT CAPTION's pairs (the head): each caption's start and its gradient end
CAPTION_GRADIENTS = (('caption_active', 'caption_active_gradient'), ('caption_inactive', 'caption_inactive_gradient'))
CAPTION_ROW = r'kGuiCaptionWin2000\{(.*?)\};'


def compiled_caption():
    """kGuiCaptionWin2000 read off src/gui/theme_file.h -> {caption role: '#RRGGBB'}."""
    body = re.search(CAPTION_ROW, open(THEME_FILE_H).read(), re.S)
    if not body: raise SystemExit(f'gen_theme_files: no kGuiCaptionWin2000 in {THEME_FILE_H}')
    words = re.findall(r'0x([0-9A-Fa-f]{6})', body.group(1))
    if len(words) != len(CAPTION_ROLES):
        raise SystemExit(f'gen_theme_files: {THEME_FILE_H}\'s kGuiCaptionWin2000 has {len(words)} words, not six')
    return {role: '#' + w.upper() for role, w in zip(CAPTION_ROLES, words)}


def write(path, text):
    with open(path, 'w', newline='\n') as f: f.write(text)


def caption_words(e):
    """One catalog entry -> its six caption roles {role: '#RRGGBB'}: the roles it records, then THE FLAT CAPTION
    (the head)."""
    named = {role: v.upper() for role, v in roles.caption_roles(e).items()}
    for role, v in named.items():
        assert re.fullmatch(r'#[0-9A-F]{6}', v), (role, v)
    unknown = sorted(set(named) - set(CAPTION_ROLES))
    if unknown: raise SystemExit(f'gen_theme_files: {e["key"]} names caption roles the app lacks: {unknown}')
    for role in ('caption_active', 'caption_active_text', 'caption_inactive', 'caption_inactive_text'):
        if role not in named: raise SystemExit(f'gen_theme_files: {e["key"]} records no {role}')
    for start, end in CAPTION_GRADIENTS:
        if end not in named: named[end] = named[start]
    return named


# THE BUILT-IN SCHEMES (the head): the six caption keys in kGuiChromeLines' order (src/gui/palette_file.h) <- the
# caption role whose byte each takes
SCHEME_KEYS = (('chrome_title_start', 'caption_active'), ('chrome_title_end', 'caption_active_gradient'),
               ('chrome_title_text', 'caption_active_text'),
               ('chrome_inactive_title_start', 'caption_inactive'),
               ('chrome_inactive_title_end', 'caption_inactive_gradient'),
               ('chrome_inactive_title_text', 'caption_inactive_text'))
SCHEME_FAMILY_WORD = {'warptempo': None, 'windows': 'Windows', 'windows-plus': 'Plus', 'kde3': 'KDE 3', 'cde': 'CDE'}
# THE PRODUCT'S OWN SCHEMES (the head): the family whose keys the second include lists
PRODUCT_FAMILY = 'warptempo'
PRODUCT_INC = os.path.join(REPO, 'src', 'gui', 'chrome_schemes_product.inc')
SCHEME_SMALL_WORDS = {'and', 'da', 'of'}   # Title Case keeps them lower inside a name ("Leonardo da Vinci")
LINES_ROW = r'kGuiChromeLines\[\] = \{(.*?)\};'
PALETTE_FILE_H = os.path.join(REPO, 'src', 'gui', 'palette_file.h')


def chrome_line_keys():
    """kGuiChromeLines' keys read off src/gui/palette_file.h, in order."""
    body = re.search(LINES_ROW, open(PALETTE_FILE_H).read(), re.S)
    if not body: raise SystemExit(f'gen_theme_files: no kGuiChromeLines in {PALETTE_FILE_H}')
    return tuple(re.findall(r'\{"(chrome_[a-z_]+)"', body.group(1)))


def scheme_display_name(e):
    """One catalog entry -> its display name (the head's rule)."""
    name = re.sub(r' \((256|high) color\)$', '', e['name'])
    if e['family'] == 'cde': name = re.sub(r'(?<=[a-z])(?=[A-Z])', ' ', name)
    words = name.split(' ')
    name = ' '.join(w if (i and w in SCHEME_SMALL_WORDS) else w[:1].upper() + w[1:] for i, w in enumerate(words))
    word = SCHEME_FAMILY_WORD[e['family']]
    if word is None or name.startswith(word + ' '): return name
    return f'{word} {name}'


def scheme_row(e):
    """One catalog entry -> its kGuiChromeSchemes row (the head's rule)."""
    words = caption_words(e)
    values = ', '.join(f'0x{words[role][1:]}' for _, role in SCHEME_KEYS)
    return f'    {{"{e["key"]}", "{scheme_display_name(e)}", {{{values}}}}},\n'


def write_schemes_include(entries):
    """src/gui/chrome_schemes.inc: every entry's row but the dropped ones', in the catalog's order (the head)."""
    if chrome_line_keys() != tuple(k for k, _ in SCHEME_KEYS):
        raise SystemExit(f'gen_theme_files: {PALETTE_FILE_H}\'s kGuiChromeLines is {chrome_line_keys()}, '
                         'not SCHEME_KEYS')
    keys = [e['key'] for e in entries]
    names = [scheme_display_name(e) for e in entries]
    by_key = {e['key']: e for e in entries}
    me, w2k = by_key['windows-me-standard'], by_key[WIN2000]
    if caption_words(me) != caption_words(w2k):
        raise SystemExit('gen_theme_files: windows-me-standard is not windows-2000-standard\'s six')
    for what, xs in (('key', keys), ('display name', names)):
        dup = sorted({x for x in xs if xs.count(x) > 1})
        if dup: raise SystemExit(f'gen_theme_files: duplicate scheme {what}s: {dup}')
    for n in names:
        if not (0 < len(n) <= 40 and all(0x20 <= ord(c) <= 0x7E for c in n) and '/' not in n and '"' not in n
                and '\\' not in n):
            raise SystemExit(f'gen_theme_files: the display name {n!r} is outside the palette name grammar')
    write(SCHEMES_INC, GENERATED +
          '// THE BUILT-IN SCHEMES (src/gui/palette_file.h\'s kGuiChromeSchemes, included in its initializer): every\n'
          '// catalog entry but the dropped ones in the catalog\'s order, its key, its display name and its six\n'
          '// caption keys in kGuiChromeLines\' order, the entry\'s recorded bytes (docs/themes/catalog.json)\n'
          '// under the generator\'s rules; regenerate, never hand-edit.\n' +
          ''.join(scheme_row(e) for e in entries))
    product = [e['key'] for e in entries if e['family'] == PRODUCT_FAMILY]
    if not product: raise SystemExit(f'gen_theme_files: the catalog has no {PRODUCT_FAMILY} entry')
    write(PRODUCT_INC, GENERATED +
          '// THE PRODUCT\'S OWN SCHEMES (src/gui/palette_file.h\'s kGuiProductSchemeKeys, included in its\n'
          '// initializer): the keys of the catalog\'s `warptempo` entries in the catalog\'s order, each a row of\n'
          '// chrome_schemes.inc; regenerate, never hand-edit.\n' +
          ''.join(f'    "{k}",\n' for k in product))
    return len(entries), len(product)


def main():
    if sys.argv[1:]: raise SystemExit('usage: python3 tools/theme_catalog/gen_theme_files.py')
    entries = json.load(open(CATALOG))['entries']
    by_key = {e['key']: e for e in entries}
    for k in DROPPED + (WIN2000,):
        if k not in by_key: raise SystemExit(f'gen_theme_files: no {k} in the catalog')
    # WINDOWS 2000'S COMPILED CAPTION IS ITS CATALOG ENTRY'S (kGuiCaptionWin2000, theme_file.h)
    compiled, named = compiled_caption(), caption_words(by_key[WIN2000])
    if compiled != named:
        raise SystemExit(f'gen_theme_files: {THEME_FILE_H}\'s kGuiCaptionWin2000 is not {WIN2000}\'s caption: '
                         f'{ {r: (compiled[r], v) for r, v in named.items() if compiled[r] != v} }')
    schemes, product = write_schemes_include([e for e in entries if e['key'] not in DROPPED])
    print(f'wrote {os.path.relpath(SCHEMES_INC, REPO)} and {os.path.relpath(PRODUCT_INC, REPO)}')
    print(f'{schemes} built-in schemes, {product} of them the product\'s own')


if __name__ == '__main__':
    main()
