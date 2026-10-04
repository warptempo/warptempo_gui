#pragma once
// tools/palette/picker — THE PICKER: the scene's picture with its layers, and the colour panel over it. Portable
// (cairo + the scene, no platform call): main_android.cpp feeds it the touch and blits its frame; host_check.cpp
// drives it on the laptop.
//
// THE COLOUR'S TRUTH IS THE RGB BYTE TRIPLE (ColourState): every control produces an exact triple, and the active
// layer repaints from it. HSV is a view over the bytes plus a RETAINED hue and saturation, as GTK's selector keeps
// them: through grey (no chroma) the hue stays, through black (V 0) the saturation stays too, so a drag never snaps
// the hue to 0.
//
// THE PANEL (GTK's colour selector and GIMP's colour dialog, their common ground, HSV only, no CMYK): the hue ring
// with the saturation/value triangle inside it (the triangle's corners the pure hue, white and black, turning with
// the hue); the hex in large type and the OLD | NEW swatches (a tap on OLD reverts); the six sliders H, S, V and
// R, G, B, each a long track painted with its live gradient, a handle, a one-unit decrement and increment at its
// ends (acting at the lift) and the value beside it. Its chrome is the app's own greys (the ground #191919, the label
// #FFFFFF, fields #212121, flat 1-px #000000 edges), no relief, no alpha.
//
// OPENING AND CLOSING: a tap on the picture opens the panel on the HALF OPPOSITE the tap, so the place tapped stays in
// view; a tap outside the panel closes it, COMMITTING the colour when it is EDITED (below), and rewrites state.json
// either way. While closed, a small label at the bottom right carries the active layer's name, hex and count.
//
// THE PICK HISTORY (architect 2026-10-04: an undo / redo over the saved picks, so he can backtrack and compare without
// typing a hex): the active layer's committed picks, oldest first -- exactly its picks.txt lines, read at launch and
// appended to as commits happen -- and a CURSOR on the entry being shown. BACK and FORWARD (under the hex, the count
// "N of M" between them) move the cursor one entry and set the colour to it, at the pen's lift. NO SAVED PICK IS
// EVER THROWN AWAY, AND CLOSING THE PANEL IS THE ONE DELIBERATE SAVE (architect 2026-10-04: "just throw it away. One
// deliberate save action"): the colour is EDITED when it differs from the cursor's entry (or the history is empty);
// a close commits an edited colour (picks.txt, the history's end, one logcat line) and the cursor goes to the new
// end, while a close on an unedited colour appends nothing (state.json still records the colour and the cursor). An
// unsaved edit is DISCARDED by a step (the step goes from the cursor's entry) and by leaving the app with the panel
// open (discard_if_open: the colour and the cursor go back to the panel's opening, the last saved state).
// TRUTHFUL BUTTONS: BACK is disabled at the first entry, FORWARD at the last, both with an empty history; a disabled
// button's glyph is dimmed and its lift does nothing.

#include "colour.h"
#include "scene.h"

#include <cairo.h>

#include <string>
#include <vector>

// the active layer's history: its picks.txt colours, oldest first; the cursor an index into them, -1 iff empty
struct History {
    std::vector<Rgb> picks;
    int cursor = -1;
};

// THE LAUNCH STATE, shared by the device and the laptop check: state.json and picks.txt under data_dir over the
// loaded scene. The active layer takes its state.json colour if any (the last close), else keeps the manifest's;
// derived layers follow. The cursor is state.json's entry for the active layer when that entry exists, lies in the
// history and holds the colour; otherwise (the previous build's state.json, which has no entry, or a picks.txt
// deleted or trimmed beside a kept state.json) the NEWEST entry equal to the colour, else the end. `note` says which,
// for the log line. A malformed file is false and `err`.
bool picker_load(const std::string& data_dir, Scene& scene, History& hist, std::string& note, std::string& err);

// the platform's log line (logcat tag warptempo_picker on the device, stderr on the laptop)
void plog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

struct ColourState {
    Rgb rgb;
    double h = 0, s = 0, v = 0;   // degrees 0..360, 0..1, 0..1: the view, with the retained hue and saturation

    // the bytes as given; HSV re-derived from them, the hue kept through grey and the saturation through black
    void set_rgb(Rgb c);
    // the bytes from HSV (each channel rounded to the nearest byte); the HSV kept as given
    void set_hsv(double hh, double ss, double vv);
};

class Picker {
public:
    // the scene and the history as picker_load left them; data_dir is where picks.txt and state.json go
    Picker(Scene scene, std::string data_dir, History hist);

    // one pointer, window pixels
    void press(double x, double y);
    void move(double x, double y);
    void release(double x, double y);
    void cancel();

    // the panel open at the pause (home, the cover): closed WITHOUT a save, its unsaved colour discarded -- the colour
    // and the cursor back to the panel's opening, the last saved state; no file is written
    void discard_if_open();

    bool needs_paint() const { return dirty_; }
    // the whole frame: the picture, then the panel or the corner label (frame: ARGB32, the scene's size)
    void paint(cairo_surface_t* frame);

    const Scene& scene() const { return scene_; }
    const ColourState& colour() const { return cs_; }
    bool open() const { return open_; }
    bool panel_on_right() const { return right_; }
    Rgb old() const { return old_; }
    const History& history() const { return hist_; }
    bool edited() const;          // the colour differs from the cursor's entry, or the history is empty
    bool back_enabled() const;
    bool forward_enabled() const;

private:
    enum class Target { None, Outside, Ring, Triangle, Track, Minus, Plus, Old, Back, Forward, Picture };
    void apply_colour();               // the active layer takes cs_.rgb; derived layers follow; the picture repaints
    void close();                      // the one save: commit an edited colour, else rewrite state.json alone
    void commit();                     // append to picks.txt and the history, the cursor to the end; state.json
    void write_state() const;          // state.json: every layer's colour and the active layer's cursor
    void history_step(int dir);        // BACK (-1) / FORWARD (+1) from the cursor's entry, an unsaved edit discarded
    void track_to(int row, double x);
    void step(int row, int dir);
    void triangle_to(double x, double y);
    void ring_to(double x, double y);
    double panel_x() const;
    double panel_y() const;

    Scene scene_;
    std::string data_dir_;
    std::vector<uint32_t> picture_;    // the background with every layer, kept current
    ColourState cs_;
    Rgb old_;
    History hist_;
    int open_cursor_ = -1;             // the cursor when the panel opened: discard_if_open's return
    bool open_ = false, right_ = false, dirty_ = true;
    Target target_ = Target::None;
    int row_ = -1;                     // the slider row a Track / Minus / Plus press holds
    double down_x_ = 0, down_y_ = 0;

    // caches: the hue ring (never changes) and the triangle at tri_h_
    std::vector<uint32_t> ring_;       // kWheel x kWheel, 0 = not ring
    std::vector<uint32_t> tri_;        // kWheel x kWheel, 0 = not triangle
    double tri_h_ = -1;
};

// the panel's whole geometry, device px (the tablet's 2304 x 1440), relative to the panel's top-left
namespace panel {
constexpr int kW = 1120, kH = 1292, kMargin = 16, kPad = 36;
constexpr int kWheel = 580, kROut = 290, kRIn = 220, kRTri = 210;
constexpr int kColX = kPad + kWheel + 44, kColX1 = kW - kPad;            // the hex and swatch column
constexpr int kHistY = kPad + 176;                                        // BACK | N of M | FORWARD, under the hex
constexpr int kSwatchY0 = kPad + 324, kSwatchY1 = kPad + kWheel;
constexpr int kSwatchW = 200, kNewX = kColX1 - kSwatchW;
constexpr int kRowsY = kPad + kWheel + 44, kRowStep = 100, kGroupGap = 20, kRowH = 76;
constexpr int kLabelX = kPad, kMinusX = kPad + 50, kBtn = 76;
constexpr int kTrackX = kMinusX + kBtn + 14, kTrackL = 682, kTrackH = 56;
constexpr int kPlusX = kTrackX + kTrackL + 14, kFieldX = kPlusX + kBtn + 16, kFieldW = kColX1 - kFieldX;
constexpr int kBackX = kColX, kFwdX = kColX1 - kBtn;                      // the history's buttons, kBtn square
constexpr int row_y(int i) { return kRowsY + i * kRowStep + (i >= 3 ? kGroupGap : 0); }
} // namespace panel

// a plain message on the ground (the scene missing or malformed): each line in the sans, white
void paint_message(cairo_surface_t* frame, const std::vector<std::string>& lines);
