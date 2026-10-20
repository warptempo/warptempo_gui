"""tools/classical_set/family_sheets.py: THE FAMILY CONTACT PAGES the architect picks the set from (architect
2026-10-19: he picks FROM FAMILIES, not from whole sheets — the members of a family must share shading and lighting, and
no single sheet gets every family right). Each family is shown as BLOCKS, one per sliced sheet, its members side by
side under the act names; the isolated acts one row per act, every version. THE RULES (architect 2026-10-19 ~22:50): an
icon with an object besides the arrow goes with its object; A GROUP IS ALL OR NOTHING — a sheet whose group has ANY
hidden member shows no block for that group; HIDDEN IS OMITTED, never shown. A member a sheet never drew (the two
acts the 56-cell sheets before 2026-10-20 lack) is a dark blank tile in its block. Each block is labelled with its tag,
<prefix>-<HHMMSS> (the sheet's stamp), so a pick traces to <sliced folder>/<stamp>/<Act>.png.
Inputs: the sliced folders (slice_sheets.py's <folder>/sliced/, one 14-digit stamp folder per sheet), each with a tag
prefix; hide.json: {"tile": {"<tag>": ["<Act>", ...]}, "front": {"<Act>": ["<tag>", ...]}} — "tile" the members hidden
on a sheet (an input drawing copied verbatim, an isometric drawing), "front" the versions an isolated act shows first.
Outputs: <outdir>/NN_<family>_<page>.png, 1920 x 1080 PNG, judged on the laptop only (never the tablet).
Run: python3 -I tools/classical_set/family_sheets.py <outdir> <hide.json> <prefix>=<sliced folder> ..."""
import sys, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

R = Path(__file__).resolve().parent.parent.parent
FONT = R / "fonts" / "LiberationSans-Regular.ttf"
FONTB = R / "fonts" / "LiberationSans-Bold.ttf"
W, H = 1920, 1080
FACE = (0x62, 0x6C, 0x7B)
BG = (0x30, 0x34, 0x3A)
INK = (0xE8, 0xE8, 0xE8)
DIM = (0xA8, 0xB0, 0xBC)

# Groups (architect 2026-10-19 ~22:50): an icon with an object besides the arrow goes with its object; a group is
# all or nothing — a sheet whose group has ANY hidden member shows no block for that group; hidden is omitted, never shown.
FAMILIES = [
    ("arrows", "Arrows", ["Left", "Right", "Up", "Down", "ValueDrag", "PreviousMarker", "NextMarker",
                          "JumpToDefiningMarker"]),
    ("undo", "Undo and Redo", ["Undo", "Redo"]),
    ("watches", "Watches", ["Revert", "HistoryView", "Older", "Newer"]),
    ("transport", "Transport keys", ["Play", "Pause", "PlayerPrevious", "PlayerNext", "RepeatOne",
                                     "LoadInPlace", "Close"]),
    ("folder", "Folders, read-only and save", ["FolderRow", "UpAFolder", "ReadOnlyLocked", "Save"]),
    ("hands", "Hands", ["Cancel", "AddToSelection", "Follow", "TransportPlay", "TransportStop"]),   # the conductor's figures: the cuffs must match
    ("paper", "Paper and books", ["OpenProject", "Source", "Target", "WavRow", "OpenTextEditor", "DropMarker",
                                  "DeleteMarkers", "Inherit", "SwitchTab", "Tooltips", "Render", "SaveCommit",
                                  "SavePull"]),
    ("zoom", "Magnifying glasses", ["FullZoomOut", "Center"]),
    ("cards", "Card medallions", ["CardNormal", "CardCritical"]),
]
ISOLATED = ("single", "Isolated acts", [
    "BpmIterations", "Warp", "PhaseReset", "WaveformMagnification", "Settings", "PlayRenders", "DeleteFolder",
    "Disabled", "Flatten", "GridIterations"])
SHORT = {"JumpToDefiningMarker": "Jump", "PreviousMarker": "PrevMarker", "PlayerPrevious": "PlayerPrev",
         "WaveformMagnification": "Magnification", "ReadOnlyLocked": "ReadOnly", "OpenTextEditor": "TextEditor",
         "GridIterations": "Grid", "AddToSelection": "AddToSel", "BpmIterations": "BpmIterations"}

def sheets(specs):
    """(tag, stamp folder) for every sheet of every <prefix>=<sliced folder> given."""
    out = []
    for spec in specs:
        n, folder = spec.split("=", 1)
        for d in sorted(Path(folder).iterdir()):
            if d.is_dir() and len(d.name) == 14 and d.name.isdigit():
                out.append((f"{n}-{d.name[8:]}", d))
    return out

_cache = {}
def tile(d, act, px):
    k = (str(d), act, px)
    if k not in _cache:
        im = Image.open(d / f"{act}.png").convert("RGBA").resize((px, px), Image.LANCZOS)
        t = Image.new("RGB", (px, px), FACE)
        t.paste(im, (0, 0), im)
        _cache[k] = t
    return _cache[k]

def font(sz, bold=False):
    return ImageFont.truetype(str(FONTB if bold else FONT), sz)

def new_page(title, sub):
    p = Image.new("RGB", (W, H), BG)
    dr = ImageDraw.Draw(p)
    dr.text((16, 10), title, font=font(26, True), fill=INK)
    dr.text((16 + dr.textlength(title, font=font(26, True)) + 20, 16), sub, font=font(20), fill=DIM)
    return p, dr

def family_pages(key, title, acts, blocks):
    """blocks: list of (tag, dir, set_of_acts_blanked). Flow the blocks into rows."""
    n = len(acts)
    gap_t, gap_b, margin = 4, 22, 16
    # choose tile px: blocks per row bpr, tile px so that the page fits
    best = None
    for px in range(160, 60, -2):
        bw = n * px + (n - 1) * gap_t
        if bw > W - 2 * margin:
            continue
        bpr = (W - 2 * margin + gap_b) // (bw + gap_b)
        rowh = 22 + px + 12
        rows = (H - 56 - 26) // rowh
        if rows < 1:
            continue
        per = bpr * rows
        pages = -(-len(blocks) // per) if blocks else 1
        score = (pages, -px)
        if best is None or score < best[0]:
            best = (score, px, bpr, rows, rowh, bw)
    _, px, bpr, rows, rowh, bw = best
    per = bpr * rows
    pages = []
    for pi in range(0, max(1, len(blocks)), per):
        chunk = blocks[pi:pi + per]
        sub = f"page {pi // per + 1} of {-(-len(blocks) // per)}  ·  tiles at {px} px on the Face  ·  each block one source sheet"
        p, dr = new_page(title, sub)
        y0 = 56
        # column headers once per page above the first row of each block column
        fh = font(14 if px < 110 else 16)
        for bi in range(min(bpr, len(chunk))):
            x0 = margin + bi * (bw + gap_b)
            for ai, a in enumerate(acts):
                lab = SHORT.get(a, a)
                f = fh
                while dr.textlength(lab, font=f) > px + gap_t - 2 and f.size > 9:
                    f = font(f.size - 1)
                cx = x0 + ai * (px + gap_t) + px / 2
                dr.text((cx - dr.textlength(lab, font=f) / 2, y0), lab, font=f, fill=DIM)
        y0 += 26
        for i, (tag, d, blank) in enumerate(chunk):
            r, c = divmod(i, bpr)
            x0 = margin + c * (bw + gap_b)
            y = y0 + r * rowh
            dr.text((x0, y + 1), tag, font=font(17, True), fill=INK)
            for ai, a in enumerate(acts):
                x = x0 + ai * (px + gap_t)
                if a in blank:
                    dr.rectangle([x, y + 22, x + px - 1, y + 22 + px - 1], fill=(0x48, 0x4E, 0x58))
                else:
                    p.paste(tile(d, a, px), (x, y + 22))
        pages.append(p)
    return pages

def single_pages(title, acts, versions):
    """versions: {act: [(tag, dir)]}; one act per run of rows."""
    px, gap, margin = 96, 6, 16
    per_row = (W - 2 * margin + gap) // (px + gap)
    rowh = 18 + px + 8
    pages, cur, y = [], None, None
    def start(sub_n):
        p, dr = new_page(title, f"tiles at {px} px on the Face  ·  each row one act, every version")
        return p, dr, 56
    p = dr = None
    for a in acts:
        vs = versions.get(a, [])
        if not vs:
            continue
        nrows = -(-len(vs) // per_row)
        need = 26 + nrows * rowh
        if p is None or y + need > H - 8:
            if p is not None:
                pages.append(p)
            p, dr, y = start(len(pages))
        dr.text((margin, y), a, font=font(20, True), fill=INK)
        y += 26
        for i, (tag, d) in enumerate(vs):
            r, c = divmod(i, per_row)
            x = margin + c * (px + gap)
            yy = y + r * rowh
            f = font(13)
            dr.text((x + px / 2 - dr.textlength(tag, font=f) / 2, yy), tag, font=f, fill=DIM)
            p.paste(tile(d, a, px), (x, yy + 18))
        y += nrows * rowh + 6
    if p is not None:
        pages.append(p)
    return pages

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    hide = json.loads(Path(sys.argv[2]).read_text())
    ht, front = hide.get("tile", {}), hide.get("front", {})
    S = sheets(sys.argv[3:])
    seq = 0
    def save(pages, name):
        nonlocal seq
        for i, pg in enumerate(pages):
            seq += 1
            pg.save(out / f"{seq:02d}_{name}_{i + 1}.png")
    for key, title, acts in FAMILIES:
        blocks = [(t, d, {a for a in acts if not (d / f"{a}.png").exists()}) for t, d in S if not set(ht.get(t, [])) & set(acts)]
        blocks = [b for b in blocks if len(b[2]) < len(acts)]   # a sheet that drew none of the family has no block
        save(family_pages(key, title, acts, blocks), key)
    key, title, acts = ISOLATED
    vis = {}
    for a in acts:
        vs = [(t, d) for t, d in S if a not in ht.get(t, []) and (d / f"{a}.png").exists()]
        fr = front.get(a, [])
        vs.sort(key=lambda v: (v[0] not in fr, fr.index(v[0]) if v[0] in fr else 0))
        vis[a] = vs
    save(single_pages(title, acts, vis), key)

if __name__ == "__main__":
    main()
