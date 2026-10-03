#!/usr/bin/env python3
"""Contact sheets: every roster role in roster order, the NATIVE icon or the
COMPOSABLE primitives at 2x nearest-neighbour, on two dark grounds."""
import gzip, json, os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from survey import ROSTER, HERE

GROUND_A = "#232629"   # Breeze Dark window
GROUND_B = "#404040"   # Win95-dark button face (our pick)
TILE_W, TILE_H = 100, 72
CELL_W, CELL_H = 2 * TILE_W + 10, TILE_H + 38
COLS = 9
TMP = os.path.join(HERE, "work", "tiles")
os.makedirs(TMP, exist_ok=True)
FONT = "Adwaita-Sans"
TAGCOL = {"NATIVE": "#1d7a1d", "COMPOSABLE": "#9a6a00", "GAP": "#b01c1c"}


def run(*a, **kw):
    return subprocess.run(a, check=True, capture_output=True, **kw)


def native_png(path, size, out):
    """The icon at its native pixel size (scalable art rasterised at 16)."""
    px = 16 if size in (999, None) else size
    if path.endswith(".svgz"):
        raw = gzip.open(path).read()
        tmp = out + ".svg"
        open(tmp, "wb").write(raw)
        path = tmp
    if path.endswith(".svg"):
        if size in (999, None) or size > 24:
            # SVG from a big/scalable dir: rasterise at 16 (the retro toolbar size)
            px = 16
        run("rsvg-convert", "-w", str(px), "-h", str(px), "-o", out, path)
    else:
        run("magick", path, "-background", "none", out)
    return out


def scaled(path, size, key):
    base = os.path.join(TMP, key + "_1x.png")
    native_png(path, size, base)
    w, h = map(int, run("magick", "identify", "-format", "%w %h", base, text=True).stdout.split())
    out = os.path.join(TMP, key + "_2x.png")
    f = 2 if max(w, h) <= 24 else 1   # 2x nearest-neighbour; >24 px shown 1x
    run("magick", base, "-filter", "point", "-scale", f"{100*f}%", out)
    return out


def cell(sname, idx, enum, row):
    key = f"{sname}_{idx:02d}"
    imgs = []
    label2 = ""
    if row["status"] == "NATIVE":
        imgs.append(scaled(row["path"], row["size"], key + "_n"))
        label2 = f"{row['name']} [{row['size'] if row['size'] != 999 else 'svg'}]"
    elif row["status"] == "COMPOSABLE":
        for j, p in enumerate(row["prims"][:2]):
            imgs.append(scaled(p[2], p[1], f"{key}_c{j}"))
        label2 = "+".join(p[0] for p in row["prims"][:2])
    label2 = label2[:34]
    strip = os.path.join(TMP, key + "_strip.png")
    if imgs:
        run("magick", "-background", "none", *imgs, "-gravity", "center", "+smush", "4", strip)
    out = os.path.join(TMP, key + "_cell.png")
    cmd = ["magick", "-size", f"{CELL_W}x{CELL_H}", "xc:#e6e6e6",
           "(", "-size", f"{TILE_W}x{TILE_H}", f"xc:{GROUND_A}", ")", "-geometry", "+3+3", "-composite",
           "(", "-size", f"{TILE_W}x{TILE_H}", f"xc:{GROUND_B}", ")", "-geometry", f"+{TILE_W+7}+3", "-composite"]
    if imgs:
        w, h = map(int, run("magick", "identify", "-format", "%w %h", strip, text=True).stdout.split())
        ox, oy = (TILE_W - w) // 2, (TILE_H - h) // 2
        cmd += [strip, "-geometry", f"+{3+ox}+{3+oy}", "-composite",
                strip, "-geometry", f"+{TILE_W+7+ox}+{3+oy}", "-composite"]
    else:
        cmd += ["-font", FONT, "-pointsize", "14", "-fill", "#8a8a8a",
                "-annotate", f"+{TILE_W//2-14}+{TILE_H//2+8}", "GAP",
                "-annotate", f"+{TILE_W+7+TILE_W//2-14}+{TILE_H//2+8}", "GAP"]
    tag = {"NATIVE": "N", "COMPOSABLE": "C", "GAP": "G"}[row["status"]]
    cmd += ["-font", FONT, "-pointsize", "12", "-fill", TAGCOL[row["status"]],
            "-annotate", f"+4+{TILE_H+18}", f"{idx+1:02d} {tag}",
            "-fill", "#111111", "-annotate", f"+40+{TILE_H+18}", enum,
            "-pointsize", "10", "-fill", "#444444", "-annotate", f"+4+{TILE_H+32}", label2,
            out]
    run(*cmd)
    return out


def sheet(sname, rows, title):
    cells = [cell(sname, i, e, rows[e]) for i, (e, *_r) in enumerate(ROSTER)]
    rows_png = []
    for r in range(0, len(cells), COLS):
        rp = os.path.join(TMP, f"{sname}_row{r//COLS}.png")
        run("magick", "-background", "#e6e6e6", *cells[r:r+COLS], "+smush", "2", rp)
        rows_png.append(rp)
    c = {"NATIVE": 0, "COMPOSABLE": 0, "GAP": 0}
    for v in rows.values():
        c[v["status"]] += 1
    head = os.path.join(TMP, f"{sname}_head.png")
    run("magick", "-size", f"{COLS*CELL_W + 2*(COLS-1)}x46", "xc:#e6e6e6", "-font", FONT,
        "-pointsize", "20", "-fill", "#111", "-annotate", "+8+26", title,
        "-pointsize", "13", "-fill", "#333", "-annotate", "+8+42",
        f"NATIVE {c['NATIVE']}  COMPOSABLE {c['COMPOSABLE']}  GAP {c['GAP']}   "
        f"left tile {GROUND_A} (Breeze Dark), right tile {GROUND_B} (Win95-dark);  "
        f"<=24 px art at 2x nearest-neighbour, larger art at 1x;  C cells show the primitives",
        head)
    out = os.path.join(HERE, f"sheet_{sname}.png")
    run("magick", "-background", "#e6e6e6", head, *rows_png, "-append", "+repage", out)
    return out, c


TITLES = {
 "chicago95": "Chicago95 (grassmunk) - Icons/Chicago95, GPL-3.0+/MIT, 16 px",
 "se98": "SE98 (nestoris/Win98SE) - SE98/, GPL-2.0, 16 px",
 "bluecurve": "Bluecurve (neeeeow/Bluecurve, Red Hat 8/9 icons from the AI sources) - GPL-3.0, 16 px SVG",
 "crystalsvg": "Trinity Crystal SVG (tdelibs/pics/crystalsvg) - LGPL-2.1 + artwork add-on, 16 px PNG",
 "kdeclassic": "Trinity kdeclassic (tdeartwork/IconThemes/kdeclassic, KDE 1/2 hicolor) - 'completely free', 16 px",
 "locolor": "Trinity locolor (tdeartwork/IconThemes/locolor, 40-colour KDE 1/2) - 'completely free', 16 px, own files only",
 "cde": "CDE (cdesktopenv, cde/programs/icons) - LGPL-2+, .t=16 .m=32 .l=48 XPM",
 "reactos": "ReactOS (comctl32 / mplay32 / shell32 resources) - GPL-2 / Wine LGPL-2.1, 16 px",
 "nautilus1": "Nautilus 1.0.6 / Eazel (icons/) - GPL-2 (Eazel logo excluded), mixed sizes",
}

if __name__ == "__main__":
    cov = json.load(open(os.path.join(HERE, "coverage.json")))
    only = sys.argv[1:] or list(cov)
    for s in only:
        out, c = sheet(s, cov[s], TITLES[s])
        print(out, c)
