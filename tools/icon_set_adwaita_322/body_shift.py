"""THE BODY-CENTERING SHIFT WRITTEN INTO EACH FILE (architect 2026-10-19: the launch "used to be very snappy"; ~0.3 s of
measuring at launch was felt on the laptop): build_set.py measures every SVG it writes exactly as the 2026-10-12 table
was measured — rsvg-convert at 240 px, the alpha >= 0.1 bounds, the bbox center put at the viewBox center, in the
SVG's user units, rounded to 0.1 — and writes the result into the file as one attribute on the root tag,
`data-body-shift="dx dy"` (one decimal each, e.g. "2.6 -0.1"), a minimal textual insertion after `<svg`, nothing else
in the file touched. The product reads it from the tag's bytes before parsing (icons.h's THE GLYPHS BODY-CENTERED);
resvg ignores it. It agrees with the product's own resvg measure within 0.1 unit on every file (2026-10-19)."""
import io, subprocess, numpy as np
import xml.etree.ElementTree as ET
from PIL import Image
ATTR='data-body-shift'
def measure(path):
    """(dx, dy) in user units, each rounded to 0.1; (0, 0) for a drawing with no pixel at alpha 0.1."""
    out=subprocess.run(['rsvg-convert','-w','240','-h','240',path],capture_output=True,check=True).stdout
    a=np.asarray(Image.open(io.BytesIO(out)).convert('RGBA'))[...,3]/255.0; ys,xs=np.where(a>=0.1)
    if not len(xs): return 0.0,0.0
    vb=[float(v) for v in ET.parse(path).getroot().get('viewBox','0 0 48 48').replace(',',' ').split()]
    u=vb[2]/240; cx=(xs.min()+xs.max()+1)/2*u+vb[0]; cy=(ys.min()+ys.max()+1)/2*u+vb[1]
    return round(vb[0]+vb[2]/2-cx,1)+0.0, round(vb[1]+vb[3]/2-cy,1)+0.0
def write(path):
    """Measure `path` and insert the attribute after its leading `<svg`; returns (dx, dy)."""
    dx,dy=measure(path)
    b=open(path,'rb').read()
    assert b.startswith(b'<svg ') and ATTR.encode() not in b[:b.index(b'>')],path
    open(path,'wb').write(b'<svg '+f'{ATTR}="{dx:.1f} {dy:.1f}" '.encode()+b[5:])
    return dx,dy
