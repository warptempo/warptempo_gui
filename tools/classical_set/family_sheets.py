"""tools/classical_set/family_sheets.py: THE FAMILY CONTACT PAGES the architect picks the set from (architect
2026-10-19: he picks FROM FAMILIES, not from whole sheets — the members of a family must share shading and lighting, and
no single sheet gets every family right). Each family is shown as BLOCKS, one per sliced sheet, its members side by
side under the act names; the isolated acts one row per act, every version. THE RULES (architect 2026-10-19 ~22:50): an
icon with an object besides the arrow goes with its object; A GROUP IS ALL OR NOTHING — a sheet whose group has ANY
hidden member shows no block for that group; HIDDEN IS OMITTED, never shown. A SHEET THAT NEVER DREW A MEMBER SHOWS NO
BLOCK EITHER (architect 2026-10-20 ~15:20, "remove the hands without the transport button": the 10-19 sheets lack Transport
Play / Stop, and a Hands block without the conductor's two hands cannot be judged as a family; the dark blank tile that
stood for the missing member until then is retired). Each block is labelled with its tag,
<prefix>-<HHMMSS> (the sheet's stamp), so a pick traces to <sliced folder>/<stamp>/<Act>.png.
Inputs: the sliced folders (slice_sheets.py's <folder>/sliced/, one 14-digit stamp folder per sheet), each with a tag
prefix; hide.json: {"tile": {"<tag>": ["<Act>", ...]}, "front": {"<Act>": ["<tag>", ...]}} — "tile" the members hidden
on a sheet (an input drawing copied verbatim, an isometric drawing), "front" the versions an isolated act shows first.
OUTDATED DRAWINGS ARE HIDDEN BY DRAFT (architect 2026-10-20 ~12:40, "everything that isn't currently in prompt.md"):
REDRAWN names, per act, the prompt draft in which the act's CURRENT drawing was first asked for — a different object,
not a refinement of the same one — and a folder given as <prefix>=<folder>@<draft> (the draft its sheets were drawn
from) hides every act redrawn after that draft, as if hide.json named it on each of its sheets. A refined act (the
podiums' squat base, Undo's even tail, Phase Reset's gilt edge, the post horn) keeps
every batch: an old sheet may already have drawn it the new way (Phase Reset's gilt slate was his pick off an old one).
Revert is REDRAWN at draft 12 (architect 2026-10-20 ~15:15, his own prompt edit: the watch with a gold undo arrow — "most
watches have the wrong arrow", the new sheets drawn for it), so the Watches family shows the draft-12 batches alone.
DRAFT 14 (architect 2026-10-21, "agree to all") changes the set's MATERIALS to Mozart's fortepiano's — old brass for gold,
polished steel for silver, bone for ivory, tan calf for dark leather, judged on ebony cases: A REFINEMENT, NOT A REDRAW,
so no act is hidden for it alone. Its redrawn acts: Older (the arrow clear of the watch, lit from above) and Newer (no
longer drawn: slice_sheets.py turns Older's arrow, so a Newer of any earlier sheet is a different drawing), Left and
Right (the starling's iridescent plumage), and Background Sound, the new act at cell 37 (the umbrella).
DRAFT 15 (architect 2026-10-21 evening: "return to the plain left/right/up/down arrows, but we just [draw] left and mirror
it for right … then we give older/newer their pair back and also we create an up/down for the media player so it doesn't
have to reuse the main page's. we mirror left/right but never up/down since the latter can have diff shading"): Left, Up
and Down are plain brass arrows again, all three drawn and Right the slicer's mirrored Left, judged together as the
Arrows family; Newer is drawn again beside Older; the render player gets its own steel Player Up / Player Down, drawn,
in the Transport keys; Redo is the slicer's mirrored Undo again (a different drawing from draft 14's drawn Redo); Up One
Level leaves the sheet (the act keeps its folder drawing, the UpAFolder -> FolderRow link), so the Folders family no
longer carries it — a member no new sheet draws would blank every new block under the all-or-nothing rule.
DRAFT 16 (architect 2026-10-21 ~19:50: the folders "look a bit too bright and tan … kind of modern"; the tan calf was
period-inspired, not accurate) gives the folder family a period object, THE VIENNESE PORTFOLIO — a stiff pasteboard music
portfolio in bold two-tone marbled paper (burgundy and deep blue) with a mid-brown calf spine and corners and two cloth
ties, as a 1780 copyist kept his parts: Folder the portfolio closed and Save the score slipped into it, both redrawn;
Read-Only a bare brass padlock, no case ("padlock should be just a padlock no case"); Switch Tab the two index tabs
alone, both solid and unfaded ("switch tab should be just the two tabs — make sure they are not faded out"). Its brass is
a dull, dark antique brass, an old door knob's, darker than a trumpet's: A REFINEMENT, NOT A REDRAW, so no other act is
hidden for it.
DRAFT 17 (architect 2026-10-21 ~20:25: "the notebook with tabs was better. the marbled looks interesting and could work
for the tabs") redraws Switch Tab as a closed notebook standing upright in the portfolio's marbled boards with a calf
spine, two index tabs on its fore-edge, the front brass and the other burgundy, both solid and unfaded.
DRAFT 18 (architect 2026-10-21 ~20:45, "go bronze everywhere") changes only the prompt's metal word, brass to bronze: A
REFINEMENT, NOT A REDRAW, so no act is hidden for it (these tools keep saying brass for the look).
DRAFT 19 (architect 2026-10-21 ~21:10) redraws Phase Reset as a bronze clapperboard — the clapper sticks and rim bronze,
the stripes bronze and bone, never black and white ("the clapper should be changed to bronze to stand out") — and removes
the prompt head's two ebony sentences (the near-black button, no object black as a whole: "let's try without the
near-black lines"), the suspect for the checker pattern on two of about ten sheets; the pages still judge on ebony.
DRAFT 20 (architect 2026-10-21 ~21:25, "I think we should undo the bronze conversion, but keep the clapper as brass")
reverts the metal word to brass and the materials line to draft 15's plain old brass (draft 16's door-knob darkening
gone too): A REFINEMENT AGAIN, NO ACT REDRAWN — Phase Reset stays 19, its object changed there and the metal word does
not matter.
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
# The tile ground: the ebony button case the icons stand on since 2026-10-21 (the chrome follows Mozart's fortepiano,
# whose natural keys are ebony) — the mean of the settled ebony tile, a flat color so the pages need no tmp/.
EBONY = (0x29, 0x1D, 0x1B)
BG = (0x30, 0x34, 0x3A)
INK = (0xE8, 0xE8, 0xE8)
DIM = (0xA8, 0xB0, 0xBC)

# Groups (architect 2026-10-19 ~22:50): an icon with an object besides the arrow goes with its object; a group is
# all or nothing — a sheet whose group has ANY hidden member shows no block for that group; hidden is omitted, never shown.
# ValueDrag is in Paper and books since 2026-10-20: the prompt draws it as a sheet of score paper, no arrow and no hand
# (tools/classical_set/prompt.md, cell 31). Switch Tab left Paper and books for the isolated acts 2026-10-21: since draft 16
# it is two index tabs alone, no paper object, and under the all-or-nothing rule one bad tab drawing would blank the
# whole Paper page.
# Drop Marker joined the flag group from Paper and books 2026-10-20: the flag is the marker, so the acts that carry it are
# judged together. Since draft 15 (architect 2026-10-21 evening) Up and Down are plain brass arrows again, judged with Left
# and Right as the Arrows family (Right the slicer's mirrored Left), the starlings and Up / Down's score sheets retired.
FAMILIES = [
    ("arrows", "Arrows", ["Left", "Right", "Up", "Down"]),
    ("flags", "Flags", ["DropMarker", "PreviousMarker", "NextMarker", "JumpToDefiningMarker"]),
    ("undo", "Undo and Redo", ["Undo", "Redo"]),   # Redo the slicer's mirrored Undo since draft 15
    ("watches", "Watches", ["Revert", "HistoryView", "Older", "Newer"]),   # Newer drawn again since draft 15 (draft 14's the turned Older)
    ("transport", "Transport keys", ["Play", "Pause", "PlayerPrevious", "PlayerNext", "RepeatOne",
                                     "PlayerUp", "PlayerDown", "LoadInPlace", "Close"]),
    ("folder", "Folders, read-only and save", ["FolderRow", "ReadOnly", "Save"]),   # Up One Level off the sheet since draft 15
    ("hands", "Hands", ["Cancel", "AddToSelection", "Follow", "TransportPlay", "TransportStop"]),   # the conductor's figures: the cuffs must match
    ("paper", "Paper and books", ["OpenProject", "Source", "Target", "WavRow", "OpenTextEditor",
                                  "DeleteMarkers", "Inherit", "Tooltips", "Render", "SaveCommit",
                                  "SavePull", "ValueDrag"]),
    ("zoom", "Magnifying glasses", ["FullZoomOut", "Center"]),
    ("cards", "Card medallions", ["CardNormal", "CardCritical"]),
]
ISOLATED = ("single", "Isolated acts", [
    "BpmIterations", "Warp", "PhaseReset", "WaveformMagnification", "Settings", "PlayRenders", "DeleteFolder",
    "Disabled", "Flatten", "GridIterations", "BackgroundSound", "SwitchTab"])
# The act's current drawing and the draft that first asked for it (tools/classical_set/prompt.md's git history; drafts 7
# and 8 lived in tmp/ before a51f45d0 — tmp/classical_set/NOTES.md): the starlings (7; the ball out in 10, no speckles
# 79bf5052; retired in 15), Up / Down's score sheets (9; retired in 15), Value Drag's two staves (10), the pigeon and the flag-and-arrow pairs (7; spelled by
# position in 8), the record (8), the bare lens and the gold minus (7), Repeat One without its note (7). The 10-19
# batch (gemini-new, gemini-new2) is draft 6; gemini-new4 the one-commit list of ~09:50, gemini-new5 draft 10, gemini-new6
# draft 11 and gemini-new7 draft 12 (Revert's gold undo arrow beside the watch, his edit of ~14:30); draft 13 (~15:40) the
# magnifying glasses over a sheet of score paper, the minus and the single note seen through the lens on the paper — an
# empty lens shows the sheet's background, which the cut cannot remove, so every icon is opaque; draft 14 (2026-10-21) the
# fortepiano's materials (no act redrawn for them), Older's arrow clear of the watch and Newer derived from it, the
# starlings' iridescent plumage, and the umbrella of Background Sound, the new act at cell 37 where Newer was drawn;
# draft 15 (2026-10-21 evening) the plain brass arrows (Left drawn and Right its mirror, Up and Down drawn), Redo the
# mirrored Undo again (a different drawing from draft 14's drawn Redo), Newer drawn again (not turned), and the render
# player's own steel Player Up / Player Down, new acts. Older stays 14 (the same drawing asked) and Value Drag 10 (the same
# two-staff sheet; its wording lost only a cross-reference); draft 16 (2026-10-21 ~19:50) the Viennese portfolio of Folder
# and Save (marbled pasteboard for the tan calf case), Read-Only's bare padlock (the case gone) and Switch Tab's two index
# tabs alone (the notebook gone), a different object each — the darker antique brass a refinement, no act redrawn for it.
# Draft 17 (2026-10-21 ~20:25) Switch Tab's marbled notebook with its two solid tabs, a different object from draft 16's.
# Draft 18 (2026-10-21 ~20:45) the bronze metal word, a refinement, no act redrawn for it; draft 19 (2026-10-21 ~21:10)
# Phase Reset's bronze clapperboard, a redraw of the black-and-white one (Wav Row's black record kept by his word);
# draft 20 (2026-10-21 ~21:25) brass again, the clapperboard with it, a refinement, no act redrawn for it.
REDRAWN = {"Left": 15, "Right": 15, "Up": 15, "Down": 15, "ValueDrag": 10, "JumpToDefiningMarker": 8,
           "PreviousMarker": 8, "NextMarker": 8, "WavRow": 8, "RepeatOne": 7,
           "Revert": 12, "Center": 13, "FullZoomOut": 13, "Older": 14, "Newer": 15, "Redo": 15,
           "BackgroundSound": 14, "PlayerUp": 15, "PlayerDown": 15,
           "FolderRow": 16, "Save": 16, "ReadOnly": 16, "SwitchTab": 17,
           "PhaseReset": 19}

SHORT = {"JumpToDefiningMarker": "Jump", "PreviousMarker": "PrevMarker", "PlayerPrevious": "PlayerPrev",
         "WaveformMagnification": "Magnification", "OpenTextEditor": "TextEditor",
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
        t = Image.new("RGB", (px, px), EBONY)
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
    """blocks: list of (tag, dir); every block complete. Flow the blocks into rows."""
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
        sub = f"page {pi // per + 1} of {-(-len(blocks) // per)}  ·  tiles at {px} px on ebony  ·  each block one source sheet"
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
        for i, (tag, d) in enumerate(chunk):
            r, c = divmod(i, bpr)
            x0 = margin + c * (bw + gap_b)
            y = y0 + r * rowh
            dr.text((x0, y + 1), tag, font=font(17, True), fill=INK)
            for ai, a in enumerate(acts):
                x = x0 + ai * (px + gap_t)
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
        p, dr = new_page(title, f"tiles at {px} px on ebony  ·  each row one act, every version")
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
        blocks = [(t, d) for t, d in S
                  if not set(ht.get(t, [])) & set(acts) and all((d / f"{a}.png").exists() for a in acts)]
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
