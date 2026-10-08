#!/usr/bin/env python3
# tools/theme_catalog/roles.py — THE ROLE MAPPING, the one table (architect 2026-10-03): which recorded value (or
# which value the source's own toolkit computed at import, toolkit_rules.py) fills each catalog role, per family.
# A role a family has no word for is ABSENT from its row and from every entry of it: Windows 2000's value applies,
# never a guess. The flags, the red, the waveform ink and canvas are not catalog roles (a theme owns the chrome).
# The app-specific roles are drawn from these catalog roles, not stored (architect 2026-10-03, late): the ruler label
# <- label, the ruler ticks and the playhead head <- bevel_shadow; the flags' shading is the entry's flag_rule (build.py
# FLAG_RULE).
#
# A value names a key of the entry's VALUES: its raw keys as the source spells them, or a computed key
# "<rule>:<name>" (kde3:light, motif:set5.ts, ...), the rule named in the entry's provenance.

ROLES = ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow',
         'selected_fill', 'selected_text', 'info_ground', 'info_text', 'info_frame', 'field_ground', 'field_text',
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
    # GNOME 2 (architect 2026-10-07: Clearlooks as Debian 6 squeeze shipped it; build.py gnome2_entries). The raw keys
    # are the gtkrc's gtk-color-scheme names (the eight colours every Clearlooks style is written over) and the
    # metacity theme's one literal colour, its focused title's #FFFFFF (`title_text`, the draw_ops it sits in); a
    # `gtk2:` value is the engine's own shade of a scheme colour as the engine paints it (toolkit_rules.gtk2_shade
    # through cairo_byte), a `gtkrc:` value a gtkrc expression as GTK stores it, a `metacity:` value a metacity colour
    # spec as metacity renders it (the derivation of each in the entry's provenance.rule.derivations). THE RELIEF
    # QUARTET IS THE ENGINE'S ONE-LINE EDGE: Clearlooks draws a sunken or raised edge as one line of shade 0.94 / 1.06
    # of the parent's background (clearlooks_draw_inset, the ring round an entry, a pressed button and a scale trough;
    # clearlooks_draw_highlight_and_shade, a frame's inner line), so its quartet is (light, light, dark, dark), as
    # Motif's two shadows give CDE's (ts, ts, bs, bs). The tooltip pair is the tooltips style's bg / fg[NORMAL], which
    # are the scheme's tooltip colours; info_frame its border (clearlooks_draw_tooltip, shade 0.6 of its bg).
    'gnome2': {
        'ground': 'bg_color', 'label': 'fg_color',
        'bevel_hilight': 'gtk2:inset_light', 'bevel_light': 'gtk2:inset_light', 'bevel_shadow': 'gtk2:inset_dark',
        'bevel_dkshadow': 'gtk2:inset_dark',
        'selected_fill': 'selected_bg_color', 'selected_text': 'selected_fg_color',
        'info_ground': 'tooltip_bg_color', 'info_text': 'tooltip_fg_color', 'info_frame': 'gtk2:tooltip_border',
        'field_ground': 'base_color', 'field_text': 'text_color', 'disabled_text': 'gtkrc:fg[INSENSITIVE]',
        'title_active': 'selected_bg_color', 'title_inactive': 'bg_color',
    },
}
for _fam, _row in MAPPING.items():
    assert set(_row) <= set(ROLES), (_fam, set(_row) - set(ROLES))


# THE CHROME AS RECORDED, the one light-roles function (the dark level retired 2026-10-04: a dark look is a theme he
# designs): one catalog entry -> {name: '#RRGGBB'} over LIGHT_ROLES, read by gen_theme_files.py (the compiled
# themes; the frozen crops, docs/themes/crops/, were rendered through it too), whose CHROME maps its names onto the
# app's roles: the ground, the label and the relief quartet as recorded; the emboss's light copy the recorded
# Hilight (Windows' DSS_DISABLED; the app's emboss reads Hilight, render.h's palette block); the selected pair the
# entry's selected_fill / selected_text, or for a CDE entry, which records none (Motif selects by inverse video), its
# title_active under colour set 1's own Motif foreground; the field pair the entry's, recorded on every entry. The
# info pair (and its frame), the disabled text and the title bars are not here: the generator names the card from the
# info pair (and the frame) where an entry records it, and no app role reads the rest.
LIGHT_ROLES = ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow', 'emboss_hilight',
               'selected_fill', 'selected_text', 'field_ground', 'field_text')


# THE CAPTION AS RECORDED (architect 2026-10-05): one catalog entry -> {app role: '#RRGGBB'} for the app's six
# caption roles (src/gui/theme_file.h), read by gen_theme_files.py. Each family names its own title colours, a value
# being a key of the entry's VALUES (its raw keys, or its toolkit rule's computed keys): Windows' ActiveTitle /
# GradientActiveTitle / TitleText and InactiveTitle / GradientInactiveTitle / InactiveTitleText; KDE 3's
# activeBackground / activeForeground and inactiveBackground / inactiveForeground; CDE's colour sets 1 and 2 (the
# active and the inactive window frame, the sets' roles above) under each set's own Motif foreground; GNOME 2's
# metacity title band over gtk:bg[SELECTED] focused and gtk:bg[NORMAL] unfocused (the 1.0 stop of each band's lower
# ramp, the scheme's selected_bg_color and bg_color), the focused title's literal #FFFFFF and the unfocused title's
# blend of fg and bg at 0.45 (draw_ops title_text / title_text_unfocused). NO DERIVATION:
# a gradient end is named only where the entry records one (the 19 Windows entries that carry Gradient*Title; Windows
# 95 Standard, the Plus! themes, KDE 3, CDE and GNOME 2 record none: metacity's band is a ramp of shades of its one
# colour, which the Windows caption painter draws flat), and gen_theme_files.py's flat-caption rule then makes the end
# the start, a flat caption. KDE 3's activeBlend / inactiveBlend are not read: a KDE 3 window decoration's own choice,
# not the scheme's caption.
CAPTION = {
    'windows': (('caption_active', 'ActiveTitle'), ('caption_active_gradient', 'GradientActiveTitle'),
                ('caption_active_text', 'TitleText'), ('caption_inactive', 'InactiveTitle'),
                ('caption_inactive_gradient', 'GradientInactiveTitle'), ('caption_inactive_text', 'InactiveTitleText')),
    'kde3': (('caption_active', 'activeBackground'), ('caption_active_text', 'activeForeground'),
             ('caption_inactive', 'inactiveBackground'), ('caption_inactive_text', 'inactiveForeground')),
    'cde': (('caption_active', 'set1'), ('caption_active_text', 'motif:set1.fg'),
            ('caption_inactive', 'set2'), ('caption_inactive_text', 'motif:set2.fg')),
}
CAPTION['windows-plus'] = CAPTION['windows']
CAPTION['gnome2'] = (('caption_active', 'selected_bg_color'), ('caption_active_text', 'title_text'),
                     ('caption_inactive', 'bg_color'), ('caption_inactive_text', 'metacity:title_unfocused'))


def caption_roles(e):
    """One catalog entry -> {app role: '#RRGGBB'} for the caption (the rule above): each mapped value the entry
    records; a Windows entry without the Gradient keys (Windows 95 Standard, the Plus! themes) names no end."""
    row = CAPTION.get(e['family'])
    if row is None: return {}
    values = dict(e['raw'])
    values.update((e['provenance'].get('rule') or {}).get('computed', {}))
    out = {}
    for role, k in row:
        if k in values: out[role] = values[k].upper()
        elif not role.endswith('_gradient'):
            raise SystemExit(f'roles: {e["key"]} records no {k!r} for {role}')
    return out


def selected_pair(e):
    """An entry's selected pair (LIGHT_ROLES' rule): its own, or a CDE entry's title_active under set 1's foreground."""
    r = e['roles']
    if 'selected_fill' in r: return r['selected_fill'], r['selected_text']
    return r['title_active'], e['provenance']['rule']['computed']['motif:set1.fg']


def light_roles(e):
    """One catalog entry -> {name: '#RRGGBB'} over LIGHT_ROLES (the rule above)."""
    r = e['roles']
    out = {x: r[x] for x in ('ground', 'label', 'bevel_hilight', 'bevel_light', 'bevel_shadow', 'bevel_dkshadow')}
    out['emboss_hilight'] = r['bevel_hilight']
    out['selected_fill'], out['selected_text'] = selected_pair(e)
    out['field_ground'], out['field_text'] = r['field_ground'], r['field_text']
    assert tuple(out) == LIGHT_ROLES, tuple(out)
    return out


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
