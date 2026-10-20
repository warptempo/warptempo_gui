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
OUTDATED DRAWINGS ARE HIDDEN BY DRAFT (architect 2026-10-20 ~12:40, "everything that isn't currently in prompt.md"):
REDRAWN names, per act, the prompt draft in which the act's CURRENT drawing was first asked for — a different object,
not a refinement of the same one — and a folder given as <prefix>=<folder>@<draft> (the draft its sheets were drawn
from) hides every act redrawn after that draft, as if hide.json named it on each of its sheets. A refined act (the
podiums' squat base, Revert's arrow touching the watch, Undo's even tail, Phase Reset's gilt edge, the post horn) keeps
every batch: an old sheet may already have drawn it the new way (Phase Reset's gilt slate was his pick off an old one).
EVERY TILE IS ONE SIZE, TILE_PX, on every page (architect 2026-10-20 ~13:50, "a uniform size, about the size of the
flags, and use more pages as needed": the families are compared against each other, so a tile must not shrink with the
family's width; a wide family flows onto more pages instead).
Outputs: <outdir>/NN_<family>_<page>.png, 1920 x 1080 PNG, judged on the laptop only (never the tablet).
Run: python3 -I tools/classical_set/family_sheets.py <outdir> <hide.json> <prefix>=<sliced folder>[@<draft>] ..."""
import sys, json
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

R = Path(__file__).resolve().parent.parent.parent
FONT = R / "fonts" / "LiberationSans-Regular.ttf"
FONTB = R / "fonts" / "LiberationSans-Bold.ttf"
W, H = 1920, 1080
TILE_PX = 110   # the Flags page's tile of 2026-10-20 ~13:40, his "about the size of the flags"
FACE = (0x62, 0x6C, 0x7B)
BG = (0x30, 0x34, 0x3A)
INK = (0xE8, 0xE8, 0xE8)
DIM = (0xA8, 0xB0, 0xBC)

# Groups (architect 2026-10-19 ~22:50): an icon with an object besides the arrow goes with its object; a group is
# all or nothing — a sheet whose group has ANY hidden member shows no block for that group; hidden is omitted, never shown.
# Up, Down and ValueDrag moved from Arrows to Paper and books 2026-10-20: the prompt draws them as a sheet of score paper
# whose notes crowd or open across the page, no arrow and no hand (tools/classical_set/prompt.md, cells 31, 40, 41).
# The starlings (Left, Right) are judged apart from the pigeon and the flags since 2026-10-20, and Drop Marker joined the
# flag group from Paper and books: the flag is the marker, so the acts that carry it are judged together.
FAMILIES = [
    ("starlings", "Starlings", ["Left", "Right"]),
    ("flags", "Flags", ["DropMarker", "PreviousMarker", "NextMarker", "JumpToDefiningMarker"]),
    ("undo", "Undo and Redo", ["Undo", "Redo"]),
    ("watches", "Watches", ["Revert", "HistoryView", "Older", "Newer"]),
    ("transport", "Transport keys", ["Play", "Pause", "PlayerPrevious", "PlayerNext", "RepeatOne",
                                     "LoadInPlace", "Close"]),
    ("folder", "Folders, read-only and save", ["FolderRow", "UpAFolder", "ReadOnlyLocked", "Save"]),
    ("hands", "Hands", ["Cancel", "AddToSelection", "Follow", "TransportPlay", "TransportStop"]),   # the conductor's figures: the cuffs must match
    ("paper", "Paper and books", ["OpenProject", "Source", "Target", "WavRow", "OpenTextEditor",
                                  "DeleteMarkers", "Inherit", "SwitchTab", "Tooltips", "Render", "SaveCommit",
                                  "SavePull", "Up", "Down", "ValueDrag"]),
    ("zoom", "Magnifying glasses", ["FullZoomOut", "Center"]),
    ("cards", "Card medallions", ["CardNormal", "CardCritical"]),
]
ISOLATED = ("single", "Isolated acts", [
    "BpmIterations", "Warp", "PhaseReset", "WaveformMagnification", "Settings", "PlayRenders", "DeleteFolder",
    "Disabled", "Flatten", "GridIterations"])
# The act's current drawing and the draft that first asked for it (tools/classical_set/prompt.md's git history; drafts 7
# and 8 lived in tmp/ before a51f45d0 — tmp/classical_set/NOTES.md): the starlings (7; the ball out in 10, no speckles
# 79bf5052), the score sheets (9; Value Drag's two staves 10), the pigeon and the flag-and-arrow pairs (7; spelled by
# position in 8), the record (8), the bare lens and the gold minus (7), Repeat One without its note (7). The 10-19
# batch (gemini-new, gemini-new2) is draft 6; gemini-new4 the one-commit list of ~09:50 and gemini-new5 draft 10.
REDRAWN = {"Left": 10, "Right": 10, "Up": 9, "Down": 9, "ValueDrag": 10, "JumpToDefiningMarker": 8,
           "PreviousMarker": 8, "NextMarker": 8, "WavRow": 8, "Center": 7, "FullZoomOut": 7, "RepeatOne": 7}

SHORT = {"JumpToDefiningMarker": "Jump", "PreviousMarker": "PrevMarker", "PlayerPrevious": "PlayerPrev",
         "WaveformMagnification": "Magnification", "ReadOnlyLocked": "ReadOnly", "OpenTextEditor": "TextEditor",
         "GridIterations": "Grid", "AddToSelection": "AddToSel", "BpmIterations": "BpmIterations"}

def sheets(specs):
    """(tag, stamp folder, outdated acts) for every sheet of every <prefix>=<sliced folder>[@<draft>] given; the
    outdated acts those REDRAWN after the folder's draft (none when no draft is given)."""
    out = []
    for spec in specs:
        n, folder = spec.split("=", 1)
        folder, _, draft = folder.partition("@")
        old = {a for a, d in REDRAWN.items() if draft and int(draft) < d}
        for d in sorted(Path(folder).iterdir()):
            if d.is_dir() and len(d.name) == 14 and d.name.isdigit():
                out.append((f"{n}-{d.name[8:]}", d, old))
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
    px = TILE_PX
    bw = n * px + (n - 1) * gap_t
    assert bw <= W - 2 * margin, f"{title}: {n} tiles at {px} px do not fit a page"
    bpr = (W - 2 * margin + gap_b) // (bw + gap_b)   # blocks per row
    rowh = 22 + px + 12
    rows = (H - 56 - 26) // rowh
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
    """versions: {act: [(tag, dir)]}; one act per run of rows, the run continuing on the next page when it does not fit
    (the act's name repeated with "continued"), so no page is left mostly empty."""
    px, gap, margin = TILE_PX, 6, 16
    per_row = (W - 2 * margin + gap) // (px + gap)
    rowh = 18 + px + 8
    pages, p, dr, y = [], None, None, 0
    def start():
        nonlocal p, dr, y
        if p is not None:
            pages.append(p)
        p, dr = new_page(title, f"tiles at {px} px on the Face  ·  each row one act, every version")
        y = 56
    for a in acts:
        vs = versions.get(a, [])
        if not vs:
            continue
        rows = [vs[i:i + per_row] for i in range(0, len(vs), per_row)]
        for ri, row in enumerate(rows):
            if p is None or y + 26 + rowh > H - 8:
                start()
                dr.text((margin, y), a + (" (continued)" if ri else ""), font=font(20, True), fill=INK)
                y += 26
            elif ri == 0:
                dr.text((margin, y), a, font=font(20, True), fill=INK)
                y += 26
            for c, (tag, d) in enumerate(row):
                x = margin + c * (px + gap)
                f = font(13)
                dr.text((x + px / 2 - dr.textlength(tag, font=f) / 2, y), tag, font=f, fill=DIM)
                p.paste(tile(d, a, px), (x, y + 18))
            y += rowh
        y += 6
    if p is not None:
        pages.append(p)
    return pages

def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    out = Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)
    hide = json.loads(Path(sys.argv[2]).read_text())
    ht, front = hide.get("tile", {}), hide.get("front", {})
    S = [(t, d) for t, d, old in sheets(sys.argv[3:])]
    for t, d, old in sheets(sys.argv[3:]):
        ht[t] = sorted(set(ht.get(t, [])) | old)
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
