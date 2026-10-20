"""tools/classical_set/act_strip.py: ONE ACT ACROSS EVERY SLICED SHEET, a close-up strip for judging a single act's
versions side by side (eleven to a row on the Face gray, each tile labelled with its tag <prefix>-<HHMMSS>, as
family_sheets.py labels its blocks; a sheet that never drew the act is left out).
Inputs: the act's enumerator name and the sliced folders (slice_sheets.py's <folder>/sliced/), each with a tag prefix.
Output: one PNG at the path given (keep it under tmp/).
Run: python3 -I tools/classical_set/act_strip.py <out.png> <Act> [--px N] <prefix>=<sliced folder> ...   (N default 150)"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

R = Path(__file__).resolve().parent.parent.parent
F = ImageFont.truetype(str(R / "fonts" / "LiberationSans-Bold.ttf"), 16)


def main():
    a = sys.argv[1:]
    if len(a) < 3:
        sys.exit(__doc__)
    out, act, a = a[0], a[1], a[2:]
    px = 150
    if a[:1] == ["--px"]:
        px, a = int(a[1]), a[2:]
    S = []
    for spec in a:
        n, folder = spec.split("=", 1)
        S += [(f"{n}-{d.name[8:]}", d) for d in sorted(Path(folder).iterdir())
              if d.is_dir() and len(d.name) == 14 and d.name.isdigit() and (d / f"{act}.png").exists()]
    if not S:
        sys.exit("no sheet drew %s" % act)
    cols = 11; rows = -(-len(S) // cols)
    im = Image.new("RGB", (cols * (px + 6), rows * (px + 26)), (0x30, 0x34, 0x3A)); dr = ImageDraw.Draw(im)
    for i, (t, d) in enumerate(S):
        r, c = divmod(i, cols); x, y = c * (px + 6), r * (px + 26)
        dr.text((x + 4, y + 2), t, font=F, fill=(230, 230, 230))
        g = Image.open(d / f"{act}.png").convert("RGBA").resize((px, px), Image.LANCZOS)
        b = Image.new("RGB", (px, px), (0x62, 0x6C, 0x7B)); b.paste(g, (0, 0), g); im.paste(b, (x, y + 22))
    im.save(out)


if __name__ == "__main__":
    main()
