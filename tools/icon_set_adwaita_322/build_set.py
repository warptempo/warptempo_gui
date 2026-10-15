"""The Adwaita 3.22.0 icon set by enumerator (architect 2026-10-14): for every row of mapping.tsv, extract.py's
pruned 48-unit file from the row's sheet and slot, its drop shadows removed by shadows.tsv's ids (architect
2026-10-15); THE SET'S ONE OWN FILE, the `own` treatment (architect 2026-10-15): own/<sheet>, the digital metronome,
NOT Adwaita's, COPIED with its dated notice inserted as one XML comment after the root's start tag and every other
byte verbatim (DigitalMetronome and AppIcon both from it, no tarball check); the two treated files, the clock's text to outlines
(text_to_paths.py, VcsDiff) and the derived TabDetachB (preferences-system-windows with the lower-right window copy,
use5281, moved after the front window g5270: element order alone, every coordinate unchanged), each with its dated
changed-file comment at the head; the tarball's four licence texts; and README_table.md, each sheet with its sha256.
One line per seat reports the shadow elements removed and the checks of the faithful drawing (shadows kept):
identical to the sheet cropped to the slot (rgb <= 1.5, alpha <= 3 at 240 px),
or within d <= 3.0 of the theme's shipped 48 PNG (user-trash's mesh pattern sits at 3.0 and its crop at 7.7, because
the crop clips the rim highlight's mask region).
usage: python3 -I tools/icon_set_adwaita_322/build_set.py <tarball-root> <out-dir>"""
import os, sys, hashlib, shutil, tempfile, xml.etree.ElementTree as ET
HERE=os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0,HERE)
import extract as X, text_to_paths as TP
NOTES={'AccessoriesDictionary':'NEW: Source (architect 2026-10-14)','CameraPhoto':'NEW: Phase reset (architect 2026-10-15: the phase snapshot)',
'ChangesAllow':'NEW: Read-Only while unlocked (architect 2026-10-14)','ChangesPrevent':'NEW: Read-Only while locked (architect 2026-10-14)',
'DialogOkApply':'Load in Place (architect 2026-10-14)','DocumentSave':'the floppy retired (architect 2026-10-14)','InputTablet':'NEW: Target (architect 2026-10-14)',
'Merge':'Flatten: the broom (architect 2026-10-14)','TabDetachB':'DERIVED, below',
'DigitalMetronome':'OWN FILE, not Adwaita\'s: Warp (architect 2026-10-15), below','AppIcon':'OWN FILE, DigitalMetronome\'s: the program icon (architect 2026-10-15)'}
TAB_B_NOTICE=(' TabDetachB: adwaita-icon-theme 3.22.0 preferences-system-windows, CHANGED 2026-10-14 (LGPL-3 / CC BY-SA 3.0): the '
 'lower-right window copy use5281 moved after the front window g5270, element order alone, every coordinate unchanged; Switch Tab on tab B. ')
VCS_DIFF_NOTICE=(" VcsDiff: adwaita-icon-theme 3.22.0 document-open-recent (src/fullcolor/clocks.svg), CHANGED 2026-10-14 (LGPL-3 / CC BY-SA 3.0): "
 "its 48 clock-digit text elements replaced by the glyph outlines of the face they name, Liberation Sans Bold, from the repository's "
 "fonts/LiberationSans-Bold.ttf (the product's resvg draws no text); every other element unchanged. ")
OWN_NOTICE=(' DigitalMetronome: NOT from adwaita-icon-theme 3.22.0. The architect\'s own drawing (2026-10-15), made with '
 'Google\'s Gemini at his prompt, Adwaita 3.22\'s phone, accessories-calculator and camera-photo 48 drawings the style '
 'inputs and a product photograph of a Korg MA-1 the subject, a generic digital metronome with WARP in place of the '
 'maker\'s name; cleaned by hand (the ground shadow and every small legend removed, WARP at the speaker grille\'s right '
 'edge, the power button flat). Placed under the set\'s terms, LGPL-3 / CC BY-SA 3.0. Warp, and the program icon (AppIcon). ')
TEXTS=['COPYING','COPYING_LGPL','COPYING_CCBYSA3','AUTHORS']
def copy_own(src,p):
    """the own file, its bytes verbatim but for OWN_NOTICE inserted as one comment after the root's start tag (line 1,
    as tools/app_icon/gen_app_icon.sh requires)."""
    raw=open(src,'rb').read(); assert raw.startswith(b'<svg') and b'--' not in OWN_NOTICE.encode()
    end=raw.index(b'>')+1
    open(p,'wb').write(raw[:end]+b'\n  <!--'+OWN_NOTICE.encode('ascii')+b'-->'+raw[end:])
def sha(p): return hashlib.sha256(open(p,'rb').read()).hexdigest()
def derive_tab_b(p):
    t=ET.parse(p); r=t.getroot(); par={c:p for p in r.iter() for c in p}
    use=next(e for e in r.iter() if e.attrib.get('id')=='use5281'); front=next(e for e in r.iter() if e.attrib.get('id')=='g5270')
    g=par[use]; assert par[front] is g; g.remove(use); g.insert(list(g).index(front)+1,use)
    r.insert(0,ET.Comment(TAB_B_NOTICE))
    t.write(p)
def shadow_table():
    """shadows.tsv: enumerator -> the drop-shadow ids to remove ('-' = none)."""
    t={}
    for line in open(f'{HERE}/shadows.tsv'):
        if line.startswith('#') or not line.strip(): continue
        enum,_src,field=line.rstrip('\n').split('\t')[:3]
        t[enum]=[i for i in field.split() if i!='-']
    return t
def main(root,out):
    os.makedirs(out,exist_ok=True); tmp=tempfile.mkdtemp(); rows=[]; bad=0
    lines=open(f'{HERE}/mapping.tsv').read().splitlines(); shadows=shadow_table()
    for line in lines[1:]:
        enum,name,sheet,x,y,w,h,treat=line.split('\t'); p=f'{out}/{enum}.svg'
        if enum not in shadows: sys.exit(f'{enum}: no row in shadows.tsv')
        if treat=='own':
            assert not shadows[enum],enum; src=f'{HERE}/own/{sheet}'; copy_own(src,p)
            rows.append((enum,name,f'own/{sheet}',None,sha(src),NOTES.get(enum,'')))
            print(f'{enum:24s} {"own/"+sheet:28s} own   copied, the notice inserted'); continue
        slot=(float(x),float(y),float(w),float(h))
        r=X.extract(root,name,sheet,slot,p,tmp,shadows[enum]); note=NOTES.get(enum,'')
        if treat=='text-to-outlines':
            n=TP.convert(p,p,VCS_DIFF_NOTICE); note=f'TEXT TO OUTLINES: {n} text elements (Liberation Sans Bold) replaced by glyph outlines from fonts/LiberationSans-Bold.ttf'
        elif treat=='tab-b': derive_tab_b(p)
        else: assert treat=='-',treat
        ok=(r['max_rgb_diff']<=1.5 and r['max_alpha_diff']<=3) or 0<=r['d_shipped_png']<=3.0
        bad+=not ok
        rows.append((enum,name,sheet,slot,sha(f'{root}/src/fullcolor/{sheet}'),note))
        ns=len(shadows[enum])+len(r['shadow_copies'])
        print(f'{enum:24s} {name:28s} {"ok" if ok else "CHECK"}  shadows -{ns:<2d} crop rgb {r["max_rgb_diff"]:5.1f} alpha {r["max_alpha_diff"]:5.1f}  shipped png d {r["d_shipped_png"]:5.1f}')
    with open(f'{out}/README_table.md','w') as f:
        f.write('| enumerator | 3.22 name | source sheet (src/fullcolor, or own/ beside this tool) | slot (x, y, w, h) | sheet sha256 | note |\n|---|---|---|---|---|---|\n')
        for enum,name,sheet,sl,hs,note in rows:
            f.write(f'| {enum} | {"—" if sl is None else f"`{name}`"} | `{sheet}` | {"—" if sl is None else ", ".join("%g"%v for v in sl)} | `{hs}` | {note} |\n')
    for t in TEXTS: shutil.copyfile(f'{root}/{t}',f'{out}/{t}-adwaita-icon-theme-3.22.0')
    shutil.rmtree(tmp); print(f'{len(rows)} files, {bad} to check')
if __name__=='__main__':
    if len(sys.argv)!=3: sys.exit(__doc__)
    main(sys.argv[1],sys.argv[2])
