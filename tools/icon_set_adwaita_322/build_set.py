"""The Adwaita 3.22.0 drawings by enumerator (architect 2026-10-14) into the flat icon folder, assets/icons/ (architect
2026-10-19: one folder, each file named by its act — icons.h's kIconNames): for every row of mapping.tsv, extract.py's
pruned 48-unit file from the row's sheet and slot, its drop shadows removed by shadows.tsv's ids (architect
2026-10-15) and any other element the architect rules out by removed.tsv's (ValueDrag's arrow, 2026-10-18); the
`text-to-outlines` treatment, HistoryView's (history's clock with the back arrow, again since 2026-10-18): its clock
digits turned into Liberation Sans Bold outlines by text_to_paths.py at the build, with its dated changed-file comment
at the head; the tarball's four licence texts; and README_table.md, each sheet with its sha256. THE TOOL WRITES SVGs AND
THE LICENCE TEXTS ONLY — never a PNG (the architect's own drawings are dropped into the folder by hand, the drop road,
icons.h's head) and never a symlink (a repeated drawing, AppIcon.png, BpmIterations.svg, CardCritical.svg and
UpAFolder.svg, is a link the folder carries, with no row here: mapping.tsv names each canonical file once). Its
output's SVGs are byte-identical to the folder's of the same name (compare with `cmp`; the folder's PNGs and links are
not the tool's).
One line per seat reports the shadow and other elements removed and the checks of the faithful drawing (nothing
removed): identical to the sheet cropped to the slot (rgb <= 1.5, alpha <= 3 at 240 px),
or within d <= 3.0 of the theme's shipped 48 PNG (user-trash's mesh pattern sits at 3.0 and its crop at 7.7, because
the crop clips the rim highlight's mask region).
usage: python3 -I tools/icon_set_adwaita_322/build_set.py <tarball-root> <out-dir>"""
import os, sys, hashlib, shutil, tempfile
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE)
import extract as X, text_to_paths as TP
NOTES={'Source':'NEW: Source (architect 2026-10-14)','PhaseReset':'NEW: Phase reset (architect 2026-10-15: the phase snapshot)',
'ReadOnlyUnlocked':'NEW: Read-Only while unlocked (architect 2026-10-14)','ReadOnlyLocked':'NEW: Read-Only while locked (architect 2026-10-14)',
'LoadInPlace':'Load in Place (architect 2026-10-14)','Save':'the floppy retired (architect 2026-10-14)','Target':'NEW: Target (architect 2026-10-14)',
'Flatten':'Flatten: the broom (architect 2026-10-14)',
'HistoryView':'History: the clock turned back again (architect 2026-10-18, "go back to using the original git clock icon")',
'ValueDrag':'NEW: Value Drag, the pen alone (architect 2026-10-18: "drop the arrows altogether")','SessionHistory':'NEW: Session (architect 2026-10-15: shut off git and stay local)',
'Center':'NEW: Center (architect 2026-10-16: the magnifier finds the playhead)','FullZoomOut':'NEW: Full zoom out (architect 2026-10-16: the view shrunk back)',
'WaveformMagnification':'NEW: Waveform magnification (architect 2026-10-16: the louder waveform)'}
# The notice is the file's own text, written into its head (its first word the file's name when it was written, kept so
# the shipped bytes stand; the enumerator is HistoryView since 2026-10-19).
DOCUMENT_OPEN_RECENT_NOTICE=(" DocumentOpenRecent: adwaita-icon-theme 3.22.0 document-open-recent (src/fullcolor/clocks.svg), CHANGED 2026-10-14 (LGPL-3 / CC BY-SA 3.0): "
 "its 48 clock-digit text elements replaced by the glyph outlines of the face they name, Liberation Sans Bold, from the repository's "
 "fonts/LiberationSans-Bold.ttf (the product's resvg draws no text); its drop shadows removed, as from every seat of the set "
 "(2026-10-15); every other element unchanged. ")
TEXT_NOTICES={'HistoryView':DOCUMENT_OPEN_RECENT_NOTICE}
TEXTS=['COPYING','COPYING_LGPL','COPYING_CCBYSA3','AUTHORS']
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
def id_table(f):
    """shadows.tsv or removed.tsv: enumerator -> the ids to remove ('-' = none)."""
    t={}
    for line in open(f'{HERE}/{f}'):
        if line.startswith('#') or not line.strip(): continue
        enum,_src,field=line.rstrip('\n').split('\t')[:3]
        t[enum]=[i for i in field.split() if i!='-']
    return t
def main(root,out):
    os.makedirs(out,exist_ok=True); tmp=tempfile.mkdtemp(); rows=[]; bad=0
    lines=open(f'{HERE}/mapping.tsv').read().splitlines(); shadows=id_table('shadows.tsv'); removed=id_table('removed.tsv')
    enums={l.split('\t')[0] for l in lines[1:]}
    for e in removed:
        if e not in enums: sys.exit(f'{e}: removed.tsv names no mapping.tsv row')
    for line in lines[1:]:
        enum,name,sheet,x,y,w,h,treat=line.split('\t'); p=f'{out}/{enum}.svg'
        if enum not in shadows: sys.exit(f'{enum}: no row in shadows.tsv')
        slot=(float(x),float(y),float(w),float(h)); gone=removed.get(enum,[])
        r=X.extract(root,name,sheet,slot,p,tmp,shadows[enum]+gone); note=NOTES.get(enum,'')
        if gone: note=(note+'; ' if note else '')+f'REMOVED (removed.tsv): {" ".join(gone)}'
        if treat=='text-to-outlines':
            n=TP.convert(p,p,TEXT_NOTICES[enum])
            note=(note+'; ' if note else '')+f'TEXT TO OUTLINES: {n} text elements (Liberation Sans Bold) replaced by glyph outlines from fonts/LiberationSans-Bold.ttf'
        else: assert treat=='-',treat
        ok=(r['max_rgb_diff']<=1.5 and r['max_alpha_diff']<=3) or 0<=r['d_shipped_png']<=3.0
        bad+=not ok
        rows.append((enum,name,sheet,slot,sha(f'{root}/src/fullcolor/{sheet}'),note))
        ns=len(shadows[enum])+len(r['shadow_copies'])
        print(f'{enum:24s} {name:28s} {"ok" if ok else "CHECK"}  shadows -{ns:<2d} removed -{len(gone)}  crop rgb {r["max_rgb_diff"]:5.1f} alpha {r["max_alpha_diff"]:5.1f}  shipped png d {r["d_shipped_png"]:5.1f}')
    with open(f'{out}/README_table.md','w') as f:
        f.write('| enumerator | 3.22 name | source sheet (src/fullcolor) | slot (x, y, w, h) | sheet sha256 | note |\n|---|---|---|---|---|---|\n')
        for enum,name,sheet,sl,hs,note in rows:
            f.write(f'| {enum} | `{name}` | `{sheet}` | {", ".join("%g"%v for v in sl)} | `{hs}` | {note} |\n')
    for t in TEXTS: shutil.copyfile(f'{root}/{t}',f'{out}/{t}-adwaita-icon-theme-3.22.0')
    shutil.rmtree(tmp); print(f'{len(rows)} files, {bad} to check')
if __name__=='__main__':
    if len(sys.argv)!=3: sys.exit(__doc__)
    main(sys.argv[1],sys.argv[2])
