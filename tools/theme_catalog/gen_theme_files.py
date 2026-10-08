#!/usr/bin/env python3
# tools/theme_catalog/gen_theme_files.py — docs/themes/catalog.json -> THE COMPILED CHROME THEMES' GENERATED INCLUDES.
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
# BYTE-STABLE: the roles in the role table's order (kGuiThemeRoles, src/gui/theme_file.h, read here and checked
# against ROLE_ORDER), uppercase hex, LF, no timestamp; run it twice and the includes are identical. A catalog change
# is build.py, then this, the outputs committed together.
#
#   python3 tools/theme_catalog/gen_theme_files.py
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
    for path in (ROLES_INC, MEMBERS_INC, VALUES_INC): print(f'wrote {os.path.relpath(path, REPO)}')
    print(f'{len(clearlooks_tones(entries))} Clearlooks block roles; the Clearlooks theme {len(words)} roles')


if __name__ == '__main__':
    main()
