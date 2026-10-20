# tools/classical_set/gimp_fuzzy.py: GIMP'S OWN FUZZY SELECT, DRIVEN HEADLESS (architect 2026-10-19: "goal is fuzzy
# select with thresh 20, then manual fixes as needed. png output with alpha"). GIMP 3.2's python-fu batch interpreter
# runs this file over a JSON job list ($GIMP_FUZZY_JOBS: [{"src", "out", "seeds": [[x, y], ...]}, ...]): per job, load
# the sheet, add alpha, select-contiguous-color at the first seed (REPLACE) and at every further seed (ADD — his
# Shift-click), sample threshold $GIMP_FUZZY_THRESH/255 (default 20, his setting; antialias on, no feather, composite
# criterion, 4-connected — the tool's defaults), clear the selection, save a PNG with alpha (Gimp.file_save:
# file_export is gone in 3.2). THE LAUNCH NEEDS --quit: GIMP 3.2 otherwise stays alive as a background process after
# its batch commands (it says so on stderr); `(gimp-quit 0)` as a batch command does not end it. The stderr noise
# (surfacemap_x, gjs missing, wire_read EOF) is harmless. Inputs and outputs: the job list's paths. slice_sheets.py
# runs it; by hand:
#   GIMP_FUZZY_JOBS=jobs.json gimp-console -i --quit --batch-interpreter=python-fu-eval -b "$(cat tools/classical_set/gimp_fuzzy.py)"
import os, json, gi
gi.require_version('Gimp', '3.0'); gi.require_version('Gegl', '0.4')
from gi.repository import Gimp, Gegl, Gio
THRESH = int(os.environ.get('GIMP_FUZZY_THRESH', '20'))
for job in json.load(open(os.environ['GIMP_FUZZY_JOBS'])):
    img = Gimp.file_load(Gimp.RunMode.NONINTERACTIVE, Gio.File.new_for_path(job['src']))
    layer = img.get_layers()[0]
    if not layer.has_alpha(): layer.add_alpha()
    Gimp.context_push()
    Gimp.context_set_antialias(True); Gimp.context_set_feather(False)
    Gimp.context_set_sample_merged(False); Gimp.context_set_sample_transparent(False)
    Gimp.context_set_sample_criterion(Gimp.SelectCriterion.COMPOSITE)
    Gimp.context_set_sample_threshold(THRESH / 255.0)
    Gimp.context_set_diagonal_neighbors(False)
    for i, (x, y) in enumerate(job['seeds']):
        img.select_contiguous_color(Gimp.ChannelOps.REPLACE if i == 0 else Gimp.ChannelOps.ADD, layer, float(x), float(y))
    Gimp.context_pop()
    layer.edit_clear()
    Gimp.Selection.none(img)
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, img, Gio.File.new_for_path(job["out"]), None)
    print('wrote', job['out'], 'seeds', len(job['seeds']))
    img.delete()
