#!/usr/bin/env python3
# tools/theme_catalog/preset_keys.py — THE PRESETS' DEVICE-CONFIG LINES (architect 2026-10-04): for each preset the
# colour picker saved (tools/palette/picker/presets/presets.json, the repository's copy of the tablet's file), PRINTS
# the lines a device config takes to show that look, ready to paste: `theme=warptempo-preset-<n>` and
# `theme_level=light` first (the preset's chrome, the catalog entry build.py generates from the same preset), then the
# program colour keys its other elements map to, in the writer's order (src/gui/device_config.h kProgramColourKeys):
#   ink -> waveform_ink, canvas -> waveform_canvas, and waveform_outline the picker theme's "auto" rule over the two
#   (the 50 % linear-light blend of the ink over the canvas), computed ONCE here by render.py's own resolution;
#   unselected_flag -> flag_face with flag_label, selected_flag -> flag_face_selected with flag_label_selected (each
#   label the white the picker shows fixed); playhead_head and playhead_stem -> the same-named keys.
# A key whose element the preset does not record is not printed (the outline needs both the ink and the canvas).
# The preset's chrome and its LABEL (the picker's Label element, architect 2026-10-04) are theme roles, not device
# keys: build.py writes them into the preset's catalog entry (the label and the field text), so `theme=` carries them
# and nothing is printed for the label.
# With no argument every preset is printed, each block led by a `# Preset <n> (saved ...)` line and the blocks a blank
# line apart -- the device config admits neither (device_config.h: no blank or comment lines), so paste a block's key
# lines only; with a preset number, that preset's lines alone, nothing else (pipeable).
# IT WRITES NOTHING: no device config is touched; the architect pastes the lines where he wants the look.
#
# Before printing, each preset is resolved through render.py's own Theme over the picker's theme
# (tools/palette/themes/picker.json with the preset's colours applied, render.picker_apply), so every printed value is
# the colour the picker painted: the fixed labels below must equal that theme's, and the catalog entry's LIGHT row
# (levels.level_roles, what gen_theme_table.py writes into src/gui/theme_table.h) must equal its chrome roles. A
# mismatch is a hard fail naming what moved: a stale catalog (re-run build.py and gen_theme_table.py) or a changed
# picker theme (the presets' fixed roles then want a ruling).
#
#   python3 tools/theme_catalog/preset_keys.py [n]
import json, os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
PALETTE = os.path.join(REPO, 'tools', 'palette')
sys.path.insert(0, HERE)
sys.path.insert(0, PALETTE)
import levels                                                          # noqa: E402
import render                                                          # noqa: E402
import common as C                                                     # noqa: E402
from build import PRESETS, PRESET_PREFIX                               # noqa: E402

PICKER_THEME = os.path.join(PALETTE, 'themes', 'picker.json')
# (preset element, device-config key), the writer's order; the outline between them is the rule's
ELEMENT_KEYS = (('ink', 'waveform_ink'), ('canvas', 'waveform_canvas'), ('outline', 'waveform_outline'),
                ('unselected_flag', 'flag_face'), ('flag_label', 'flag_label'),
                ('selected_flag', 'flag_face_selected'), ('flag_label_sel', 'flag_label_selected'),
                ('playhead_head', 'playhead_head'), ('playhead_stem', 'playhead_stem'))
# the labels the picker shows fixed (2026-10-04, picker.json: both white), each printed with its flag face
FIXED_LABELS = {'flag_label': ('unselected_flag', '#FFFFFF'), 'flag_label_sel': ('selected_flag', '#FFFFFF')}


def lines(preset, picker, catalog):
    """One preset -> its device-config lines (the head is the rule)."""
    n, cols = preset['number'], preset['colours']
    th = render.Theme(PICKER_THEME, data=render.picker_apply(picker, picker['picker']['elements'], cols))
    key = f'{PRESET_PREFIX}{n}'
    if key not in catalog: raise SystemExit(f'preset_keys: {key} is not in the catalog; run build.py and gen_theme_table.py')
    light = levels.level_roles(catalog[key], 'light')
    painted = {r: C.hexs(th.get(r)) for r in levels.ROLES}
    if light != painted:
        raise SystemExit(f'preset_keys: {key}\'s light row differs from the chrome the picker paints for Preset {n} '
                         f'({ {r: (light[r], painted[r]) for r in light if light[r] != painted[r]} }); re-run build.py '
                         f'and gen_theme_table.py, or rule on the picker theme\'s change')
    for role, (_, hexv) in FIXED_LABELS.items():
        if C.hexs(th.get(role)) != hexv:
            raise SystemExit(f'preset_keys: the picker now shows {role} {C.hexs(th.get(role))}, not {hexv}; a ruling')
    out = [f'theme={key}', 'theme_level=light']
    for el, cfg in ELEMENT_KEYS:
        if el == 'outline':
            if 'ink' not in cols or 'canvas' not in cols: continue
            assert picker['colours']['outline'] == 'auto'
            out.append(f'{cfg}={C.hexs(th.get("outline"))}')
        elif el in FIXED_LABELS:
            if FIXED_LABELS[el][0] in cols: out.append(f'{cfg}={FIXED_LABELS[el][1]}')
        elif el in cols:
            out.append(f'{cfg}={C.hexs(th.get(element_role(picker, el)))}')
    return out


def element_role(picker, el):
    """The picker element's colour role (picker.json's elements)."""
    return next(e['role'] for e in picker['picker']['elements'] if e['key'] == el)


def main():
    picker = json.load(open(PICKER_THEME))
    catalog = {e['key']: e for e in levels.entries()}
    ps = sorted(json.load(open(PRESETS))['presets'], key=lambda p: p['number'])
    if sys.argv[1:]:
        want = sys.argv[1:]
        p = [q for q in ps if len(want) == 1 and str(q['number']) == want[0]]
        if not p: raise SystemExit(f'usage: python3 tools/theme_catalog/preset_keys.py [n], n one of {[q["number"] for q in ps]}')
        print('\n'.join(lines(p[0], picker, catalog)))
        return
    for i, p in enumerate(ps):
        if i: print()
        print(f'# Preset {p["number"]} (saved {p["saved"]})')
        print('\n'.join(lines(p, picker, catalog)))


if __name__ == '__main__':
    main()
