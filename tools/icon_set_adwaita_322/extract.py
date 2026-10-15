"""One seat of the Adwaita 3.22.0 set (architect 2026-10-14) extracted from its source sheet
(adwaita-icon-theme-3.22.0/src/fullcolor/<sheet>.svg) as a standalone 48-unit SVG BY PRUNING THE SHEET IN PLACE:
hidden layers, the editor's metadata and every element that puts no ink in the slot are removed, every remaining
element keeps its ancestors exactly, the drawing is nested once as an <svg> whose viewport IS the slot inside the
48-unit root (a 256-unit slot, an app icon with no 48 drawing, scaled by that viewport as the theme renders its 48
PNG), a mask or filter in user space with no region of its own is given, explicitly, the region the full sheet's
viewport gave it, every DROP SHADOW of the seat's drawing is removed by id (shadows.tsv, architect 2026-10-15: "a drop
shadow is the live glyph's raised fiction; Cool Edit's flat bitmaps never had one"), every <use> of a removed element
with it, and the defs are pruned to those referenced. Nothing is redrawn. The faithful drawing (shadows kept) is
checked against the sheet cropped to the slot (rsvg, 240 px) and against the theme's own shipped 48 PNG; the
shadow-less file is the one written.
The slot rect comes from mapping.tsv, the shadow ids from shadows.tsv and any other removed element's from removed.tsv
(appointment-new's star, architect 2026-10-15), removed the same way; this module is driven by build_set.py."""
import os, re, sys, copy, subprocess, numpy as np, xml.etree.ElementTree as ET, glob
from PIL import Image
SVGNS='http://www.w3.org/2000/svg'; SV='{%s}'%SVGNS; XL='http://www.w3.org/1999/xlink'
ET.register_namespace('',SVGNS); ET.register_namespace('xlink',XL)
DROP={'metadata','namedview','title','desc'}
SKIP={'defs','metadata','namedview','title','desc','style'}
def tag(e): return e.tag.split('}')[-1]
def hidden(e): return 'display:none' in (e.attrib.get('style','')+';display:'+e.attrib.get('display','')).replace(' ','')
def rsvg(src,out,px):
    subprocess.run(['rsvg-convert','-w',str(px),'-h',str(px),src,'-o',out],check=True); return out
def load(p): return np.array(Image.open(p).convert('RGBA')).astype(float)
def dist(a,b):
    """the mean premultiplied RGB and alpha distance, 0..255."""
    return float((np.abs(a[...,:3]*a[...,3:]/255-b[...,:3]*b[...,3:]/255).mean()*3+np.abs(a[...,3]-b[...,3]).mean())/4)
def shipped(root,name):
    """the theme's own installed 48 PNG of the name (Adwaita/48x48/<context>/<name>.png), or None."""
    f=sorted(glob.glob(f'{root}/Adwaita/48x48/*/{name}.png')); return f[0] if f else None
def slot_svg(sheet,slot,out):
    """the sheet cropped to the slot: the root's width, height and viewBox replaced, nothing else touched."""
    x,y,w,h=slot; raw=open(sheet,'rb').read()
    m=re.search(rb'<svg\b[^>]*>',raw,re.S); t=m.group(0)
    t=re.sub(rb'\swidth="[^"]*"',b'',t); t=re.sub(rb'\sheight="[^"]*"',b'',t); t=re.sub(rb'\sviewBox="[^"]*"',b'',t)
    t=t[:-1]+(' width="%g" height="%g" viewBox="%g %g %g %g">'%(w,h,x,y,w,h)).encode()
    open(out,'wb').write(raw[:m.start()]+t+raw[m.end():]); return out
def refs(el):
    s=ET.tostring(el,encoding='unicode')
    return set(re.findall(r'url\(#([^)"\']+)\)',s))|set(re.findall(r'href="#([^"]+)"',s))
def ink_nonempty(tree,el,vbox,tmp):
    """the element rendered alone (every sibling along its ancestry hidden) in the slot's box."""
    t=copy.deepcopy(tree); r=t.getroot(); r.attrib['viewBox']='%g %g %g %g'%vbox; r.attrib['width']=r.attrib['height']='48'
    par={c:p for p in r.iter() for c in p}
    tgt=next(e for e in r.iter() if e.attrib.get('id')==el.attrib['id'])
    keep=set(); a=tgt
    while a is not None: keep.add(a); a=par.get(a)
    sub=set(tgt.iter())
    byid={e.attrib.get('id'):e for e in r.iter()}; todo=refs(tgt)       # what the element references stays visible
    while todo:
        i=todo.pop(); e=byid.get(i)
        if e is None or e in sub: continue
        sub|=set(e.iter()); todo|=refs(e)
        a=par.get(e)
        while a is not None: keep.add(a); a=par.get(a)
    for e in list(r.iter()):
        if e in keep or e in sub or tag(e) in SKIP: continue
        if par.get(e) in keep:
            st=e.attrib.get('style',''); e.attrib['style']=(st+';' if st else '')+'display:none'
    p=f'{tmp}/_cand.svg'; t.write(p); rsvg(p,p[:-4]+'.png',240)
    return np.array(Image.open(p[:-4]+'.png').convert('RGBA'))[...,3].max()>=8
def prune_shadows(trees,ids,label):
    """remove the listed elements (each must exist) from the drawing and its defs, then every <use> that references a
    removed element (a copy of a shadow is the same shadow), repeated until none is left; exit if any element left
    still references (href or url(#...)) a removed one. Returns the removed copies' ids."""
    par={c:p for t in trees for p in t.iter() for c in p}
    byid={e.attrib['id']:e for t in trees for e in t.iter() if 'id' in e.attrib}
    for i in ids:
        if i not in byid: sys.exit(f'{label}: no element with id {i}')
    gone=set(); todo=list(ids); copies=[]
    while todo:
        for i in todo:
            e=byid[i]
            if e in par and e in list(par[e]): par[e].remove(e)
            gone|={d.attrib['id'] for d in e.iter() if 'id' in d.attrib}
        todo=[u.attrib['id'] for t in trees for u in t.iter(SV+'use')
              if (u.attrib.get('{%s}href'%XL) or u.attrib.get('href') or '#')[1:] in gone and u.attrib['id'] not in gone]
        copies+=todo
    for t in trees:
        for e in t.iter():
            for v in e.attrib.values():
                for a,b in re.findall(r'^#(\S+)$|url\(#([^)]+)\)',v):
                    if (a or b) in gone: sys.exit(f'{label}: {e.attrib.get("id")} still references removed {a or b}')
    return copies
def serialise(icon,defs_all):
    """steps 4 and 5 and the file's bytes: the 48-unit root over the pruned defs and the drawing."""
    # 4. the defs: every referenced one, transitively, in the sheet's order
    D={}
    for d in defs_all:
        for e in d: D[e.attrib['id']]=e
    for e in icon.iter():                       # gradients etc. defined inside the drawing count too
        if tag(e) in ('linearGradient','radialGradient','filter','clipPath','mask','pattern','symbol','marker'): D.setdefault(e.attrib['id'],e)
    need=set(); todo=refs(icon)
    while todo:
        i=todo.pop()
        if i in need or i not in D: continue
        need.add(i); todo|=refs(D[i])
    defs=ET.Element(SV+'defs')
    for d in defs_all:
        for e in d:
            if e.attrib['id'] in need: defs.append(e)
    # 5. the editor's attributes, the anonymous ids
    for e in list(defs.iter())+list(icon.iter()):
        for a in list(e.attrib):
            if a.startswith('{') and not a.startswith('{'+XL+'}'): del e.attrib[a]
        if e.attrib.get('id','').startswith('anon'): del e.attrib['id']
    root=ET.Element(SV+'svg',{'viewBox':'0 0 48 48','width':'48','height':'48'})
    root.append(defs); root.append(icon)
    ET.indent(root,space=' '); return ET.tostring(root,encoding='unicode')+'\n'
def extract(tarball_root,name,sheet_file,slot,out,tmp,shadow_ids=()):
    """sheet_file the basename under src/fullcolor/, slot (x, y, w, h) in the sheet's user units, shadow_ids the
    seat's drop-shadow elements (shadows.tsv); writes out and returns the record of its checks."""
    sheet=f'{tarball_root}/src/fullcolor/{sheet_file}'; x,y,w,h=slot
    tree=ET.parse(sheet); root=tree.getroot(); sheet_root_attrs=dict(root.attrib)
    for e in root.iter():
        if 'id' not in e.attrib: e.attrib['id']='anon%d'%id(e)
    # 0. a <use>'s target lives on in a defs block when its own place in the sheet goes (a template in a hidden
    #    layer or in another slot's drawing renders through the use exactly as a defs entry would); a target that is
    #    part of the slot's drawing stays where it is
    used=refs(root)        # every id referenced by a use, a paint server, a filter, a clip or a mask, anywhere
    tpl=ET.Element(SV+'defs')
    def discard(parent,c):
        """remove c; any referenced element inside it (or c itself) moves to the template defs first."""
        par={cc:pp for pp in c.iter() for cc in pp}
        for e in list(c.iter()):
            if e.attrib.get('id') in used:
                if e is c: parent.remove(c); tpl.append(c); return
                par[e].remove(e); tpl.append(e)
        parent.remove(c)
    # 1. hidden elements and the editor's own
    for p in list(root.iter()):
        for c in list(p):
            if tag(c) in DROP or (c.tag.startswith('{') and not c.tag.startswith(SV)): p.remove(c)
            elif tag(c)!='defs' and hidden(c): discard(p,c)
    # 2. the candidates: a layer's children (a layer = a <g> directly under the root), the root's other children
    removed=[]; kept=[]
    for c in list(root):
        if tag(c)=='defs': continue
        if tag(c)=='g':
            for cc in list(c):
                if tag(cc)=='defs': continue
                if ink_nonempty(tree,cc,(x,y,w,h),tmp): kept.append(cc.attrib['id'])
                else: discard(c,cc); removed.append(cc.attrib['id'])
            if len(c)==0: root.remove(c)
        elif not ink_nonempty(tree,c,(x,y,w,h),tmp): discard(root,c); removed.append(c.attrib['id'])
        else: kept.append(c.attrib['id'])
    if len(tpl): root.insert(0,tpl)
    # 3. the wrap: a nested <svg> whose viewport IS the slot (so anything the sheet states relative to the viewport
    #    resolves as the theme's own render resolves it), scaled to the 48-unit root by its width and height
    icon=ET.Element(SV+'svg',{'id':'icon','x':'0','y':'0','width':'48','height':'48','viewBox':'%g %g %g %g'%(x,y,w,h)}); defs_all=[]
    for c in list(root):
        root.remove(c)
        if tag(c)=='defs': defs_all.append(c)
        else: icon.append(c)
    # 3b. a mask or filter in user space with no region of its own takes the region the theme's render gave it:
    #     -10 % / 120 % of the SHEET's viewport (the sheet's width and height), stated explicitly — without it the
    #     3.22 edit-undo / edit-redo masks clip the arrow to nothing in every current renderer
    vbs=[float(v) for v in sheet_root_attrs.get('viewBox','').replace(',',' ').split()] if sheet_root_attrs.get('viewBox') else None
    SW=float(re.sub('[a-z]*$','',sheet_root_attrs['width'])) if 'width' in sheet_root_attrs else vbs[2]
    SH=float(re.sub('[a-z]*$','',sheet_root_attrs['height'])) if 'height' in sheet_root_attrs else vbs[3]
    SX,SY=(vbs[0],vbs[1]) if vbs else (0.0,0.0)
    for e in [x for d in defs_all for x in d.iter()]+list(icon.iter()):
        t=tag(e)
        # (a pattern's width and height are its tile, never a region: left alone)
        if t in ('mask','filter') and e.attrib.get({'mask':'maskUnits','filter':'filterUnits'}[t],'objectBoundingBox')=='userSpaceOnUse' and not any(k in e.attrib for k in ('x','y','width','height')):
            e.attrib.update({'x':'%g'%(SX-0.1*SW),'y':'%g'%(SY-0.1*SH),'width':'%g'%(1.2*SW),'height':'%g'%(1.2*SH)})
    # 4-5 twice: the faithful drawing for the checks, then the drop shadows removed for the file
    faithful=f'{tmp}/_faithful.svg'
    open(faithful,'w').write(serialise(copy.deepcopy(icon),copy.deepcopy(defs_all)))
    copies=prune_shadows([icon]+defs_all,list(shadow_ids),f'{name} ({sheet_file})')
    open(out,'w').write(serialise(icon,defs_all))
    # the checks: against the sheet cropped to the slot, and against the theme's shipped 48 PNG
    rsvg(faithful,f'{tmp}/_x1.png',240); a=load(f'{tmp}/_x1.png')
    rsvg(slot_svg(sheet,slot,f'{tmp}/_crop.svg'),f'{tmp}/_x2.png',240); b=load(f'{tmp}/_x2.png')
    pa=a[...,:3]*a[...,3:]/255; pb=b[...,:3]*b[...,3:]/255
    sp=shipped(tarball_root,name); dpng=dist(load(rsvg(faithful,f'{tmp}/_x3.png',48)),load(sp)) if sp else -1
    return {'shadow_copies':copies,'d_shipped_png':dpng,'sheet':f'src/fullcolor/{sheet_file}','slot':[x,y,w,h],'kept':kept,
            'max_rgb_diff':float(np.abs(pa-pb).max()),'max_alpha_diff':float(np.abs(a[...,3]-b[...,3]).max()),'bytes':os.path.getsize(out)}
