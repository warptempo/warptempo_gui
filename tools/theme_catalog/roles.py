#!/usr/bin/env python3
# tools/theme_catalog/roles.py — THE ROLE MAPPING, the one table (architect 2026-10-03): which recorded value (or
# which value the source's own toolkit computed at import, toolkit_rules.py) fills each catalog role, per family.
# A role a family has no word for is ABSENT from its row and from every entry of it: the app's own value applies,
# never a guess. The flags, the red, the waveform ink and canvas are not catalog roles (a theme owns the chrome).
# The app-specific roles are drawn from these catalog roles, not stored (architect 2026-10-03, late; crops.py
# theme_for): the ruler label <- label, the ruler ticks and the playhead head <- bevel_shadow, the flag outline <-
# bevel_dkshadow; the flags' shading is the entry's flag_rule (build.py FLAG_RULE).
#
# A value names a key of the entry's VALUES: its raw keys as the source spells them, or a computed key
# "<rule>:<name>" (kde3:light, motif:set5.ts, ...), the rule named in the entry's provenance.

ROLES = ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow',
         'selected_fill', 'selected_text', 'info_ground', 'info_text', 'field_ground', 'field_text',
         'disabled_text', 'title_active', 'title_inactive')

# Windows' system colours by their .theme names (the registry's Control Panel\Colors spelling; COLOR_3DFACE is
# ButtonFace, COLOR_3DHILIGHT ButtonHilight, COLOR_3DLIGHT ButtonLight, COLOR_3DSHADOW ButtonShadow, COLOR_3DDKSHADOW
# ButtonDkShadow). Every Windows family reads the same row.
_WINDOWS = {
    'ground': 'ButtonFace', 'label': 'ButtonText',
    'bevel_hilight': 'ButtonHilight', 'bevel_light': 'ButtonLight', 'bevel_shadow': 'ButtonShadow',
    'bevel_dkshadow': 'ButtonDkShadow',
    'selected_fill': 'Hilight', 'selected_text': 'HilightText', 'info_ground': 'InfoWindow', 'info_text': 'InfoText',
    'field_ground': 'Window', 'field_text': 'WindowText', 'disabled_text': 'GrayText',
    'title_active': 'ActiveTitle', 'title_inactive': 'InactiveTitle',
}

MAPPING = {
    'windows': _WINDOWS,
    'windows-plus': _WINDOWS,
    # KDE 3 (createApplicationPalette, toolkit_rules.kde3_palette): the ground is the scheme's `background`, which the
    # relief is computed from (the button face, `buttonBackground`, is recorded raw but no catalog role: the app has
    # one ground); the disabled text is the toolkit's disabled foreground. kcsrc records no tooltip colours.
    'kde3': {
        'ground': 'background', 'label': 'foreground',
        'bevel_hilight': 'kde3:light', 'bevel_light': 'kde3:midlight', 'bevel_shadow': 'kde3:dark',
        'bevel_dkshadow': 'kde3:shadow',
        'selected_fill': 'selectBackground', 'selected_text': 'selectForeground',
        'field_ground': 'windowBackground', 'field_text': 'windowForeground',
        'disabled_text': 'kde3:disabled_foreground',
        'title_active': 'activeBackground', 'title_inactive': 'inactiveBackground',
    },
    # CDE (a .dp palette is the eight colour sets' backgrounds; Motif computes each set's foreground, select colour
    # and shadows, toolkit_rules.motif_colors). The sets' roles (Motif ColorObj.c's resource defaults, dtsession
    # SrvPalette.c, dtwm WmResource.c / Dtwm.defs): 1 the active window frame, 2 the inactive frame, 4 the text and
    # list areas, 5 THE PRIMARY (an application's main background; dtsession's high-colour *background), 6 the
    # secondary (menu bars, menus, dialogs), 3 and 7 workspace backdrops / switch buttons (dtwm's high-colour map
    # 3, 5, 6, 7), 8 the front panel. Motif paints two shadows, so the quartet is (ts, ts, bs, bs). Motif has no
    # selection-highlight colour (text selection is inverse video; the select colour fills set toggles and armed
    # buttons), no tooltip pair and no disabled colour (insensitive text is stippled): those roles are absent.
    'cde': {
        'ground': 'set5', 'label': 'motif:set5.fg',
        'bevel_hilight': 'motif:set5.ts', 'bevel_light': 'motif:set5.ts', 'bevel_shadow': 'motif:set5.bs',
        'bevel_dkshadow': 'motif:set5.bs',
        'field_ground': 'set4', 'field_text': 'motif:set4.fg',
        'title_active': 'set1', 'title_inactive': 'set2',
    },
    # The app itself (src/gui/render.h's constants): the highlight is the accent with the highlight text rule's
    # dark ink, the field is the modal text field (the canvas under the label white).
    'warptempo': {
        'ground': 'kRedesignContentGround', 'label': 'kRedesignLabel',
        'bevel_hilight': 'kReliefHilight', 'bevel_light': 'kRelief3DLight', 'bevel_shadow': 'kReliefShadow',
        'bevel_dkshadow': 'kReliefDkShadow',
        'selected_fill': 'kRedesignAccent', 'selected_text': 'kRedesignHighlightLabel',
        'info_ground': 'kInfoGround', 'info_text': 'kInfoText',
        'field_ground': 'kModalFieldGround', 'field_text': 'kRedesignLabel',
    },
}
for _fam, _row in MAPPING.items():
    assert set(_row) <= set(ROLES), (_fam, set(_row) - set(ROLES))


def map_roles(family, values):
    """-> {role: '#RRGGBB'} for one entry: each mapped role from the entry's values (a missing value is a fail: the
    family's row names a value every entry of it has)."""
    out = {}
    for role in ROLES:
        k = MAPPING[family].get(role)
        if k is None: continue
        if k not in values: raise SystemExit(f'roles: family {family} maps {role} to {k!r}, which the entry lacks')
        out[role] = values[k]
    return out
