# tools/mockup — sheets onto the tablet's Gallery, and THE COLOR ROAD

THE COLOR ROAD (measured on the architect's glass 2026-10-22, after four sessions of flipping between roads on
screenshots; his word that day: "this problem was already solved. We're reinventing the wheel"): THE iCCP TAG SAYS
WHAT THE BYTES ARE. His viewer is Fossify Gallery (the tablet's VIEW default, not Samsung Gallery), and it honors a
PNG's Display-P3 tag: a TAGGED file's bytes are shown as Display-P3 coordinates, an UNTAGGED file's as sRGB. The
app's window is a Display-P3 layer (`src/gui/display_transform.h`): with `true_colors=on` it converts every authored
sRGB byte to P3 at the painter's entry, so its bytes — and a screencap's, which always carries the 520-byte Skia
Display-P3 iCCP, as his own screenshots do — are P3 bytes; with `true_colors=off` the same screencap holds the
authored sRGB bytes under the same tag. So:

- A SCREENCAP WITH TRUE COLORS ON IS RIGHT AS IT IS. Keep its tag.
- A SCREENCAP WITH TRUE COLORS OFF IS RIGHT ONLY WITH ITS TAG STRIPPED. Tagged, the Gallery reads its sRGB bytes as
  P3: oversaturated, redder — his "bright orange" of 2026-10-22 ~15:55 and his "wrong in every region" of 2026-10-21.
  Untagged, its bytes are the authored hex, so a value read off it in GIMP or a browser round-trips (that is what
  true colors off is for, display_transform.h's head).
- A COLOR INSERTED INTO A MOCK follows the file's kind: into a tagged (P3) mock through the app's transform
  (`road.py`'s `srgb_to_p3`, display_transform.h's math byte for byte); into an untagged (sRGB) mock as the sRGB
  byte itself. A P3 byte in an untagged file reads pale and desaturated (his word on the split sheet's right half);
  an sRGB byte in a tagged file reads oversaturated.

THE PROOF OF 2026-10-22:
two split sheets of one screen, the left half a true-colors-off capture's bytes and the right half a true-colors-on
capture's, one file tagged and one untagged, beside the live app. His verdict: untagged — the left (sRGB) half
matches the app, the right reads less red and desaturated; tagged — the right (P3) half matches. Everything else
measured that day agrees: the app's transform ported to numpy reproduces a true-colors-on capture from a
true-colors-off one on 3,302,322 of 3,317,760 pixels (the rest antialiased edges within one step); a screencap of the
Gallery itself equals the file's bytes for every kind, so A SCREENCAP CANNOT SEE how the panel treats a layer (the
closed question of 2026-10-02) — the glass alone decides, and it has. The "paler than the GUI" verdict of 2026-10-22
~12:55 that reversed the road was on mocks whose every pixel but a FLAT marker lane was the app's own tagged P3
bytes: the flat lane against the grainy brass, a design remark, not color management.

```
tools/mockup/road.py capture <name> [--device D]   screencap into /sdcard/Download/<name>.png and ./<name>.png, the
                                                   tag kept or stripped by the app's true_colors key (read by run-as)
tools/mockup/road.py srgb <in> <out> [--bytes srgb|p3]   an untagged sRGB file from any file (tagged input converted P3 -> sRGB)
tools/mockup/road.py p3 <in> <out> [--bytes srgb|p3]     a tagged P3 file from any file (untagged input converted sRGB -> P3)
                                                   `--bytes` names the input's bytes where its tag lies — a RAW true-colors-off
                                                   screencap, sRGB bytes under the device's tag, is the one such file (`--bytes srgb`)
tools/mockup/road.py check <png...>                each file's kind and sample means
tools/mockup/push.sh <png...>                      each file in name order: pushed, touched, announced to the media
                                                   scanner, two seconds apart, so Gallery lists them in their names' order
tools/mockup/push.sh --delete <name...>            the superseded sheets (base names) off the tablet, the scanner told
```

`display_p3.icc` is the 520-byte profile every tablet capture carries, kept here so `road.py p3` tags a file exactly as
the device does. The planner runs these; the coder never does. The mock-up renderer that lived here retired on
2026-10-07 with the palette mock tool (both stay in the git history); the sheets since are ad-hoc numpy scripts kept
in the planner's scratch folder, drawn over his tablet captures under the road above.
