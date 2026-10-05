#pragma once
// tools/palette/picker — THE PICKER: the scene's picture with every element live, and the colour panel over it.
// Portable (cairo + the export, no platform call): main_android.cpp feeds it the touch and blits its frame;
// host_check.cpp drives it on the laptop.
//
// THE ELEMENTS (architect 2026-10-04, the chrome round: he picks the chrome and its neighbours as he picked the ink,
// one element at a time, choosing which; the later rounds added theirs over scenes of their own, in the theme alone,
// the flag kinds' round of 2026-10-05 each flag kind's face and selected face, keyed by the product's role names):
// the export's elements (scene.h), each with its own colour, view, pick
// history and cursor (ElementState), ONE ACTIVE -- the one the panel edits. Each element is picked over its own
// scene; EVERY ELEMENT'S CURRENT COLOUR IS LIVE IN EVERY SCENE (picking the chrome shows the ink at its saved colour).
//
// THE CHOOSER: the active element's NAME at the panel's top is a button; a tap on it (at the lift, as every panel
// control) opens the chooser, a list of the elements in manifest order in two columns, the active one marked, running
// down over the wheel and the slider rows and painted over all it covers (panel::kChooserMax, its fit). A tap on an
// entry picks it and closes the chooser; a tap outside the chooser closes it and changes nothing. CHOOSING ANOTHER
// ELEMENT IS THE CLOSE FOR THE ONE BEING LEFT (the planner's ruling, confirmed by the architect 2026-10-04): an
// edited colour is committed exactly as a close commits it (one picks.txt line, the logcat line, state.json), then
// the panel stands open on the chosen element, OLD its current colour -- he switches to see the new colour against
// its neighbours, and a switch that threw the colour he is looking at away would undo what he came to compare (an
// empty history counts as edited, as at a close). Choosing the active element again is a no-op.
//
// THE COLOUR'S TRUTH IS THE RGB BYTE TRIPLE (ColourState): every control produces an exact triple, and the active
// element repaints from it. HSV, HSL and LCh are VIEWS over the bytes (colour.h, the models), each with a RETAINED hue
// as GTK's selector keeps HSV's: through grey (no chroma) the hue stays -- HSV's and HSL's, and LCh's through C 0 --
// and through black HSV's saturation stays too, HSL's through black and white, so a drag never snaps the hue to 0.
//
// THE MODEL SWITCH (architect 2026-10-04): a dropdown button over the three upper tracks picks the model they speak,
// HSV, HSL or LCh (the chooser's style: a tap opens the list, a tap on an entry picks it, a tap outside closes it);
// the tracks, their − / + and their one-decimal readouts then speak that model, R, G, B as ever. THE RING AND THE
// TRIANGLE STAY HSV (GTK's and GIMP's own selector) and work in every model: in HSL the pen's HSV becomes the HSL view
// exactly (the same hue, HSL's saturation and lightness by the closed form), in LCh the pen's HSV gives the bytes and
// LCh reads them. The model is the panel's, every element's alike, and persists in state.json. ONLY LCh CAN LEAVE THE
// GAMUT: a track paints its out-of-gamut stretch in a flat neutral (the panel's ground), and a drag, a tap or a − / +
// whose value would leave the gamut stops at the last in-gamut value on the way there along that axis
// (ColourState::move_axis: a walk from the current value in steps of 0.01 unit to the first value outside, then
// bisection to 1e-9 unit); a value that lies inside the gamut is reached directly, across an out-of-gamut stretch if
// need be. No colour is ever clipped: an LCh view always gives its bytes by rounding alone (colour.h unit_in_gamut).
//
// THE VIEW HE DIALLED IS PART OF THE PICK (architect 2026-10-04): a saved pick keeps the exact view it was saved under
// beside its bytes (Pick, scene.h) -- the model shown when the colour last changed, and its three numbers -- and EVERY
// ROAD BACK TO A STORED COLOUR RESTORES THAT VIEW instead of re-deriving it from the bytes when the panel shows that
// model -- the launch, BACK / FORWARD, OLD, leaving the app with the panel open, a preset's load; a panel showing
// another model re-derives its view from the bytes (the stored view's hue retained for a grey when both are HSV or
// HSL). Re-derived, a view is the bytes' own (S 0.3529 where he dialled 0.35): the number reads 35.3 where he dialled
// 35.0 and the handle sits elsewhere, and the next − / + rounds from the re-derived values, while at low saturation or
// value one byte is several degrees of hue or a percent of saturation -- so an axis he never touched would move.
// Re-derivation from bytes stays only where the bytes are the input: the R / G / B tracks and their − / +, a theme's
// swatch, and a pick saved before views were stored. The ring, the triangle and the three model tracks and − / + set
// the view directly. Switching the model changes no colour and no stored view: switching back without a change
// shows the numbers he dialled.
//
// THE PANEL (GTK's colour selector and GIMP's colour dialog, their common ground, no CMYK): the hue ring
// with the saturation/value triangle inside it (the triangle's corners the pure hue, white and black, turning with
// the hue); the element button (its name set smaller when it would run under the chooser's head), the hex in large type and the OLD | NEW swatches (a tap on OLD reverts); the model
// switch under the wheel; the six sliders -- the model's three (H, S, V or H, S, L or L, C, h) and R, G, B -- each a long
// track painted with its live gradient, a handle, a one-unit decrement and
// increment at its ends (acting at the lift) and the value beside it (row_readout). Its chrome is the app's own greys (the ground
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
//
// THE PRESETS (architect 2026-10-04: he tunes whole looks on the glass and keeps them to come back to): a PRESETS
// button beside the element button opens the PRESETS POP-UP, built like the chooser (the same chrome; a tap on an entry
// acts and closes it, a press lifted on another entry acts on nothing, a tap outside it closes it and changes nothing)
// over the panel below the two buttons. OPENING IT IS THE CLOSE FOR THE PANEL'S EDIT (the chooser's rule): an edited
// colour is committed first (one picks.txt line, state.json), so a preset always snapshots saved colours. Its first
// line, fixed, is "Save as Preset N"; under it a list that SCROLLS by a drag (a drag past kSlop px scrolls and acts on
// nothing; a tap acts at the lift): the saved presets, oldest first, each with a small swatch of every element's
// colour in manifest order; then the heading "Themes" and THE PRODUCT'S THEMES (scene.h), each by its CATALOG KEY (the
// name he types in the app's Settings, whose family prefix groups them by provenance; architect 2026-10-04) with a
// swatch of its ground. The list keeps its scroll while the app lives. SAVE appends the whole look (every element's
// colour and view) to presets.json as "Preset N" (scene.h) -- duplicates allowed -- one logcat line. LOAD sets every
// element to the preset's colour and view: each element whose colour or view changes gets ONE committed pick in its
// own history (picks.txt, as a commit; the element left unchanged gets none), so BACK steps to the history's previous
// newest entry (where he was, when his cursor stood there: always for the active element, whose edit the opening
// committed); state.json follows; the panel stays open on the active element, OLD now the loaded colour; one logcat
// line. No rename, delete or overwrite (they wait for a keyboard).
//
// THE THEME STRIP (architect 2026-10-04: the product's themes as inspiration, not as looks to load): a tap on a theme
// in the pop-up OPENS it -- the pop-up closes and the panel grows a STRIP, a column beside it on the scene's side (the
// panel itself unchanged): the theme's key with its display title small under it, a close control, and every colour the catalog records for it (scene.h,
// Theme) as a swatch beside its hex and the names that record it, a list that scrolls as the pop-up's does. A TAP ON A
// SWATCH ADOPTS IT AS THE ACTIVE ELEMENT'S COLOUR, AS AN EDIT, exactly as a control's drag would: the bytes set, the
// view re-derived from them (set_rgb; a theme stores no view), the scene repainted live, NEW showing it, OLD still
// reverting, the close the one save. For the chrome that sets the ground and every line follows by Windows 95's rule,
// never the theme's own relief. The swatch showing the active element's colour is marked. THE STRIP STAYS OPEN ACROSS
// ELEMENT SWITCHES (he adopts for another element by choosing it) and across relaunches (state.json's "theme"), until
// its close control or another theme opened; it shows while the panel is open.
//
// COPY AND PASTE (architect 2026-10-05: "pick the unselected flag, copy-paste its colour into selected, and then just
// tweak the luminance"): two buttons under OLD | NEW, in the swatches' columns (kCopyX0.., kPasteX0..), acting at the
// lift. COPY takes the active element's current colour and its exact view (ColourState::pick: the model shown when the
// colour last changed and its three numbers). PASTE sets the active element to it AS AN EDIT, exactly as a theme
// swatch's adoption is one: the bytes and the view set (ColourState::restore, the road every stored colour takes back:
// the copied view shown when the panel shows its model, else re-derived from the bytes), the scene live, NEW showing
// it, OLD still reverting, the close the one save. PASTE IS GREYED (its word dimmed, its lift doing nothing) while
// nothing has been copied. The copied colour lives while the app's process does: it is never written to a file. Each
// logs one line, `picker: copy <key> #RRGGBB` / `picker: paste <key> #RRGGBB`.
//
// STATE.JSON IS THE LAST SAVED STATE: written at a close, a switch, a commit at the pop-up's opening, a preset load and
// the strip's opening and close; while the panel is open the active element is written as the panel's opening (OLD
// and its cursor, the last saved state), so an unsaved edit never reaches the file.

#include "colour.h"
#include "scene.h"

#include <cairo.h>

#include <array>
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
    std::vector<Preset> presets; // presets.json
    int theme = -1;              // state.json's open theme, an index into the export's themes, or -1
    Model model = Model::Hsv;    // state.json's model (HSV when it names none)
};
bool picker_load(const std::string& data_dir, Export& ex, Launch& out, std::string& err);

// the platform's log line (logcat tag warptempo_picker on the device, stderr on the laptop)
void plog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

struct ColourState {
    Rgb rgb;
    Model model = Model::Hsv;     // the model the panel shows (the model switch; every element's alike)
    Model dialled = Model::Hsv;   // the model shown when the colour last changed: the view a commit stores
    double h = 0, s = 0, v = 0;   // the HSV view: degrees 0..360, 0..1, 0..1
    double hsl[3] = {0, 0, 0};    // the HSL view: degrees, 0..1, 0..1
    double lch[3] = {0, 0, 0};    // the LCh view: 0..100, 0..kChromaMax, degrees
    double ring[3] = {0, 0, 0};   // the HSV the ring and the triangle show: the HSV view, or in HSL the HSL view's own
                                  // HSV (the same hue), or the pen's HSV after a ring or triangle drag
    // EVERY VIEW ALWAYS GIVES THE BYTES. The one a change sets is exact; the others are re-derived from the bytes at
    // once, each keeping its own hue through grey (and HSV's saturation through black, HSL's through black and white),
    // an HSV or HSL view re-derived after the other's change taking that one's hue for a grey.

    std::array<double, 3> view(Model m) const;
    // the bytes as given (the R / G / B tracks, a theme's swatch); every view re-derived from them
    void set_rgb(Rgb c);
    // the HSV view as given (each number clamped to its range), the bytes from it
    void set_hsv(double hh, double ss, double vv);
    // the shown model's three numbers as given (clamped to the ranges; an LCh view the caller keeps inside the gamut),
    // the bytes from them; the same numbers again change nothing
    void set_view(std::array<double, 3> x);
    // the shown model's axis (0..2) toward `target` (clamped to its range): there, or in LCh, when the target lies
    // outside the gamut, the last in-gamut value on the way from the current one (picker.h's head: the walk)
    void move_axis(int axis, double target);
    // the ring and the triangle: an HSV
    void set_ring(double hh, double ss, double vv);
    // the model switch: the panel shows m (no colour, no view changes)
    void show(Model m);
    // a stored pick: its bytes with its view as saved, or (a pick saved before views were stored) set_rgb's
    void restore(const Pick& p);
    // the state as a pick, the dialled view included: what a commit stores
    Pick pick() const;
    // the state shows the pick: the same bytes and, when the pick carries a view, the same view in its model
    bool shows(const Pick& p) const;

private:
    void put(Model m, const std::array<double, 3>& x);
    void derive(Model m, Model from);   // m's view from the bytes, retaining its hue (from's for a grey, both HSV / HSL)
    void ring_follow();
};

// THE READOUT, the value a slider row shows in its field (rows 0..2 the model's, 3..5 R, G, B): the model's numbers to
// ONE DECIMAL, rounded to nearest -- HSV's and HSL's hue in degrees and the rest in percent ("247.3", "47.1",
// "100.0"), LCh's L, C and h as they are -- so the number shows where the view actually is (architect 2026-10-04, as
// GIMP's fields read; the view is exact doubles, and the − / + still step whole units from the rounded whole number);
// R, G, B their whole bytes
std::string row_readout(const ColourState& cs, int row);

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
    bool models_open() const { return models_; }
    Model model() const { return el_[size_t(active_)].cs.model; }
    bool panel_on_right() const { return right_; }
    Rgb old() const { return old_.rgb; }
    bool edited() const;          // the state does not show the cursor's entry (ColourState::shows), or no history
    bool back_enabled() const;
    bool forward_enabled() const;
    bool presets_open() const { return presets_; }
    bool paste_enabled() const { return has_clip_; }   // something has been copied (picker.h's head: COPY AND PASTE)
    const Pick& clip() const { return clip_; }
    const std::vector<Preset>& presets() const { return presets_list_; }
    std::string save_label() const;      // "Save as Preset N"
    int pop_scroll() const { return pop_.pos; }
    int theme_open() const { return theme_; }   // the strip's theme, or -1
    int strip_scroll() const { return strip_.pos; }
    double strip_x() const;              // the strip's left edge, window px
    // the strip's row i (the theme's colour i): its top in window px at the current scroll, and its height
    double strip_row_y(int i) const;
    int strip_row_h(int i) const { return strip_rows_[size_t(i)].h; }
    double strip_list_y0() const;        // the list's viewport, window px
    double strip_list_y1() const;

private:
    enum class Target { None, Outside, Ring, Triangle, Track, Minus, Plus, Old, Back, Forward, Name, Row, OffChooser, Picture,
                        Presets, PopSave, PopList, OffPopup, StripClose, StripList, ModelBtn, ModelRow, OffModels, Copy,
                        Paste };
    // a scrolling list's state: its offset (content px) and the drag that moves it
    struct Scroll {
        int pos = 0, start = 0;
        double anchor = 0;
        bool dragging = false;
    };
    struct StripRow {
        int y = 0, h = 0;                  // content px, from the list's top
        std::vector<std::string> names;    // the names' lines, wrapped
    };
    void apply_colours();              // every element takes its cs' bytes; the roles follow; the picture repaints
    void close();                      // the one save: commit an edited colour, else rewrite state.json alone
    void append_pick(int e, bool log = true);   // picks.txt, e's history's end, its cursor to it, the logcat line
    void choose(int e);                // the chooser's pick: the close for the element left, then the panel on e
    void set_model(Model m);           // the model switch's pick: every element (and OLD) shows m; state.json
    void write_state() const;          // state.json: every element's colour, view and cursor, the active element, the theme
    void open_presets();               // the pop-up: the close for the panel's edit first
    void save_preset();
    void load_preset(int i);
    void open_theme(int t);            // the strip on theme t
    void close_theme();
    void layout_strip();               // the strip's title lines and rows for theme_ (text measured once)
    void write_presets() const;
    void pop_act(int item);            // a tap on the pop-up list's item
    int pop_item_at(double ly) const;  // the pop-up list's item under the panel-relative y, or -1
    int pop_items() const;             // presets, the heading, the themes
    int pop_max() const;               // the list's furthest scroll
    int strip_row_at(double y) const;  // the strip's row under the window y, or -1
    int strip_max() const;
    void scroll_move(Scroll& sc, double y, int max);
    void copy_colour();                // COPY: the active element's colour and view to the clip
    void paste_colour();               // PASTE: the clip onto the active element, an edit
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
    bool models_ = false;              // the model switch's list is open
    Pick clip_;                        // the copied colour and view, while has_clip_ (never saved)
    bool has_clip_ = false;
    Target target_ = Target::None;
    int row_ = -1;                     // the slider row a Track / Minus / Plus press holds, the chooser row a Row press
    double down_x_ = 0, down_y_ = 0;

    bool presets_ = false;             // the pop-up is open
    std::vector<Preset> presets_list_;
    Scroll pop_, strip_;
    int item_ = -1;                    // the pop-up item or strip row a PopList / StripList press holds
    int theme_ = -1;                   // the open theme strip's theme, or -1
    std::vector<std::string> strip_title_;   // the header's key lines
    std::vector<std::string> strip_sub_;     // and its display title's, small, under them
    std::vector<StripRow> strip_rows_;
    int strip_content_ = 0, strip_head_ = 0;   // the rows' height, the header's (the list starts under it)

    // caches: the hue ring (never changes) and the triangle at tri_h_
    std::vector<uint32_t> ring_;       // kWheel x kWheel, 0 = not ring
    std::vector<uint32_t> tri_;        // kWheel x kWheel, 0 = not triangle (at the ring's hue tri_h_)
    double tri_h_ = -1;
};

// the panel's whole geometry, device px (the tablet's 2304 x 1440), relative to the panel's top-left
namespace panel {
constexpr int kW = 1120, kH = 1352, kMargin = 16, kPad = 36;
constexpr int kWheel = 580, kROut = 290, kRIn = 220, kRTri = 210;
constexpr int kColX = kPad + kWheel + 44, kColX1 = kW - kPad;            // the hex and swatch column
constexpr int kNameY = kPad, kNameH = 64;                                 // the element button, the column's top
constexpr int kNameX1 = kColX + 264;                                      // the element button kColX..kNameX1
constexpr int kPresetsX = kNameX1 + 12;                                   // the presets button kPresetsX..kColX1
constexpr int kChooserY = kNameY + kNameH + 8, kChooserRowH = 76;         // the chooser's rows, under the button
// THE CHOOSER'S FIT (architect 2026-10-05: the flag kinds made sixteen elements, and the clock's and the card's roles
// may join them): TWO COLUMNS over the panel's whole width inside its pad, as the presets pop-up lies under the two
// buttons, read down the first column and then the second (Windows 95's list view in its List mode), each column
// ceil(n / 2) rows of the pop-up's and the model list's height running down from under the button over the wheel and
// the slider rows as far as the panel's pad, painted over everything they cover (the wheel, a readout, a handle, a
// field; Picker::paint). kChooserMax entries fit (30: 15 rows, 108..1248 of the panel's 1352), and picker_load refuses
// an export with more; sixteen end at 716, twenty-two at 944
constexpr int kChooserX0 = kPad, kChooserX1 = kColX1, kChooserCols = 2;
constexpr int kChooserColW = (kChooserX1 - kChooserX0) / kChooserCols;   // 524
constexpr int kChooserMax = kChooserCols * ((kH - kPad - kChooserY) / kChooserRowH);
static_assert(kChooserMax >= 22, "the chooser holds at least twenty-two entries inside the panel");
// an entry's cell: its column's left edge and its row's top, panel px, for n entries
constexpr int chooser_rows(int n) { return (n + kChooserCols - 1) / kChooserCols; }
constexpr int chooser_x(int e, int n) { return kChooserX0 + e / chooser_rows(n) * kChooserColW; }
constexpr int chooser_y(int e, int n) { return kChooserY + e % chooser_rows(n) * kChooserRowH; }
// in a cell: the active mark kChooserMarkX..+16 inside its left edge, the name from kChooserNameDX, ending as far
// inside the cell's right edge as the mark stands inside its left; a longer name is set smaller (fit_px)
constexpr int kChooserMarkX = 22, kChooserNameDX = 60, kChooserNameW = kChooserColW - kChooserNameDX - kChooserMarkX;
constexpr int kHistY = kPad + 176;                                        // BACK | N of M | FORWARD, under the hex
constexpr int kSwatchY0 = kPad + 324, kSwatchY1 = kPad + kWheel;
constexpr int kSwatchW = 200, kNewX = kColX1 - kSwatchW;
// THE MODEL SWITCH: a button the element button's height under the wheel, at the left over the three tracks it
// switches, its list (the chooser's rows) under it over the tracks
constexpr int kModelX0 = kPad, kModelX1 = kPad + 180, kModelY = kPad + kWheel + 20, kModelH = kNameH;
constexpr int kModelListY = kModelY + kModelH + 8;
// COPY AND PASTE: the model switch's row and height, under the swatches, each a swatch's width in its column (COPY
// under OLD, PASTE under NEW), so they read as the swatch pair's colour's; the 20 px above them the wheel's gap to the
// model switch, the slider rows from kRowsY under them
constexpr int kClipY = kModelY, kClipH = kModelH;
constexpr int kCopyX0 = kColX, kCopyX1 = kColX + kSwatchW, kPasteX0 = kNewX, kPasteX1 = kColX1;
constexpr int kRowsY = kModelY + kModelH + 20, kRowStep = 100, kGroupGap = 20, kRowH = 76;
constexpr int kLabelX = kPad, kMinusX = kPad + 50, kBtn = 76;
constexpr int kTrackX = kMinusX + kBtn + 14, kTrackL = 642, kTrackH = 56;
constexpr int kPlusX = kTrackX + kTrackL + 14, kFieldX = kPlusX + kBtn + 16, kFieldW = kColX1 - kFieldX;
// the numbers' size (the readouts and the history's count); a readout ends kReadoutInset px inside its field's
// right edge. The six fields are one width, kFieldW (160), sized for the widest readouts "360.0" and "100.0" with air
// on both sides (architect 2026-10-04: H, S, V to one decimal); the track gave up the width.
constexpr double kNumPx = 38;
constexpr int kReadoutInset = 14;
constexpr int kBackX = kColX, kFwdX = kColX1 - kBtn;                      // the history's buttons, kBtn square
constexpr int row_y(int i) { return kRowsY + i * kRowStep + (i >= 3 ? kGroupGap : 0); }
static_assert(row_y(5) + kRowH + kPad == kH, "the panel ends kPad under the last row");
static_assert(kClipY > kSwatchY1 && kClipY + kClipH < row_y(0) - 12 && kCopyX0 > kModelX1 + 12 && kCopyX1 < kPasteX0 &&
                  kPasteX1 <= kW - kPad,
              "COPY and PASTE stand clear of the swatches, the model switch, each other and the slider rows' hit bands");
// THE PRESETS POP-UP, under the two buttons over the whole panel inside its pad (the theme names are long): its fixed
// save line, then the list's viewport down to the pad; rows the chooser's height
constexpr int kPopX0 = kPad, kPopX1 = kColX1, kPopY0 = kChooserY, kPopY1 = kH - kPad, kPopRowH = kChooserRowH;
constexpr int kPopListY0 = kPopY0 + kPopRowH;
constexpr int kPopSw = 44, kPopSwGap = 8, kPopSwInset = 24;               // a preset's / theme's swatches, right-aligned
constexpr int kSlop = 16;                                                 // a drag past it scrolls, and acts on nothing
// THE THEME STRIP: a column kStripW wide, kStripGap from the panel on the scene's side, the panel's height; its close
// control kBtn square at the top right, the name left of it; each colour a kStripSwW x kStripSwH swatch and, right of
// it, its hex and the names that record it (wrapped), rows kStripRowGap apart
constexpr int kStripW = 360, kStripGap = 12, kStripPad = 20;
constexpr int kStripSwW = 88, kStripSwH = 64, kStripTextX = kStripPad + kStripSwW + 14, kStripRowGap = 12;
constexpr double kStripTitlePx = 28, kStripTitleLineH = 34, kStripNamePx = 18, kStripHexPx = 20, kStripLineH = 24;
} // namespace panel

// a plain message on the ground (the scene missing or malformed): each line in the sans, white
void paint_message(cairo_surface_t* frame, const std::vector<std::string>& lines);
