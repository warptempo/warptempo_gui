#!/usr/bin/env python3
# tools/theme_catalog/gen_theme_files.py — docs/themes/catalog.json -> THE COMPILED CHROME THEMES' GENERATED INCLUDES
# and THE BUILT-IN SCHEMES' (below).
# EVERY CHROME'S COLORS ARE COMPILED IN (architect 2026-10-08: "okay to retire the color theme catalog"; "we just need
# hard-coded chromes"): no theme file ships and the app reads none (src/gui/theme_file.h's head). The catalog stays the
# RECORD — every entry's bytes with their provenance — and the SOURCE of each chrome's compiled bytes; an official new
# look becomes a new chrome variant, its theme compiled in the same way. Two chromes, two themes:
#   WINDOWS 2000's, `windows-2000-standard`, HAND-RECORDED as the role table's value column (kGuiThemeRoles,
#     src/gui/theme_file.h) and CHECKED here against its catalog entry: the bytes the entry's chrome roles name.
#   CLEARLOOKS', `clearlooks`, GENERATED here whole as src/gui/theme_clearlooks_values.inc — every role of the table,
#     in its order, under the rules below.
#
# THE CHROME ROLES AN ENTRY NAMES, each value the entry's recorded byte, nothing derived:
#   THE CHROME, the entry as recorded (roles.light_roles, the one light-roles function): ground, label, the relief
#     quartet (hilight, light_3d, shadow, dk_shadow), the selected pair (a CDE entry's its title_active under colour
#     set 1's Motif foreground, light_roles' rule) and the field pair.
#   THE CLOCK PANEL, clock_ground / clock_text = the entry's ground and label: Windows' status bar is ButtonFace /
#     ButtonText, a mapping by Windows' own rule (the Windows 2000 theme's own two roles are the same pair); A GNOME 2
#     ENTRY'S its field pair (CLOCK_FROM_FIELD, architect 2026-10-07: GTK has no sunken status panel, and a time shown
#     in a field is an entry, base / text).
#   THE CAPTION (architect 2026-10-05), the six caption roles = the entry's recorded title colours (roles.caption_roles,
#     the one caption mapping: Windows' ActiveTitle / GradientActiveTitle / TitleText and the inactive three, KDE 3's
#     active / inactive background and foreground, CDE's colour sets 1 and 2 under their Motif foregrounds), A
#     GRADIENT END ONLY WHERE THE ENTRY RECORDS ONE. THE FLAT CAPTION (architect 2026-10-05): an entry naming a
#     caption's start and not its end gets the end EQUAL TO THE START — a flat caption, as a theme recording no
#     gradient drew one — never the Windows 2000 end under the entry's start, a gradient no one recorded
#     (CAPTION_GRADIENTS; Clearlooks' metacity band records none).
#   THE CARD, card_ground / card_text = the entry's recorded info pair (roles.py: Windows' InfoWindow / InfoText; the
#     two Windows families and GNOME 2 record it, KDE 3 and CDE have no tooltip pair), unnamed where the entry records
#     none. card_frame = the entry's recorded info_frame where it records one (GNOME 2: Clearlooks' tooltip border,
#     shade 0.6 of the tooltip's ground), else unnamed: Windows 2000's black is Windows' tooltip border, and no other
#     entry records a frame colour of its own (every Windows entry's raw WindowFrame is #000000, which no role reads).
#   NO PROGRAM ROLE (architect 2026-10-07): the program's colors are THE PALETTE's (src/gui/palette_file.h).
#   A ROLE THE ENTRY DOES NOT NAME takes Windows 2000's byte (the table's column): none of Clearlooks' does, once the
#     flat caption has filled its two gradient ends.
#
#   THE CLEARLOOKS PAINTERS' TONES (architect 2026-10-07, the painters round): the GNOME 2 entry's `engine_tones`
#     (build.py engine_tones: the Clearlooks engine's and metacity's own arithmetic at the product's geometry), every
#     one named, after the table's other roles. THEY ARE ALSO THE ROLE TABLE'S CLEARLOOKS BLOCK: this script writes
#     the generated includes src/gui/theme_clearlooks_roles.inc (the kGuiThemeRoles rows, each tone's column byte
#     squeeze's own, which no Windows 2000 painter reads) and src/gui/theme_clearlooks_members.inc (the GuiPalette
#     members), so a tone joining or leaving is build.py, then this, then the build — never a hand edit of an include.
#
# THE BUILT-IN SCHEMES (architect 2026-10-08 ~11:00: "the catalog's schemes are transcribed to the used keys and
# compiled in"; "anything beyond the keys we use is extraneous"): EVERY catalog entry, in the catalog's order, written
# as src/gui/chrome_schemes.inc — the rows of kGuiChromeSchemes (src/gui/palette_file.h, which owns what a scheme is
# and does) — each its key verbatim (the `palette` device key's word for it), its DISPLAY NAME and THE TWELVE CHROME
# KEYS (kGuiChromeLines, palette_file.h, in that order), each the entry's recorded byte under the rules above, nothing
# derived: ground = the entry's ground, text = its label (roles.light_roles); THE TITLE = caption_active, its END
# caption_active_gradient where the entry records one, else THE START (the flat caption), its TEXT
# caption_active_text, and the inactive three the same off the inactive roles (roles.caption_roles); THE SELECTION =
# the selected pair (roles.selected_pair: a CDE entry, which records none, its title_active under colour set 1's
# foreground); THE FIELD = the field pair. THE DISPLAY NAME is the family's word, then the catalog name in Title Case
# (SCHEME_FAMILY_WORD, scheme_display_name): "Windows Rainy Day", "Plus Space" (the Plus! themes' colour-depth tag
# dropped), "KDE 3 Storm", "CDE Northern Sky" (CDE's run-together names parted at their capitals); a name that
# already opens with the family's word takes it once ("Windows 2000 Standard"), and the one GNOME 2 entry, the
# clearlooks chrome's own, is its name alone ("Clearlooks", the Settings Chrome row's word). Keys and display names
# are checked unique, and every display name printable ASCII within the palette name grammar's 40 bytes (a typed
# preset name equal to one is refused as taken, color_picker.cpp's commit_name).
#
# BYTE-STABLE: the roles in the role table's order (kGuiThemeRoles, src/gui/theme_file.h, read here and checked
# against ROLE_ORDER), the schemes in the catalog's, uppercase hex, LF, no timestamp; run it twice and the includes
# are identical. EVERY INCLUDE IT WRITES IS ITS OUTPUT, NEVER HAND-EDITED: a catalog change is build.py, then this,
# the outputs committed together.
#
#   python3 -I tools/theme_catalog/gen_theme_files.py
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import roles                                                           # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
THEME_FILE_H = os.path.join(REPO, 'src', 'gui', 'theme_file.h')
WIN2000 = 'windows-2000-standard'   # the hand-recorded column's catalog entry
CLEARLOOKS = 'clearlooks'           # the generated theme's catalog entry
ROLES_INC = os.path.join(REPO, 'src', 'gui', 'theme_clearlooks_roles.inc')
MEMBERS_INC = os.path.join(REPO, 'src', 'gui', 'theme_clearlooks_members.inc')
VALUES_INC = os.path.join(REPO, 'src', 'gui', 'theme_clearlooks_values.inc')
SCHEMES_INC = os.path.join(REPO, 'src', 'gui', 'chrome_schemes.inc')
GENERATED = '// GENERATED by tools/theme_catalog/gen_theme_files.py; never hand-edit.\n'

# THE ROLE TABLE'S ORDER (kGuiThemeRoles), checked against the header at every run
ROLE_ORDER = ('ground', 'label', 'hilight', 'light_3d', 'shadow', 'dk_shadow', 'selected_fill', 'selected_text',
              'field_ground', 'field_text', 'clock_ground', 'clock_text', 'card_ground', 'card_text', 'card_frame',
              'caption_active', 'caption_active_gradient', 'caption_active_text', 'caption_inactive',
              'caption_inactive_gradient', 'caption_inactive_text')
# the chrome: app role <- roles.light_roles' name
CHROME = (('ground', 'ground'), ('label', 'label'), ('hilight', 'bevel_hilight'), ('light_3d', 'bevel_light'),
          ('shadow', 'bevel_shadow'), ('dk_shadow', 'bevel_dkshadow'), ('selected_fill', 'selected_fill'),
          ('selected_text', 'selected_text'), ('field_ground', 'field_ground'), ('field_text', 'field_text'),
          ('clock_ground', 'ground'), ('clock_text', 'label'))
# the families whose clock panel is the field pair (the head's rule): clock role <- roles.light_roles' name
CLOCK_FROM_FIELD = {'gnome2': (('clock_ground', 'field_ground'), ('clock_text', 'field_text'))}
# the card: app role <- the catalog role (named only when the entry records both); its frame named alone
CARD = (('card_ground', 'info_ground'), ('card_text', 'info_text'))
CARD_FRAME = ('card_frame', 'info_frame')
# THE FLAT CAPTION's pairs (the head): each caption's start and its gradient end
CAPTION_GRADIENTS = (('caption_active', 'caption_active_gradient'), ('caption_inactive', 'caption_inactive_gradient'))
ROW = r'\{"([a-z0-9_]+)",\s*&GuiPalette::\w+,\s*0x([0-9A-Fa-f]{6})\}'


def role_table():
    """kGuiThemeRoles read off src/gui/theme_file.h and the Clearlooks block it includes at its end
    (theme_clearlooks_roles.inc, this script's own output) -> ((name, the column's '#RRGGBB'), ...) in order."""
    text = open(THEME_FILE_H).read()
    body = text[text.index('kGuiThemeRoles[] = {'):]
    body = body[:body.index('};')]
    rows = re.findall(ROW, body)
    if os.path.exists(ROLES_INC): rows += re.findall(ROW, open(ROLES_INC).read())
    return tuple((n, '#' + v.upper()) for n, v in rows)


def role_table_names():
    """kGuiThemeRoles' hand-written names in order (the Clearlooks block apart)."""
    return tuple(n for n, _ in role_table() if not n.startswith('cl_'))


def clearlooks_tones(entries):
    """The one GNOME 2 entry's engine tones {role: '#RRGGBB'}, in their order."""
    es = [e for e in entries if 'engine_tones' in e]
    if len(es) != 1: raise SystemExit(f'gen_theme_files: {len(es)} catalog entries carry engine tones, not 1')
    if es[0]['key'] != CLEARLOOKS: raise SystemExit(f'gen_theme_files: the engine tones are {es[0]["key"]}\'s')
    return es[0]['engine_tones']


def write(path, text):
    with open(path, 'w', newline='\n') as f: f.write(text)


def write_block_includes(tones):
    """The role table's Clearlooks block and GuiPalette's members, in the tones' order (the head)."""
    write(ROLES_INC, GENERATED +
          '// THE CLEARLOOKS BLOCK of kGuiThemeRoles (src/gui/theme_file.h, included at the table\'s end):\n'
          '// the Clearlooks painters\' tones, each column byte squeeze\'s own (build.py engine_tones), which no\n'
          '// Windows 2000 painter reads.\n' +
          ''.join(f'    {{"{r}", &GuiPalette::{r}, 0x{v[1:].upper()}}},\n' for r, v in tones.items()))
    write(MEMBERS_INC, GENERATED +
          '// THE CLEARLOOKS BLOCK of GuiPalette (src/gui/render.h, included at the struct\'s end), in\n'
          '// the role table\'s order: the Clearlooks painters\' tones (src/gui/clearlooks_paint.h).\n' +
          ''.join(f'    GuiColor {r};\n' for r in tones))


def entry_roles(e):
    """One catalog entry -> {app role: '#RRGGBB'}, the chrome roles it names (the head's rule)."""
    light = roles.light_roles(e)
    out = {role: light[src] for role, src in CHROME}
    out.update({role: light[src] for role, src in CLOCK_FROM_FIELD.get(e['family'], ())})
    out.update(roles.caption_roles(e))
    if all(src in e['roles'] for _, src in CARD):
        out.update({role: e['roles'][src] for role, src in CARD})
    if CARD_FRAME[1] in e['roles']: out[CARD_FRAME[0]] = e['roles'][CARD_FRAME[1]]
    out.update(e.get('engine_tones', {}))
    for role, v in out.items():
        assert re.fullmatch(r'#[0-9A-Fa-f]{6}', v), (role, v)
    return {role: v.upper() for role, v in out.items()}


def compiled_theme(e):
    """One catalog entry -> every role of the table, in its order: the column's byte, overwritten by each role the
    entry names, then THE FLAT CAPTION (the head)."""
    table = role_table()
    named = entry_roles(e)
    unknown = sorted(set(named) - {n for n, _ in table})
    if unknown: raise SystemExit(f'gen_theme_files: {e["key"]} names roles the table lacks: {unknown}')
    words = {n: named.get(n, v) for n, v in table}
    for start, end in CAPTION_GRADIENTS:
        if start in named and end not in named: words[end] = words[start]
    return words


# THE BUILT-IN SCHEMES (the head): the twelve chrome keys in kGuiChromeLines' order (src/gui/palette_file.h) <- the
# app role of entry_roles / compiled_theme whose byte each takes
SCHEME_KEYS = (('chrome_ground', 'ground'), ('chrome_text', 'label'),
               ('chrome_title_start', 'caption_active'), ('chrome_title_end', 'caption_active_gradient'),
               ('chrome_title_text', 'caption_active_text'),
               ('chrome_inactive_title_start', 'caption_inactive'),
               ('chrome_inactive_title_end', 'caption_inactive_gradient'),
               ('chrome_inactive_title_text', 'caption_inactive_text'),
               ('chrome_selection', 'selected_fill'), ('chrome_selection_text', 'selected_text'),
               ('chrome_field', 'field_ground'), ('chrome_field_text', 'field_text'))
SCHEME_FAMILY_WORD = {'windows': 'Windows', 'windows-plus': 'Plus', 'kde3': 'KDE 3', 'cde': 'CDE', 'gnome2': None}
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
    words = compiled_theme(e)
    named = entry_roles(e)
    for role in ('ground', 'label', 'caption_active', 'caption_active_text', 'caption_inactive',
                 'caption_inactive_text', 'selected_fill', 'selected_text', 'field_ground', 'field_text'):
        if role not in named: raise SystemExit(f'gen_theme_files: {e["key"]} records no {role}')
    values = ', '.join(f'0x{words[role][1:]}' for _, role in SCHEME_KEYS)
    return f'    {{"{e["key"]}", "{scheme_display_name(e)}", {{{values}}}}},\n'


def write_schemes_include(entries):
    """src/gui/chrome_schemes.inc: every entry's row, in the catalog's order (the head)."""
    if chrome_line_keys() != tuple(k for k, _ in SCHEME_KEYS):
        raise SystemExit(f'gen_theme_files: {PALETTE_FILE_H}\'s kGuiChromeLines is {chrome_line_keys()}, '
                         'not SCHEME_KEYS')
    keys = [e['key'] for e in entries]
    names = [scheme_display_name(e) for e in entries]
    for what, xs in (('key', keys), ('display name', names)):
        dup = sorted({x for x in xs if xs.count(x) > 1})
        if dup: raise SystemExit(f'gen_theme_files: duplicate scheme {what}s: {dup}')
    for n in names:
        if not (0 < len(n) <= 40 and all(0x20 <= ord(c) <= 0x7E for c in n) and '/' not in n and '"' not in n
                and '\\' not in n):
            raise SystemExit(f'gen_theme_files: the display name {n!r} is outside the palette name grammar')
    write(SCHEMES_INC, GENERATED +
          '// THE BUILT-IN SCHEMES (src/gui/palette_file.h\'s kGuiChromeSchemes, included in its initializer): every\n'
          '// catalog entry in the catalog\'s order, its key, its display name and its twelve chrome keys in\n'
          '// kGuiChromeLines\' order, the entry\'s recorded bytes (docs/themes/catalog.json) under the generator\'s\n'
          '// rules; regenerate, never hand-edit.\n' +
          ''.join(scheme_row(e) for e in entries))
    return len(entries)


def main():
    if sys.argv[1:]: raise SystemExit('usage: python3 tools/theme_catalog/gen_theme_files.py')
    if role_table_names() != ROLE_ORDER:
        raise SystemExit(f'gen_theme_files: {THEME_FILE_H}\'s kGuiThemeRoles is {role_table_names()}, not ROLE_ORDER')
    entries = json.load(open(CATALOG))['entries']
    by_key = {e['key']: e for e in entries}
    for k in (WIN2000, CLEARLOOKS):
        if k not in by_key: raise SystemExit(f'gen_theme_files: no {k} in the catalog')
    write_block_includes(clearlooks_tones(entries))
    if [n for n, _ in role_table() if n.startswith('cl_')] != list(clearlooks_tones(entries)):
        raise SystemExit(f'gen_theme_files: {THEME_FILE_H} does not include {ROLES_INC} at its table\'s end')
    # WINDOWS 2000'S THEME IS ITS CATALOG ENTRY: the table's column (kGuiThemeRoles) holds the bytes the entry names,
    # the roles it does not name keeping the column's own values (card_frame)
    table = dict(role_table())
    named = entry_roles(by_key[WIN2000])
    if {r: table[r] for r in named} != named:
        raise SystemExit(f'gen_theme_files: {THEME_FILE_H}\'s column is not {WIN2000}\'s bytes: '
                         f'{ {r: (table[r], v) for r, v in named.items() if table[r] != v} }')
    # CLEARLOOKS' THEME, every role
    words = compiled_theme(by_key[CLEARLOOKS])
    write(VALUES_INC, GENERATED +
          '// THE CLEARLOOKS THEME (src/gui/theme_file.h\'s kGuiThemeClearlooksValues, included in its initializer):\n'
          f'// every role of kGuiThemeRoles in the table\'s order, the catalog entry `{CLEARLOOKS}`\'s bytes (squeeze\'s\n'
          '// gtkrc and the engine\'s tones, the theme catalog\'s record) under the generator\'s rules.\n' +
          ''.join(f'    {{"{n}", 0x{v[1:]}}},\n' for n, v in words.items()))
    schemes = write_schemes_include(entries)
    for path in (ROLES_INC, MEMBERS_INC, VALUES_INC, SCHEMES_INC): print(f'wrote {os.path.relpath(path, REPO)}')
    print(f'{len(clearlooks_tones(entries))} Clearlooks block roles; the Clearlooks theme {len(words)} roles; '
          f'{schemes} built-in schemes')


if __name__ == '__main__':
    main()
