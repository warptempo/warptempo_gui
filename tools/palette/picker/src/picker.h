#pragma once
// tools/palette/picker — THE PICKER: the scene's picture with every element live, and the colour panel over it.
// Portable (cairo + the export, no platform call): main_android.cpp feeds it the touch and blits its frame;
// host_check.cpp drives it on the laptop.
//
// THE ELEMENTS (architect 2026-10-04, the chrome round: he picks the chrome and its neighbours as he picked the ink,
// one element at a time, choosing which): the export's elements (scene.h), each with its own colour, HSV view, pick
// history and cursor (ElementState), ONE ACTIVE -- the one the panel edits. Each element is picked over its own
// scene; EVERY ELEMENT'S CURRENT COLOUR IS LIVE IN EVERY SCENE (picking the chrome shows the ink at its saved colour).
//
// THE CHOOSER: the active element's NAME at the panel's top is a button; a tap on it (at the lift, as every panel
// control) opens the chooser, a vertical list of the elements in manifest order, the active one marked. A tap on an
// entry picks it and closes the chooser; a tap outside the chooser closes it and changes nothing. CHOOSING ANOTHER
// ELEMENT IS THE CLOSE FOR THE ONE BEING LEFT (the planner's ruling, confirmed by the architect 2026-10-04): an
// edited colour is committed exactly as a close commits it (one picks.txt line, the logcat line, state.json), then
// the panel stands open on the chosen element, OLD its current colour -- he switches to see the new colour against
// its neighbours, and a switch that threw the colour he is looking at away would undo what he came to compare (an
// empty history counts as edited, as at a close). Choosing the active element again is a no-op.
//
// THE COLOUR'S TRUTH IS THE RGB BYTE TRIPLE (ColourState): every control produces an exact triple, and the active
// element repaints from it. HSV is a view over the bytes plus a RETAINED hue and saturation, as GTK's selector keeps
// them: through grey (no chroma) the hue stays, through black (V 0) the saturation stays too, so a drag never snaps
// the hue to 0.
//
// THE HSV HE DIALLED IS PART OF THE PICK (architect 2026-10-04): a saved pick keeps the exact view it was saved under
// beside its bytes (Pick, scene.h), and EVERY ROAD BACK TO A STORED COLOUR RESTORES THAT VIEW instead of re-deriving it
// from the bytes -- the launch, BACK / FORWARD, OLD, leaving the app with the panel open. Re-derived, a view is the
// bytes' own HSV (S 0.3529 where he dialled 0.35): the number still reads 35 but the handle sits elsewhere, and the
// next − / + rounds from the re-derived values, while at low saturation or value one byte is several degrees of hue
// or a percent of saturation -- so an axis he never touched would move. Re-derivation from bytes stays only where the
// bytes are the input: the R / G / B tracks and their − / +, and a pick saved before views were stored. The ring,
// the triangle and the H / S / V tracks and − / + set the view directly.
//
// THE PANEL (GTK's colour selector and GIMP's colour dialog, their common ground, HSV only, no CMYK): the hue ring
// with the saturation/value triangle inside it (the triangle's corners the pure hue, white and black, turning with
// the hue); the element button, the hex in large type and the OLD | NEW swatches (a tap on OLD reverts); the six
// sliders H, S, V and R, G, B, each a long track painted with its live gradient, a handle, a one-unit decrement and
// increment at its ends (acting at the lift) and the value beside it. Its chrome is the app's own greys (the ground
// #191919, the label #FFFFFF, fields #212121, flat 1-px #000000 edges), no relief, no alpha.
//
// OPENING AND CLOSING: a tap on the picture opens the panel on the HALF OPPOSITE the tap, so the place tapped stays in
// view; a tap outside the panel closes it, COMMITTING the colour when it is EDITED (below), and rewrites state.json
// either way. While closed, a small label at the bottom right carries the active element's name, hex and count.
//
// THE PICK HISTORY (architect 2026-10-04: an undo / redo over the saved picks, so he can backtrack and compare without
// typing a hex), ONE PER ELEMENT: its committed picks, oldest first -- exactly its picks.txt lines, read at launch and
// appended to as commits happen -- and a CURSOR on the entry being shown. BACK and FORWARD (under the hex, the count
// "N of M" between them) move the active element's cursor one entry and restore it (its bytes and its view), at the
// pen's lift. NO SAVED PICK IS EVER THROWN AWAY, AND CLOSING THE PANEL IS THE ONE DELIBERATE SAVE (architect
// 2026-10-04: "just throw it away. One deliberate save action"; choosing another element is that close): the colour is
// EDITED when it differs from the cursor's entry -- its bytes, or its view when the entry carries one (at low value an
// S − / + can move the view and not the bytes, and the number he reads is what he saves) -- or the history is empty;
// a close commits an edited colour (picks.txt, the history's end, one logcat line) and the cursor goes to the new end,
// while a close on an unedited colour appends nothing (state.json still records the colour, its view and the cursor).
// An unsaved edit is DISCARDED by a step (the step goes from the cursor's entry) and by leaving the app with the panel
// open (discard_if_open: the colour, its view and the cursor go back to the panel's opening, the last saved state).
// TRUTHFUL BUTTONS: BACK is disabled at the first entry, FORWARD at the last, both with an empty history; a disabled
// button's glyph is dimmed and its lift does nothing.

#include "colour.h"
#include "scene.h"

#include <cairo.h>

#include <string>
#include <vector>

// one element's history: its picks.txt picks, oldest first; the cursor an index into them, -1 iff empty
struct History {
    std::vector<Pick> picks;
    int cursor = -1;
};

// THE LAUNCH STATE, shared by the device and the laptop check: state.json and picks.txt under data_dir over the
// loaded export. EVERY ELEMENT takes its state.json colour if any (the last close), else keeps the manifest's; its
// `start` is that colour with state.json's view of it when the file has one (the Picker restores it). Its cursor is
// state.json's entry for it when that entry exists, lies in its history and holds the start; otherwise (an earlier
// build's state.json, which may have no entry, or a picks.txt deleted or trimmed beside a kept state.json) the NEWEST
// entry holding it, else the end. An entry HOLDS the start when their bytes are equal and, if both carry a view, their
// views are too. The ACTIVE element is state.json's when it names one, else the manifest's. `note` says how the
// active element started, for the log line. A malformed file is false and `err`.
struct Launch {
    std::vector<History> hist;   // per element, manifest order
    std::vector<Pick> start;     // per element
    int active = -1;
    std::string note;
};
bool picker_load(const std::string& data_dir, Export& ex, Launch& out, std::string& err);

// the platform's log line (logcat tag warptempo_picker on the device, stderr on the laptop)
void plog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

struct ColourState {
    Rgb rgb;
    double h = 0, s = 0, v = 0;   // degrees 0..360, 0..1, 0..1: the view, with the retained hue and saturation

    // the bytes as given; HSV re-derived from them, the hue kept through grey and the saturation through black
    void set_rgb(Rgb c);
    // the bytes from HSV (each channel rounded to the nearest byte); the HSV kept as given
    void set_hsv(double hh, double ss, double vv);
    // a stored pick: its bytes with its view as saved, or (a pick saved before views were stored) set_rgb's
    void restore(const Pick& p);
    // the state as a pick, its view included: what a commit stores
    Pick pick() const { return Pick{rgb, true, h, s, v}; }
    // the state shows the pick: the same bytes and, when the pick carries a view, the same view
    bool shows(const Pick& p) const;
};

// one element as the Picker holds it: its colour state and its history
struct ElementState {
    ColourState cs;
    History hist;
};

class Picker {
public:
    // the export, the histories and the starts as picker_load left them; data_dir is where picks.txt and state.json go
    Picker(Export ex, std::string data_dir, Launch launch);

    // one pointer, window pixels
    void press(double x, double y);
    void move(double x, double y);
    void release(double x, double y);
    void cancel();

    // the panel open at the pause (home, the cover): closed WITHOUT a save, its unsaved colour discarded -- the colour
    // and the cursor back to the panel's opening (or the element's choice), the last saved state; no file is written
    void discard_if_open();

    bool needs_paint() const { return dirty_; }
    // the whole frame: the picture, then the panel (and the chooser) or the corner label (frame: ARGB32, the export's size)
    void paint(cairo_surface_t* frame);

    const Export& exp() const { return ex_; }
    int active() const { return active_; }
    const ColourState& colour() const { return el_[size_t(active_)].cs; }
    const History& history() const { return el_[size_t(active_)].hist; }
    const ElementState& element(int e) const { return el_[size_t(e)]; }
    const std::vector<uint32_t>& picture() const { return picture_; }   // the active element's scene, kept current
    bool open() const { return open_; }
    bool chooser_open() const { return chooser_; }
    bool panel_on_right() const { return right_; }
    Rgb old() const { return old_.rgb; }
    bool edited() const;          // the state does not show the cursor's entry (ColourState::shows), or no history
    bool back_enabled() const;
    bool forward_enabled() const;

private:
    enum class Target { None, Outside, Ring, Triangle, Track, Minus, Plus, Old, Back, Forward, Name, Row, OffChooser, Picture };
    void apply_colour();               // the active element takes its cs' bytes; the roles follow; the picture repaints
    void close();                      // the one save: commit an edited colour, else rewrite state.json alone
    void append_pick();                // picks.txt, the history's end, the cursor to it, the logcat line
    void choose(int e);                // the chooser's pick: the close for the element left, then the panel on e
    void write_state() const;          // state.json: every element's colour, view and cursor, the active element
    void history_step(int dir);        // BACK (-1) / FORWARD (+1) from the cursor's entry, an unsaved edit discarded
    void track_to(int row, double x);
    void step(int row, int dir);
    void triangle_to(double x, double y);
    void ring_to(double x, double y);
    double panel_x() const;
    double panel_y() const;
    ColourState& cs() { return el_[size_t(active_)].cs; }
    History& hist() { return el_[size_t(active_)].hist; }

    Export ex_;
    std::string data_dir_;
    std::vector<ElementState> el_;
    int active_ = 0;
    int shown_ = -1;                   // the scene picture_ holds (the active element's)
    std::vector<uint32_t> words_, old_words_;   // every role's frame word, now and before the last change
    std::vector<uint32_t> picture_;    // the shown scene at the current colours, kept current
    ColourState old_;                  // the state the panel opened with (or the element chosen with): OLD's and the discard's
    int open_cursor_ = -1;             // the cursor then: discard_if_open's return
    bool open_ = false, right_ = false, dirty_ = true, chooser_ = false;
    Target target_ = Target::None;
    int row_ = -1;                     // the slider row a Track / Minus / Plus press holds, the chooser row a Row press
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
constexpr int kNameY = kPad, kNameH = 64;                                 // the element button, the column's top
constexpr int kChooserY = kNameY + kNameH + 8, kChooserRowH = 76;         // the chooser's rows, under the button
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
