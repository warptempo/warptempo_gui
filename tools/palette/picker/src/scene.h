#pragma once
// tools/palette/picker — THE EXPORT: the ELEMENTS he picks and the SCENES he picks them over, written by
// render.py --export (tools/palette/README.md, the export) and read once at launch from <data dir>/scene/:
//
//   manifest.json      {"width": W, "height": H, "active": "<key>",
//                       "elements": [{"key": "ink", "name": "Ink", "colour": "#A6B9DE", "scene": "waveform"}, ...],
//                       "roles": [{"name": "ground", "element": "chrome"},
//                                 {"name": "bevel_shadow", "scale": {"of": "chrome", "num": 128, "den": 192}},
//                                 {"name": "outline", "derive": {"from": "ink", "over": "canvas", "linear_mix": 0.5}},
//                                 {"name": "label", "colour": "#FFFFFF"}, ...],
//                       "scenes": [{"name": "waveform", "base": "waveform.base.pgm", "cover": "waveform.cover.bin"}, ...]}
//   <scene>.base.pgm   binary P5, 8-bit, W x H: each byte the role-table index of the pixel's BASE colour
//   <scene>.cover.bin  the antialiased pixels' COVERAGE STACKS: "WTCOVER1", a little-endian uint32 count, then per
//                      pixel in ascending order its uint32 index y x W + x, a uint8 depth n >= 1 and n (uint8 role,
//                      uint8 coverage 1..254) pairs, oldest first
//
// AN ELEMENT is one colour he picks (the chooser lists them in manifest order): its key (its word in picks.txt and
// state.json), its Title Case name, its colour and the scene it is picked over. A ROLE is a colour the scenes paint,
// each with its RULE over the elements: an element's own colour; `scale`, the chrome rule's Windows 95 proportion of
// an element (colour.h scale_byte: each channel x num / den, half to even, capped); `derive`, the linear-light mix of
// one element toward another or a literal (colour.h lin_mix; the waveform outline follows the live ink and canvas);
// or a fixed `colour`. A rule follows elements only. EVERY ELEMENT'S CURRENT COLOUR IS LIVE IN EVERY SCENE.
//
// A SCENE'S PICTURE is every pixel's base role's colour, then over each stacked pixel its paints in order, each the
// role's colour through its coverage by cairo's own arithmetic (colour.h over_n_8): the picture render.py drew, byte
// for byte, at any element colours. NOTHING IS RASTERIZED HERE: a repaint is a fill of the solid pixels of the roles
// that moved and a lookup-and-blend over the stacks that read them.

#include "colour.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct Element {
    std::string key, name;
    Rgb colour;                  // the current colour (the Picker keeps it)
    int scene = -1;              // an index into Export::scenes
};

struct Role {
    enum class Kind { Element, Colour, Scale, Derive };
    std::string name;
    Kind kind = Kind::Colour;
    int el = -1;                 // Element / Scale: the element; Derive: the element it mixes from
    int over = -1;               // Derive: the element it mixes toward, or -1 for the literal `rgb`
    Rgb rgb;                     // Colour: the colour; Derive with over -1: the literal it mixes toward
    int num = 1, den = 1;        // Scale
    double linear_mix = 0;       // Derive: how far toward `over` (0 = the element's own colour)
    uint32_t deps = 0;           // the elements its colour reads, one bit each
};

struct Run { uint32_t start, len; };   // a row's pixels start .. start + len - 1 (indices y x W + x)
struct Stack {                   // one antialiased pixel
    uint32_t index;              // y x W + x
    uint32_t deps;               // the elements its colour reads (its base's and its paints')
    uint32_t first;              // its paints: Scene::layers[first .. first + n)
    uint8_t base, n;
};
struct Layer { uint8_t role, cov; };

struct Scene {
    std::string name;
    std::vector<std::vector<Run>> solid;   // per role: the pixels whose colour is that role's alone
    std::vector<Stack> stacks;
    std::vector<Layer> layers;
};

// THE PRODUCT'S THEMES (themes.json beside the manifest, written by the same export; render.py product_themes is the
// authoritative statement): every entry of the product's theme table (src/gui/theme_table.h, from
// docs/themes/catalog.json) at its LIGHT level, in the table's order --
//
//   {"source": "...", "themes": [{"key": "windows-95-standard", "name": "Windows 95 Standard", "ground": "#C0C0C0",
//                                 "colours": [{"hex": "#C0C0C0", "names": ["ground", "Scrollbar", ...]}, ...]}, ...]}
//
// A theme's COLOURS are every distinct byte triple the catalog records for it (its roles, its source's raw values, its
// toolkit's computed shades), each once with every name that records it, the ground first. The presets pop-up lists
// the themes by key with a swatch of the ground; a theme OPENED shows its colours as the panel's theme strip
// (picker.h).
struct ThemeColour {
    Rgb rgb;
    std::vector<std::string> names;
};
struct Theme {
    std::string key, name;
    Rgb ground;
    std::vector<ThemeColour> colours;
};

struct Export {
    int width = 0, height = 0;
    std::vector<Element> elements;
    std::vector<Role> roles;
    std::vector<Scene> scenes;
    int active = -1;             // the manifest's active element
    std::vector<Theme> themes;   // themes.json
};

// <dir>/manifest.json, the files it names and themes.json -> true and the export; false and `err` ("<file>: what is
// wrong"); an export without themes.json (written before the presets) is refused: export the theme again
bool export_load(const std::string& dir, Export& ex, std::string& err);
// the theme index of `key`, or -1
int theme_of(const Export& ex, const std::string& key);

// a role's colour at the elements' current colours
Rgb role_colour(const Export& ex, const Role& r);
// every role's frame word (colour.h word_of) at the elements' current colours
void role_words(const Export& ex, std::vector<uint32_t>& words);
// the element index of `key`, or -1
int element_of(const Export& ex, const std::string& key);

// a scene's whole picture into `picture` (width x height words) at the role words
void scene_paint(const Export& ex, int scene, const std::vector<uint32_t>& words, uint32_t* picture);
// THE LIVE REPAINT: after the elements in `changed` (bits) moved from `old_words` to `words`, refill the solid pixels
// of every role whose word changed and re-blend the stacks that read a changed element; nothing else is touched
void scene_repaint(const Export& ex, int scene, const std::vector<uint32_t>& old_words, const std::vector<uint32_t>& words,
                   uint32_t changed, uint32_t* picture);

// A PICK: the bytes, which are the colour's truth (what is painted, what the product takes), and the VIEW it was
// saved under (architect 2026-10-04: "the HSV he dialled is part of the pick"; the model switch made it any model's):
// its MODEL (colour.h: HSV, HSL or LCh) and its three numbers, the doubles exactly as ColourState held them
// (picker.h) -- HSV h degrees, s, v 0..1; HSL h degrees, s, l 0..1; LCh L 0..100, C 0..kChromaMax, h degrees. Every
// road back to a stored pick restores that view with the bytes instead of re-deriving it from them, when the panel
// shows that model, so a handle sits where it was and a − / + steps from exactly what the numbers show. A pick written
// before the view was stored has none (has_view false) and is re-derived from its bytes, as it always was. The view
// always gives the bytes (rgb_of_view(model, x) == rgb, an LCh view inside the gamut, colour.h unit_in_gamut): the
// panel can produce no other pair, so a stored pair that disagrees is a hard fail at the load.
struct Pick {
    Rgb rgb;
    bool has_view = false;
    Model model = Model::Hsv;
    double x[3] = {0, 0, 0};
};
// a view lies in its model's ranges and gives its bytes (Pick's rule)
bool view_holds(const Pick& p);

// a stored view's three numbers as text: the shortest decimal that reads back as the same double ("227", "0.35",
// "0.34671532846715331"), so the view round-trips exactly
std::string view_number(double x);

// THE PICKER'S STATE FILE, <data dir>/state.json, rewritten whole at every close of the panel and every switch of
// element or model:
//
//   {"active": "<key>", "model": "hsv" | "hsl" | "lch", "colours": {"<key>": "#RRGGBB", ...},
//    "hsv": {"<key>": [h, s, v], ...}, "hsl": {"<key>": [h, s, l], ...}, "lch": {"<key>": [L, C, h], ...},
//    "entry": {"<key>": N, ...}, "theme": "<theme key>"}
//
// every element's colour, its view (Pick) in the map of the view's model -- a key in one of "hsv", "hsl", "lch" at
// most, a map written only when some view is in it -- and its history cursor as the panel's count shows it (N of M,
// 1-based: the Nth of that element's picks.txt lines; absent with an empty history), the active element, the model
// the panel shows (the model switch) and the theme whose strip is open (absent when none; a key no theme of the export
// has is read and never used). Earlier builds' files still read: no "model" (HSV), no "active" (the manifest's
// active element), "hsv" for the active layer alone or no view map at all (a colour without a view starts
// re-derived), and the first build's flat {"<layer>": "#RRGGBB", ...} (no entry: the cursor is placed by
// picker_load's rule). `out` takes every key's colour, with the view where the file has one; a key that is no element
// of this export is carried and never read. A missing file is empty maps (no close yet); a malformed one is false and
// `err`.
bool state_load(const std::string& path, std::map<std::string, Pick>& out, std::map<std::string, int>& entries,
                std::string& active, std::string& theme, Model& model, std::string& err);

// A PRESET (architect 2026-10-04): the whole look -- every element's colour with the exact view it held (as state.json
// holds them) -- under its automatic name "Preset <number>"; the number is the highest in the file plus one, so never
// reused while the file lives (there is no rename, delete or overwrite until a keyboard). <data dir>/presets.json,
// rewritten whole at every save:
//
//   {"presets": [{"number": 1, "saved": "<ISO-8601 local time>", "colours": {"<key>": "#RRGGBB", ...},
//                 "hsv": {"<key>": [h, s, v], ...}, "hsl": {...}, "lch": {...}}, ...]}
//
// oldest first, the numbers ascending; every colour with its view (one giving its hex) in the map of the view's model
// (state.json's maps: each written only when some view is in it, so an all-HSV preset reads as before the model
// switch), the maps together over exactly the colours' keys. A key that is no element of the export is read and never
// used. A missing file is no presets; a malformed one is false and `err`, the first error.
struct Preset {
    int number = 0;
    std::string saved;
    std::map<std::string, Pick> colours;   // every view present (has_view)
};
bool presets_load(const std::string& path, std::vector<Preset>& out, std::string& err);

// THE PICKS LOG, <data dir>/picks.txt, appended at every commit: one line per pick,
// "<ISO-8601 local time> <key> #RRGGBB <model> <a> <b> <c>\n", the model's word (hsv, hsl, lch) and the view's three
// numbers as view_number writes them; a line written before the view was stored, "<ISO-8601 local time> <key>
// #RRGGBB\n", still reads, as a pick without a view. `out` takes every key's picks, oldest first (its history). A
// missing file is an empty history; a line of neither shape, or whose view does not give its hex, is false and `err`.
bool picks_load(const std::string& path, std::map<std::string, std::vector<Pick>>& out, std::string& err);
