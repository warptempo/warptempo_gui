"""Replace every <text> of an extracted 3.22 file by the glyph outlines of the face it names (architect 2026-10-14),
from the repository's own font file (fonts/LiberationSans-Bold.ttf for "Liberation Sans" bold): one <path> per text
element carrying the text's own attributes (fill, stroke, opacity ...) less the font properties, the tspan's style
folded in, text-anchor honored, the glyphs advanced by the face's own advance widths plus its GPOS pair kerning. The
product's resvg is built without text and draws none; the outlines are the geometry a text renderer lays down from
the same file, measured 0.26/255 mean against rsvg's own text rendering of document-open-recent. A notice, when
given, is the changed-file comment written at the file's head.
usage: text_to_paths.py <in.svg> <out.svg>"""
import os, sys, re, xml.etree.ElementTree as ET
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
SVGNS='http://www.w3.org/2000/svg'; SV='{%s}'%SVGNS; XL='http://www.w3.org/1999/xlink'
ET.register_namespace('',SVGNS); ET.register_namespace('xlink',XL)
ROOT=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))+'/'
FACES={('Liberation Sans','bold'):ROOT+'fonts/LiberationSans-Bold.ttf',('Liberation Sans','normal'):ROOT+'fonts/LiberationSans-Regular.ttf'}
FONTPROPS=('font-size','font-style','font-variant','font-weight','font-stretch','text-align','text-anchor','font-family','-inkscape-font-specification','line-height','writing-mode','letter-spacing','word-spacing')
def style_dict(s): return dict(kv.split(':',1) for kv in s.split(';') if ':' in kv)
def style_str(d): return ';'.join(f'{k}:{v}' for k,v in d.items())
_fonts={}
def font(path):
    if path not in _fonts:
        f=TTFont(path); kern={}
        gpos=f['GPOS'].table if 'GPOS' in f else None
        if gpos:
            for lk in gpos.LookupList.Lookup:
                for st in lk.SubTable:
                    if lk.LookupType==9: st=st.ExtSubTable
                    if getattr(st,'LookupType',lk.LookupType)!=2: continue
                    if st.Format==1:
                        for g1,ps in zip(st.Coverage.glyphs,st.PairSet):
                            for pv in ps.PairValueRecord:
                                v=pv.Value1; kern.setdefault((g1,pv.SecondGlyph),getattr(v,'XAdvance',0) if v else 0)
                    elif st.Format==2:
                        c1={g:st.ClassDef1.classDefs.get(g,0) for g in st.Coverage.glyphs}
                        c2=st.ClassDef2.classDefs
                        for g1,k1 in c1.items():
                            for g2,k2 in c2.items():
                                v=st.Class1Record[k1].Class2Record[k2].Value1
                                adv=getattr(v,'XAdvance',0) if v else 0
                                if adv: kern.setdefault((g1,g2),adv)
        _fonts[path]=(f,kern)
    return _fonts[path]
def outline(text,size,path):
    f,kern=font(path); cmap=f.getBestCmap(); gs=f.getGlyphSet(); upm=f['head'].unitsPerEm; s=size/upm
    names=[cmap[ord(c)] for c in text]; adv=0; d=[]
    for i,n in enumerate(names):
        pen=SVGPathPen(gs); tp=TransformPen(pen,(s,0,0,-s,adv*s,0)); gs[n].draw(tp); d.append(pen.getCommands())
        adv+=f['hmtx'][n][0]
        if i+1<len(names): adv+=kern.get((n,names[i+1]),0)
    return ' '.join(x for x in d if x), adv*s
def convert(src,dst,notice=None):
    tree=ET.parse(src); root=tree.getroot(); par={c:p for p in root.iter() for c in p}; n=0
    for t in list(root.iter(SV+'text')):
        st=style_dict(t.attrib.get('style','')); spans=list(t.iter(SV+'tspan'))
        txt=''.join(t.itertext()).strip()
        for sp in spans: st.update(style_dict(sp.attrib.get('style','')))
        fam=st.get('font-family','').strip().strip('"\''); wt=st.get('font-weight','normal'); size=float(re.sub('[a-z]*$','',st.get('font-size','16')))
        x=float(spans[0].attrib.get('x',t.attrib.get('x','0')).split()[0]); y=float(spans[0].attrib.get('y',t.attrib.get('y','0')).split()[0])
        d,adv=outline(txt,size,FACES[(fam,'bold' if wt in('bold','700') else 'normal')])
        anchor=st.get('text-anchor','start'); dx=-adv/2 if anchor=='middle' else -adv if anchor=='end' else 0
        p=ET.Element(SV+'path',{'d':d,'transform':f'translate({x+dx:.5f},{y:.5f})'})
        if 'id' in t.attrib: p.attrib['id']=t.attrib['id']
        for k in list(st):
            if k in FONTPROPS: del st[k]
        if st: p.attrib['style']=style_str(st)
        for k,v in t.attrib.items():
            if k not in ('id','style','x','y'): p.attrib[k]=v
        parent=par[t]; parent.insert(list(parent).index(t),p); parent.remove(t); n+=1
    if notice: root.insert(0,ET.Comment(notice))
    tree.write(dst); return n
if __name__=='__main__': print(convert(sys.argv[1],sys.argv[2]),'text elements converted')
