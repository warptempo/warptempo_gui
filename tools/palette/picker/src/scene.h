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

// THE PICKER'S STATE FILE, <data dir>/state.json: {"<layer>": "#RRGGBB", ...}, each layer's colour at the last
// commit. A missing file is an empty map (no commit yet); a malformed one is false and `err`.
bool state_load(const std::string& path, std::map<std::string, Rgb>& out, std::string& err);
