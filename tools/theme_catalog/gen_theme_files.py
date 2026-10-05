#!/usr/bin/env python3
# tools/theme_catalog/gen_theme_files.py — docs/themes/catalog.json -> assets/themes/<key>.theme, THE BUNDLED THEME
# FILES (architect 2026-10-04: every theme but the one built-in ships as a file bundled with the app; 2026-10-05: the
# app copies the bundle into its themes/ folder at every launch, src/gui/theme_file.h's head). One file per catalog
# entry EXCEPT `windows-95-standard`, the compiled built-in (a file of that name is the launch's hard fail), so the
# folder holds exactly the catalog's other entries and nothing else: a stale file is deleted.
#
# A FILE NAMES THE ROLES THE ENTRY RECORDS (the grammar, theme_file.h's head: `role=value` lines; a role a file does
# not name takes the built-in's value), each value the entry's recorded byte, nothing derived:
#   THE CHROME, the entry as recorded (roles.light_roles, the one light-roles function): ground, label, the relief
#     quartet (hilight, light_3d, shadow, dk_shadow), the selected pair (a CDE entry's its title_active under colour
#     set 1's Motif foreground, light_roles' rule) and the field pair.
#   THE CLOCK PANEL, clock_ground / clock_text = the entry's ground and label: Windows' status bar is ButtonFace /
#     ButtonText, a mapping by Windows' own rule (the built-in's own two roles are the same pair).
#   THE CAPTION (architect 2026-10-05), the six caption roles = the entry's recorded title colours (roles.caption_roles,
#     the one caption mapping: Windows' ActiveTitle / GradientActiveTitle / TitleText and the inactive three, KDE 3's
#     active / inactive background and foreground, CDE's colour sets 1 and 2 under their Motif foregrounds), A
#     GRADIENT END ONLY WHERE THE ENTRY RECORDS ONE: no end line otherwise, and the app's theme-file rule makes the end
#     the start (a flat caption, src/gui/theme_file.h's head), which resolved_roles below applies too.
#   THE CARD, card_ground / card_text = the entry's recorded info pair (roles.py: Windows' InfoWindow / InfoText; the
#     two Windows families record it, KDE 3 and CDE have no tooltip pair, the chosen `warptempo` and the presets carry
#     none), unnamed where the entry records none. card_frame is never named: the
#     built-in's black is Windows' tooltip border, and no entry records a frame colour of its own (every Windows entry's
#     raw WindowFrame is #000000, which no role reads).
#   THE PROGRAM'S ROLES only where the entry records them: the imported entries and the chosen `warptempo` name none; HIS PRESETS (`warptempo-preset-<n>`, build.py preset_entries over the picker's presets.json, architect
#     2026-10-04) name their program colours (PRESET_ELEMENT_ROLES below), each resolved through the picker's own theme
#     (tools/palette/themes/picker.json with the preset's colours applied, render.picker_apply), so every value is the
#     colour the picker painted: the canvas, the ink and THE OUTLINE (architect 2026-10-05: the picker's Outline
#     element, no inherited default) -- the preset's own `waveform_outline` where it records one, else, for a preset
#     saved before that element, the rule the picker then painted it by (the 50 % linear-light blend of the ink over
#     the canvas, C.outline_of, named only where the preset records both), so the files of those presets stay as they
#     were; the playhead's head
#     and stem; THE FLAGS one to one (architect 2026-10-05: the picker's flag elements follow the product's flag kinds,
#     keyed by its role names): Warp Flag / Selected Warp Flag onto the warp pair, Phase Reset Flag / Selected Phase
#     Reset Flag onto the phase-reset pair, Added Flag / Selected Added Flag onto the added pair and Removed Flag /
#     Selected Removed Flag onto the removed pair (the invalid flag wears it). A PRESET SAVED BEFORE THAT ROUND names
#     the old keys (OLD_PRESET_KEYS), read as the picker reads them: its Unselected / Selected Flag onto BOTH the warp
#     and the phase-reset pair (when he picked, one flag colour painted every authored kind, so this reproduces what the
#     picker showed him), its Unselected / Selected Invalid Flag onto the removed pair; a new key the preset names
#     itself wins over an old one. THE LABELS:
#     label and field_text the preset's label as build.py's preset rule gives them (the chrome above); the flag labels
#     are never named, the picker having no flag-label element (it shows them fixed white), so a preset records none and
#     the built-in's apply, white on both (architect 2026-10-05), as the picker painted them.
#   Before writing, each preset's chrome is checked against the picker: its light roles must equal the chrome the
#   picker paints for that preset, else a stale catalog (re-run build.py) or a changed picker theme (a ruling).
#
# BYTE-STABLE: the roles in the role table's order (kGuiThemeRoles, src/gui/theme_file.h, read here and checked
# against ROLE_ORDER), uppercase `#RRGGBB`, LF, no timestamp; run it twice and the folder is identical. A catalog or
# presets change is build.py, then this, the outputs committed together.
#
#   python3 tools/theme_catalog/gen_theme_files.py
import json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PALETTE = os.path.join(REPO, 'tools', 'palette')
sys.path.insert(0, HERE)
sys.path.insert(0, PALETTE)
import roles                                                           # noqa: E402
import render                                                          # noqa: E402  (tools/palette: the picker's theme)
import common as C                                                     # noqa: E402
from build import PRESETS, PRESET_PREFIX                               # noqa: E402

CATALOG = os.path.join(REPO, 'docs', 'themes', 'catalog.json')
OUT = os.path.join(REPO, 'assets', 'themes')
THEME_FILE_H = os.path.join(REPO, 'src', 'gui', 'theme_file.h')
PICKER_THEME = os.path.join(PALETTE, 'themes', 'picker.json')
BUILTIN = 'windows-95-standard'
SUFFIX = '.theme'

# THE ROLE TABLE'S ORDER (kGuiThemeRoles), checked against the header at every run
ROLE_ORDER = ('ground', 'label', 'hilight', 'light_3d', 'shadow', 'dk_shadow', 'selected_fill', 'selected_text',
              'field_ground', 'field_text', 'clock_ground', 'clock_text', 'card_ground', 'card_text', 'card_frame',
              'caption_active', 'caption_active_gradient', 'caption_active_text', 'caption_inactive',
              'caption_inactive_gradient', 'caption_inactive_text',
              'waveform_canvas', 'waveform_ink', 'waveform_outline', 'warp_flag', 'warp_flag_selected',
              'phase_reset_flag', 'phase_reset_flag_selected', 'added_flag', 'added_flag_selected', 'removed_flag',
              'removed_flag_selected', 'flag_label', 'flag_label_selected', 'playhead_head', 'playhead_stem')
# the chrome: theme-file role <- roles.light_roles' name
CHROME = (('ground', 'ground'), ('label', 'label'), ('hilight', 'bevel_hilight'), ('light_3d', 'bevel_light'),
          ('shadow', 'bevel_shadow'), ('dk_shadow', 'bevel_dkshadow'), ('selected_fill', 'selected_fill'),
          ('selected_text', 'selected_text'), ('field_ground', 'field_ground'), ('field_text', 'field_text'),
          ('clock_ground', 'ground'), ('clock_text', 'label'))
# the card: theme-file role <- the catalog role (named only when the entry records both)
CARD = (('card_ground', 'info_ground'), ('card_text', 'info_text'))
# the flat caption's pairs (kGuiThemeCaptionGradients, src/gui/theme_file.h): a file naming a start and not its end
# gets the end equal to the start
CAPTION_GRADIENTS = (('caption_active', 'caption_active_gradient'), ('caption_inactive', 'caption_inactive_gradient'))
# a preset's picker elements -> the theme-file roles each colours (an older preset's outline is the rule's, below)
PRESET_ELEMENT_ROLES = (('canvas', ('waveform_canvas',)), ('ink', ('waveform_ink',)),
                        ('waveform_outline', ('waveform_outline',)),
                        ('warp_flag', ('warp_flag',)), ('warp_flag_selected', ('warp_flag_selected',)),
                        ('phase_reset_flag', ('phase_reset_flag',)),
                        ('phase_reset_flag_selected', ('phase_reset_flag_selected',)),
                        ('added_flag', ('added_flag',)), ('added_flag_selected', ('added_flag_selected',)),
                        ('removed_flag', ('removed_flag',)), ('removed_flag_selected', ('removed_flag_selected',)),
                        ('playhead_head', ('playhead_head',)), ('playhead_stem', ('playhead_stem',)))
# the flag elements' keys before 2026-10-05 -> the elements that replaced them (the picker's renamed_key,
# tools/palette/picker/src/scene.h, the same table)
OLD_PRESET_KEYS = {'unselected_flag': ('warp_flag', 'phase_reset_flag'),
                   'selected_flag': ('warp_flag_selected', 'phase_reset_flag_selected'),
                   'unselected_invalid_flag': ('removed_flag',), 'selected_invalid_flag': ('removed_flag_selected',)}


def renamed(cols):
    """A preset's colours {key: '#RRGGBB'} under the picker's keys: each old key's colour to every new key the preset
    does not name itself (the head's rule)."""
    out = {k: v for k, v in cols.items() if k not in OLD_PRESET_KEYS}
    for old, new in OLD_PRESET_KEYS.items():
        if old in cols:
            for k in new: out.setdefault(k, cols[old])
    return out


def role_table():
    """kGuiThemeRoles read off src/gui/theme_file.h -> ((name, the built-in's '#RRGGBB'), ...) in order."""
    text = open(THEME_FILE_H).read()
    body = text[text.index('kGuiThemeRoles[] = {'):]
    body = body[:body.index('};')]
    return tuple((n, '#' + v.upper()) for n, v in
                 re.findall(r'\{"([a-z0-9_]+)",\s*&GuiPalette::\w+,\s*0x([0-9A-Fa-f]{6})\}', body))


def role_table_names():
    """kGuiThemeRoles' names in order."""
    return tuple(n for n, _ in role_table())


def preset_program_roles(e, preset, picker):
    """One preset entry and its presets.json record -> {theme-file role: '#RRGGBB'} for the program colours it records
    (the head's rule), after checking the entry's chrome is the chrome the picker paints for it."""
    n, cols = preset['number'], renamed(preset['colours'])
    th = render.Theme(PICKER_THEME, data=render.picker_apply(picker, picker['picker']['elements'], cols))
    light = roles.light_roles(e)
    painted = {r: C.hexs(th.get(r)) for r in roles.LIGHT_ROLES}
    if light != painted:
        raise SystemExit(f'gen_theme_files: {e["key"]}\'s chrome differs from the chrome the picker paints for Preset '
                         f'{n} ({ {r: (light[r], painted[r]) for r in light if light[r] != painted[r]} }); re-run '
                         f'build.py, or rule on the picker theme\'s change')
    element_role = {x['key']: x['role'] for x in picker['picker']['elements']}
    out = {}
    for el, rs in PRESET_ELEMENT_ROLES:
        if el in cols:
            for r in rs: out[r] = C.hexs(th.get(element_role[el]))
    if 'waveform_outline' not in cols and 'ink' in cols and 'canvas' in cols:   # a preset from before the element
        out['waveform_outline'] = C.hexs(C.outline_of(th.get('ink'), th.get('canvas')))
    return out


def file_roles(e, presets, picker):
    """One catalog entry -> {theme-file role: '#RRGGBB'} (the head's rule)."""
    light = roles.light_roles(e)
    out = {role: light[src] for role, src in CHROME}
    out.update(roles.caption_roles(e))
    if all(src in e['roles'] for _, src in CARD):
        out.update({role: e['roles'][src] for role, src in CARD})
    if e['key'].startswith(PRESET_PREFIX):
        n = int(e['key'][len(PRESET_PREFIX):])
        if n not in presets: raise SystemExit(f'gen_theme_files: {e["key"]} has no preset {n} in {PRESETS}')
        out.update(preset_program_roles(e, presets[n], picker))
    return out


def inputs():
    """What file_roles reads beside the entry: (presets.json's presets by number, the picker's theme)."""
    return {p['number']: p for p in json.load(open(PRESETS))['presets']}, json.load(open(PICKER_THEME))


def resolved_roles(e, presets, picker):
    """One catalog entry -> every role's '#RRGGBB' as the app resolves its theme (theme_file.h's head): the built-in's
    values, overwritten by the roles the entry's file names; the built-in itself, which has no file, its own values.
    Read by crops.py, so a crop paints what the app paints."""
    out = dict(role_table())
    if e['key'] != BUILTIN:
        named = file_roles(e, presets, picker)
        out.update(named)
        for start, end in CAPTION_GRADIENTS:
            if start in named and end not in named: out[end] = named[start]
    return out


def text_of(rs):
    for role, v in rs.items():
        assert role in ROLE_ORDER, role
        assert re.fullmatch(r'#[0-9A-Fa-f]{6}', v), (role, v)
    return ''.join(f'{role}={rs[role].upper()}\n' for role in ROLE_ORDER if role in rs)


def main():
    if sys.argv[1:]: raise SystemExit('usage: python3 tools/theme_catalog/gen_theme_files.py')
    if role_table_names() != ROLE_ORDER:
        raise SystemExit(f'gen_theme_files: {THEME_FILE_H}\'s kGuiThemeRoles is {role_table_names()}, not ROLE_ORDER')
    entries = json.load(open(CATALOG))['entries']
    if BUILTIN not in [e['key'] for e in entries]: raise SystemExit(f'gen_theme_files: no {BUILTIN} in the catalog')
    presets, picker = inputs()
    os.makedirs(OUT, exist_ok=True)
    want = {}
    for e in entries:
        if e['key'] == BUILTIN: continue
        want[e['key'] + SUFFIX] = text_of(file_roles(e, presets, picker))
    stale = sorted(set(os.listdir(OUT)) - set(want))
    for s in stale: os.remove(os.path.join(OUT, s))
    for name, text in want.items():
        with open(os.path.join(OUT, name), 'w', newline='\n') as f: f.write(text)
    print(f'wrote {os.path.relpath(OUT, REPO)}/: {len(want)} theme files (every catalog entry but {BUILTIN}); '
          f'deleted {len(stale)} stale' + (': ' + ', '.join(stale) if stale else ''))


if __name__ == '__main__':
    main()
