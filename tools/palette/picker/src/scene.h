#pragma once
// tools/palette/picker — THE SCENE: a picture of the app (render.py --export, tools/palette/README.md) and the
// LAYERS painted over it, each a colour through a binary mask. Read once at launch from <data dir>/scene/:
//
//   manifest.json   {"width": W, "height": H, "background": "background.ppm", "active": "<layer>",
//                    "layers": [{"name": "ink", "mask": "ink.pgm", "colour": "#808080"},
//                               {"name": "outline", "mask": "outline.pgm",
//                                "derive": {"from": "ink", "over": "#0E0E0E", "linear_mix": 0.5}}]}
//   background.ppm  binary P6, 8-bit, W x H
//   <layer>.pgm     binary P5, 8-bit, W x H, every byte 0 or 255 (anything else is a hard fail: the palette
//                   composites nothing)
//
// Painting: the background, then each layer in manifest order, its colour on every pixel its mask holds at 255.
// A DERIVED layer's colour is its rule over the layer it follows: the linear-light mix of `from` toward `over` by
// `linear_mix` (colour.h lin_mix; the waveform outline is the ink's 50 % mix over the canvas). The active layer is
// the one being picked; it is never derived.

#include "colour.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Layer {
    std::string name;
    std::string mask_file;
    bool derived = false;
    int from = -1;               // derived: the layer it follows (an index into Scene::layers)
    Rgb over;                    // derived: the colour it mixes toward
    double linear_mix = 0;       // derived: how far toward `over` (0 = the followed colour)
    Rgb colour;                  // the current colour (a derived one recomputed by scene_derive)
    std::vector<uint32_t> pixels;   // the mask's 255 pixels, as indices y * width + x
};

struct Scene {
    int width = 0, height = 0;
    std::vector<uint32_t> background;   // the frame's words (colour.h word_of), width x height
    std::vector<Layer> layers;
    int active = -1;
};

// <dir>/manifest.json and the files it names -> true and the scene; false and `err` ("<file>: what is wrong")
bool scene_load(const std::string& dir, Scene& scene, std::string& err);

// recompute every derived layer's colour from the colours it follows
void scene_derive(Scene& scene);

// the background, then every layer in order, into `picture` (width x height words)
void scene_paint(const Scene& scene, uint32_t* picture);
// every layer in order only (the background already in place): what a colour change repaints
void scene_paint_layers(const Scene& scene, uint32_t* picture);

// A PICK: the bytes, which are the colour's truth (what is painted, what the product takes), and the HSV VIEW it was
// saved under (architect 2026-10-04: "the HSV he dialled is part of the pick"): h in degrees 0..360, s and v 0..1,
// the doubles exactly as ColourState held them (picker.h). Every road back to a stored pick restores that view with
// the bytes instead of re-deriving it from them, so a handle sits where it was and a − / + steps from exactly what
// the numbers show. A pick written before the view was stored has none (has_hsv false) and is re-derived from its
// bytes, as it always was. The view always gives the bytes (rgb_of_hsv(h, s, v) == rgb): the panel can produce no
// other pair, so a stored pair that disagrees is a hard fail at the load.
struct Pick {
    Rgb rgb;
    bool has_hsv = false;
    double h = 0, s = 0, v = 0;
};

// a stored view's three numbers as text: the shortest decimal that reads back as the same double ("227", "0.35",
// "0.34671532846715331"), so the view round-trips exactly
std::string view_number(double x);

// THE PICKER'S STATE FILE, <data dir>/state.json, rewritten whole at every close of the panel:
//
//   {"colours": {"<layer>": "#RRGGBB", ...}, "hsv": {"<active layer>": [h, s, v]}, "entry": {"<active layer>": N}}
//
// every layer's colour at the last close, the active layer's HSV view at that close (Pick), and its history cursor as
// the panel's count shows it (N of M, 1-based: the Nth of that layer's picks.txt lines). Two earlier builds' files
// still read: {"colours": ..., "entry": ...} with no "hsv" (the colour re-derived), and the first build's flat
// {"<layer>": "#RRGGBB", ...} (no entry: the cursor is placed by picker_load's rule). `out` takes every layer's colour,
// with the view where the file has one. A missing file is two empty maps (no close yet); a malformed one is false and
// `err`.
bool state_load(const std::string& path, std::map<std::string, Pick>& out, std::map<std::string, int>& entries,
                std::string& err);

// THE PICKS LOG, <data dir>/picks.txt, appended at every commit: one line per pick,
// "<ISO-8601 local time> <layer> #RRGGBB hsv <h> <s> <v>\n" (the view as view_number writes it); a line written before
// the view was stored, "<ISO-8601 local time> <layer> #RRGGBB\n", still reads, as a pick without a view. `out` takes
// `layer`'s picks, oldest first (its history); other layers' lines are skipped. A missing file is an empty history; a
// line of neither shape, or whose view does not give its hex, is false and `err`.
bool picks_load(const std::string& path, const std::string& layer, std::vector<Pick>& out, std::string& err);
