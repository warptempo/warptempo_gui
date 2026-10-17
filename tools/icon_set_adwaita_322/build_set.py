"""The Adwaita 3.22.0 icon set by enumerator (architect 2026-10-14): for every row of mapping.tsv, extract.py's
pruned 48-unit file from the row's sheet and slot, its drop shadows removed by shadows.tsv's ids (architect
2026-10-15) and any other element the architect rules out by removed.tsv's (no row today); THE SET'S ONE OWN FILE, the
`own` treatment (architect 2026-10-16): own/<sheet>, the architect's digital metronome, NOT Adwaita's, COPIED with its
dated notice inserted as one XML comment after the root's start tag and every other byte verbatim (DigitalMetronome
and AppIcon both from it, no tarball check; drawn in the 48 frame with no lettering since 2026-10-17);
the `text-to-outlines` treatment, DocumentOpenRecent's (history's clock with the back arrow, architect 2026-10-16):
its clock digits turned into Liberation Sans Bold outlines by text_to_paths.py at the build; the derived TabDetachB
(preferences-system-windows with the lower-right window copy, use5281, moved after the front window g5270: element
order alone, every coordinate unchanged), each with its dated changed-file comment at the head; the
tarball's four licence texts; and README_table.md, each sheet with its sha256.
One line per seat reports the shadow and other elements removed and the checks of the faithful drawing (nothing
removed): identical to the sheet cropped to the slot (rgb <= 1.5, alpha <= 3 at 240 px),
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
'DigitalMetronome':'OWN FILE, not Adwaita\'s: Warp (architect 2026-10-16), above','AppIcon':'OWN FILE, DigitalMetronome\'s: the program icon (architect 2026-10-16)',
'DocumentOpenRecent':'History: the clock turned back (architect 2026-10-16)','NetworkWiredDisconnected':'NEW: Session (architect 2026-10-15: shut off git and stay local)',
'EditFind':'NEW: Center (architect 2026-10-16: the magnifier finds the playhead)','ViewRestore':'NEW: Full zoom out (architect 2026-10-16: the view shrunk back)',
'AudioVolumeHigh':'NEW: Waveform magnification (architect 2026-10-16: the louder waveform)'}
TAB_B_NOTICE=(' TabDetachB: adwaita-icon-theme 3.22.0 preferences-system-windows, CHANGED 2026-10-14 (LGPL-3 / CC BY-SA 3.0): the '
 'lower-right window copy use5281 moved after the front window g5270, element order alone, every coordinate unchanged; Switch Tab on tab B. ')
DOCUMENT_OPEN_RECENT_NOTICE=(" DocumentOpenRecent: adwaita-icon-theme 3.22.0 document-open-recent (src/fullcolor/clocks.svg), CHANGED 2026-10-14 (LGPL-3 / CC BY-SA 3.0): "
 "its 48 clock-digit text elements replaced by the glyph outlines of the face they name, Liberation Sans Bold, from the repository's "
 "fonts/LiberationSans-Bold.ttf (the product's resvg draws no text); its drop shadows removed, as from every seat of the set "
 "(2026-10-15); every other element unchanged. ")
TEXT_NOTICES={'DocumentOpenRecent':DOCUMENT_OPEN_RECENT_NOTICE}
OWN_NOTICE=(' DigitalMetronome and AppIcon: NOT from adwaita-icon-theme 3.22.0. The architect\'s own drawing (2026-10-17), '
 'a digital metronome drawn in the 48-unit frame at Adwaita 3.22\'s level of detail on the grammar of its pda: one dark '
 'outline on the pixel grid round a case 44 x 34 units with a vertical gradient lighter at the top, a white highlight '
 'line 1 unit inside fading top to bottom at .4 (.3 until 2026-10-17 evening, when the architect tuned the drawing toward '
 'the pda\'s gentle steps on 2x / 4x phone sheets: the highlight lighter, the display\'s gradient a radial; .5 for one '
 'commit, then softened to .4 off a .45 / .40 / .35 / .30 series), a dark line '
 'at .12 inside the foot and right edge, the pda\'s gloss sweep on the case and the display alike; the display a soft '
 'radial gradient from its bottom centre on the pda\'s geometry (the centre 77 % down, the radius 1.08 x the width and '
 '.45 x the height; a diagonal linear gradient until that evening) in a 1-unit dark frame with a black line at .16 just '
 'inside and the recess\'s light line just outside; the speaker grill eight holes by two; the buttons flat discs with a '
 'dark ring, the yellow power button the smallest. The display shows the pendulum at the end of its swing, the Korg MA-1\'s '
 'one tick: a stick 10.5 units long leaning 45 degrees away from the note about a pivot on the window\'s foot, centred '
 'between the window\'s left edge and the note (the swing 45 degrees each way, its whole arc inside the window), cut 2 '
 'units above the foot, beside a quarter note with room right of its stem for the flag a metronome\'s eighth or '
 'sixteenth would carry, never drawn (architect 2026-10-17, the swing\'s second round; the MA-2\'s three-tick persistence '
 'and the pivot 6 units below the window were the morning\'s). Every '
 'tone is the earlier drawing\'s own. History: the metronome of 2026-10-16, drawn with Google\'s Gemini at his prompt '
 'after a Korg MA-1, cleaned on tablet mocks and re-laid on the MA-1\'s proportions in a nested 1024-unit viewport at '
 '120 %, its WARP lettering, its 120 digits and its two sheens removed with this re-render ("too detailed; needs to be '
 'more like GNOME icons"). The drawing is licensed under the GNU LGPL v3, the set\'s licence (architect 2026-10-15). '
 'Warp, and the program icon. ')
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
        if treat=='own':
            assert not shadows[enum] and enum not in removed,enum; src=f'{HERE}/own/{sheet}'; copy_own(src,p)
            rows.append((enum,name,f'own/{sheet}',None,sha(src),NOTES.get(enum,'')))
            print(f'{enum:24s} {"own/"+sheet:28s} own   copied, the notice inserted'); continue
        slot=(float(x),float(y),float(w),float(h)); gone=removed.get(enum,[])
        r=X.extract(root,name,sheet,slot,p,tmp,shadows[enum]+gone); note=NOTES.get(enum,'')
        if gone: note=(note+'; ' if note else '')+f'REMOVED (removed.tsv): {" ".join(gone)}'
        if treat=='text-to-outlines':
            n=TP.convert(p,p,TEXT_NOTICES[enum])
            note=(note+'; ' if note else '')+f'TEXT TO OUTLINES: {n} text elements (Liberation Sans Bold) replaced by glyph outlines from fonts/LiberationSans-Bold.ttf'
        elif treat=='tab-b': derive_tab_b(p)
        else: assert treat=='-',treat
        ok=(r['max_rgb_diff']<=1.5 and r['max_alpha_diff']<=3) or 0<=r['d_shipped_png']<=3.0
        bad+=not ok
        rows.append((enum,name,sheet,slot,sha(f'{root}/src/fullcolor/{sheet}'),note))
        ns=len(shadows[enum])+len(r['shadow_copies'])
        print(f'{enum:24s} {name:28s} {"ok" if ok else "CHECK"}  shadows -{ns:<2d} removed -{len(gone)}  crop rgb {r["max_rgb_diff"]:5.1f} alpha {r["max_alpha_diff"]:5.1f}  shipped png d {r["d_shipped_png"]:5.1f}')
    with open(f'{out}/README_table.md','w') as f:
        f.write('| enumerator | 3.22 name | source sheet (src/fullcolor, or own/ beside this tool) | slot (x, y, w, h) | sheet sha256 | note |\n|---|---|---|---|---|---|\n')
        for enum,name,sheet,sl,hs,note in rows:
            f.write(f'| {enum} | {"—" if sl is None else f"`{name}`"} | `{sheet}` | {"—" if sl is None else ", ".join("%g"%v for v in sl)} | `{hs}` | {note} |\n')
    for t in TEXTS: shutil.copyfile(f'{root}/{t}',f'{out}/{t}-adwaita-icon-theme-3.22.0')
    shutil.rmtree(tmp); print(f'{len(rows)} files, {bad} to check')
if __name__=='__main__':
    if len(sys.argv)!=3: sys.exit(__doc__)
    main(sys.argv[1],sys.argv[2])
