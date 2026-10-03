#!/usr/bin/env python3
"""Dark-ground legibility: for each NATIVE icon's 1x raster, the share of its
opaque pixels whose WCAG contrast against the ground is under 2.0:1 (they sink)."""
import json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from survey import ROSTER, HERE
from sheets import native_png
T = os.path.join(HERE, "work", "contrast"); os.makedirs(T, exist_ok=True)

def lin(c):
    c /= 255.0
    return c/12.92 if c <= 0.04045 else ((c+0.055)/1.055)**2.4
def L(r, g, b): return 0.2126*lin(r)+0.7152*lin(g)+0.0722*lin(b)
def cr(a, b):
    hi, lo = max(a, b), min(a, b); return (hi+0.05)/(lo+0.05)
GA = L(0x23, 0x26, 0x29); GB = L(0x40, 0x40, 0x40)

def pixels(png):
    raw = subprocess.run(["magick", png, "-background", "none", "-alpha", "on", "-depth", "8", "rgba:-"],
                         capture_output=True, check=True).stdout
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i:i+4]
        if a >= 128: yield r, g, b

cov = json.load(open(os.path.join(HERE, "coverage.json")))
out = {}
for s, rows in cov.items():
    res = {}
    for e, *_ in ROSTER:
        r = rows[e]
        if r["status"] != "NATIVE": continue
        png = native_png(r["path"], r["size"], os.path.join(T, f"{s}_{e}.png"))
        px = list(pixels(png))
        if not px: continue
        la = sum(1 for p in px if cr(L(*p), GA) < 2.0) / len(px)
        lb = sum(1 for p in px if cr(L(*p), GB) < 2.0) / len(px)
        res[e] = (round(la, 2), round(lb, 2))
    sinkA = [e for e, v in res.items() if v[0] > 0.5]
    sinkB = [e for e, v in res.items() if v[1] > 0.5]
    out[s] = {"measured": len(res), "sink_A": sinkA, "sink_B": sinkB, "detail": res}
    print(f"{s:11s} native={len(res):2d} sink@#232629={len(sinkA):2d} sink@#404040={len(sinkB):2d}  {sinkA}")
json.dump(out, open(os.path.join(HERE, "contrast.json"), "w"), indent=1)
