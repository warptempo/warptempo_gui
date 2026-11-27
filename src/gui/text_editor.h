#pragma once

#include "gui_input.h"
#include "phaseresetmarkers.h"  // kIterHopMax — the hop cap's bound, taken
                                // from its one owner rather than re-spelled
#include "value_format.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Generic in-place text editor for a small constrained vocabulary, used
// by the top-flag editor. State is a POD-ish struct so callers can
// keep it inline on AppState. Validation, commit semantics, and visual
// rendering are the caller's concerns; this module only handles the
// keyboard-driven mutation of `pending` + cursor state and exposes hooks
// for blink and the refused commit's blink restart (refuse).
//
// Reuse: the several editor surfaces supply different
// validators and writers but reuse this state shape and keystroke routing.
//
// THE TEXT IS UTF-8 (architect 2026-08-02). `pending` holds any well-formed UTF-8 the typed path or the filter
// admits, and the free-text settings fields round-trip it; the STRUCTURAL
// grammars this editor feeds (labels, numeric spellings, settings keys) stay
// ASCII, but that is their validators' business at commit and not this
// module's. Three properties make it work:
//
//   - INDICES STAY BYTE INDICES. `pending`, the shaped run's per-byte pen
//     offsets (text_shape::byte_offsets_px) and the click-to-byte search all
//     address by byte, so there is exactly one coordinate and nothing to
//     convert between.
//   - EVERY STEP MOVES A WHOLE CODEPOINT. BackSpace, LEFT and RIGHT and the
//     pointer's caret placement all land on codepoint boundaries, so
//     `cursor_pos` and `selection_anchor` are boundaries by construction and
//     no operation can split a character.
//   - THE VOCABULARY IS THE ON-SCREEN KEYBOARD'S (architect 2026-10-30, its
//     one producer): its characters, Backspace, Enter, Esc, Tab (the
//     completion's, upstream of this module), Copy and Paste
//     (Ctrl+C / Ctrl+V, the two keys' own ctrl), and LEFT and RIGHT
//     (2026-11-08) — the caret, and with the latched
//     Shift the selection. (The keyboard's pass key, UP and DOWN are the
//     marker lane editor's own whole-text acts, architect 2026-11-20 / 21,
//     through this module's incoming filter but not its keymap —
//     GuiFlagEditor's step_editor_text and toggle_inherit_editor_text.)
//     THE BARREL HAS NO ROAD ON ANY
//     KEY, THE KEYBOARD'S INCLUDED (architect 2026-11-08): no word erase,
//     no Ctrl+A and no Ctrl+X. It has no Home / End and
//     no Delete. THE SELECTION COMES FROM THE KEYS ALONE (architect
//     2026-11-08): the field's tap seats the caret and its drag
//     moves it, collapsing (begin_touch_caret_drag's declaration,
//     input_handler.h); a refused Enter leaves the selection as it stands
//     (refuse).
//   - GRAPHEME CLUSTERS ARE OUT OF SCOPE, deliberately and in the same class as
//     the no-bidi and no-font-fallback exclusions: a combining mark is its own
//     codepoint, so it carets and deletes separately from the base character it
//     sits on. Accepted — the free-text fields are provenance notes, not a text
//     processor.
//
// Rendering follows from the same one face: an uncovered glyph draws .notdef
// (no fallback), and shaping is single-direction LTR.

namespace text_editor {

// EVERY CAP BELOW IS IN BYTES — `pending.size()`, which is what every cap test
// reads. With UTF-8 admitted a multi-byte character spends the bytes it costs,
// so a field's cap is a storage bound rather than a character count. The names
// say Chars; nothing measures characters anywhere in this module.
//
// EVERY CAP IS ITS GRAMMAR'S WIDEST SPELLING (architect 2026-09-05): a field
// whose cap is wider than what it can legally commit advertises longer typing
// than it allows, which is the truthfulness defect the roster answers. THREE
// CAPS ARE TIGHT BOUNDS and each derives its own — the flag payload, the
// iteration bound, and the BPM form's three fields (architect 2026-10-29;
// the beats field admits no leading zeros, so it has a widest spelling like
// every other field of that grammar). THE SETTINGS VALUE AND THE TRIM
// PRESET'S NAME ARE POLICY CEILINGS and say so where they stand: FREE TEXT,
// with no widest spelling to be tight against.
//
// Maximum bytes allowed in `pending`, and it belongs to the FLAG PAYLOAD
// editor alone: every other Kind names its own cap below, so this default is
// what FlagPayload reads and nothing else does.
//
// THE DERIVATION. The commit runs `parse_single_canonical_line` — the very
// parse the load runs (warpmarkers_parse.cpp owns the grammar) — so the
// widest committable payload is the widest LOADABLE one. The forms are
// `pass`, `x.yz`, `pass:x.yz`, `N.NN`, `N.NN*SCALE`, `N.NN:x.yz` and
// `N.NN*SCALE:x.yz`, each numeric form optionally carrying a DEVIATION CHAIN
// after its base, so the widest is BASE + CHAIN + `*` + SCALE + `:` + LABEL:
//   BASE    4 bytes — `4.00`. parse_tempo_cents pins exactly the N.NN
//           spelling and the bracket pins the integer part to one digit at
//           kTempoMaxCents; the assert below keeps that honest across a
//           bracket retune.
//   CHAIN  80 bytes — kMaxTempoDeviationTerms terms at FIVE each
//           (architect approval 2026-09-18, the cap raised to sixteen terms
//           2026-09-19). A term is a mandatory sign and the same N.NN
//           magnitude (parse_deviation_cents, value_format.h), whose integer
//           part is pinned to one digit by the term's own wall
//           ±kIterDeltaMaxCents — the second assert below keeps THAT honest.
//           The bound is attained: sixteen terms all spelled `-4.00` is a
//           legal chain whenever the base leaves room for the total to stay
//           in bracket, and the cap must admit the typing even where the
//           WALLS will then refuse the value, since a cap narrower than the
//           grammar hides the field's own refusal behind a full-field card.
//   SCALE  18 bytes, and this is the term that had to be ANSWERED rather
//           than assumed, the scale being a full double. THE READER IS NOT
//           FREE: parse_positive_value's last arm refuses any spelling that
//           is not the WRITER'S (`format_value_double(v, 4) != s`), and
//           parse_value_double refuses every alphabetic byte before it. So
//           no exponent form, no `01.5000`, and no long digit string that
//           canonicalizes shorter — `1.000000000000000000001` parses and is
//           then refused for not being its own canonical spelling. What is
//           left is exactly the shortest round-trip spelling of a double
//           inside [kScaleMin, kScaleMax] padded to four decimals: one
//           integer digit, the point, and at most SIXTEEN fraction digits —
//           sixteen because the bracket's own ulp is at least 2^-53, wider
//           than 1e-16, so a sixteen-decimal value always sits inside a
//           double's rounding interval there. The bound is attained, at
//           `0.5000000000000001`.
//   LABEL   4 bytes exactly (is_valid_label_format: a lowercase letter, the
//           dot, two lowercase alphanumerics).
// 4 + 80 + 1 + 18 + 1 + 4 = 108, spelled whole:
// `4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00-4.00*0.5000000000000001:a.aa`
// (the base and then SIXTEEN terms — seventeen `4.00` runs in all).
// A TIGHT BOUND, not a policy cap.
//
// An operation that would grow the pending past this cap refuses atomically
// (the whole edit lands or the buffer is untouched);
// shrinking and non-growing edits are always accepted. THAT SHRINK LATITUDE
// IS NEVER EXERCISED HERE, and the roster's honest home for it is the
// settings value (kMaxPendingCharsSettings below), whose free text does load
// wider from disk than the editor will grow: this field's seed is composed
// from the store (flag_text, flag_editor.cpp) and the store's every road —
// the load, the history walk, the load in place — comes through the strict
// canonical-line parser that this cap is derived from, so no over-cap
// payload can reach the editor at all.
static_assert(kTempoMaxCents < 1000,
              "a tempo's integer part fits one digit, as kMaxPendingChars's "
              "derivation assumes");
static_assert(kIterDeltaMaxCents < 1000,
              "a deviation term's integer part fits one digit, as "
              "kMaxPendingChars's derivation assumes");
constexpr int kMaxPendingChars = 108;
// The ITERATION BOUND editor (a double tap, or Enter — EDIT on glass — on
// one of the two bound cells a flag grows in iteration mode, the addressed
// cell). ITS GRAMMAR IS FIXED-WIDTH: a sign,
// one integer digit, the point, two decimals (format_signed_delta_cents,
// warpmarkers.h — the sign is the cells' whole syntax). The integer digit is
// single because the walls the commit and the arrows' step share — the tempo
// window, [kTempoMinCents − base, kTempoMaxCents − base] — hold every bound
// inside kTempoMaxCents - kTempoMinCents, i.e. 3.75. So `-3.75` is the widest token
// the field can commit and FIVE BYTES is the cap, a tight bound rather than a
// policy one; the assert keeps the derivation honest across a bracket retune.
// THE CAP AND THE JUDGE AGREE EXACTLY: the commit's reader
// (parse_signed_2dp_cents, flag_editor.cpp) owns that grammar and takes those
// five bytes and nothing else — no surrounding whitespace, no leading zero,
// no second integer digit — so the cap advertises exactly the spellings the
// commit accepts, and a pending arriving by any other road is judged the same.
// (The sixth character is refused by the field-full card, the truthful
// refusal for a field that could never commit one.)
static_assert(kTempoMaxCents - kTempoMinCents < 1000,
              "an iteration bound's integer part fits one digit, so five "
              "bytes hold its widest spelling");
constexpr int kMaxPendingCharsIterBound = 5;
// THE SAME KIND ON THE PHASE-RESET COLUMN (2026-09-09), whose bracket is a
// count of LATTICE HOPS rather than a tempo delta: its grammar is EXACTLY a
// sign and one digit (format_signed_hops, warpmarkers.h), because a bound is
// capped at kIterHopMax — the architect's single digit, "past nine it is no
// longer the phase reset it was". So TWO BYTES is this grammar's widest
// spelling and its cap, a tight bound like its sibling's, and the assert keeps
// the derivation honest if the digit ever stops being one. THE CAP AND THE
// JUDGE AGREE EXACTLY here too: the commit's reader (parse_signed_hops,
// flag_editor.cpp) takes those two bytes and nothing else — no whitespace, no
// bare digit, no double sign — so the cap advertises exactly the spellings the
// commit accepts. WHICH OF THE TWO CAPS A SESSION SPENDS is State::iter_hops
// below: ONE Kind serves both columns, and this module cannot see the app to
// ask which column is live.
static_assert(kIterHopMax < 10,
              "a hop bound's magnitude fits one digit, so two bytes hold its "
              "widest spelling");
constexpr int kMaxPendingCharsIterHop = 2;
// THE BPM FORM'S FIELDS (architect 2026-10-29), ONE CAP PER FIELD, the
// session's `target` naming the field (GuiBpmField, warpmarkers.h — the
// field's key and its reader there are the grammar's one owner). THE BUFFER
// IS THE FIELD'S VALUE ALONE (architect 2026-11-19: "I agree with
// your recommendation" — the line editor shows the value under the field's
// label), the key the row's own and never typed, so each cap
// is its value's widest spelling:
//   BEATS  4 bytes, a positive integer with no leading zeros, capped at
//          kBpmBeatsMax — so `9999` is the widest there is.
//   LOW / HIGH  18 bytes each. Each is pinned by an equality test to the bpm
//          writer's own spelling (format_value_double at 0 decimals, i.e.
//          to_chars' shortest round-trip form) inside [kBpmMin, kBpmMax] =
//          [10, 400]. A shortest round-trip double never needs more than
//          SEVENTEEN significant digits, and in that bracket every digit of
//          the fixed spelling is significant (the leading integer digit is
//          non-zero, and there are two or three of them), so the spelling is
//          at most seventeen digits plus the point; to_chars picks the
//          scientific form only where it is SHORTER, so 18 bounds both. The
//          bound is attained, at `10.000000000000002` — the double one ulp
//          above 10, whose rounding interval (half an ulp, 2^-50 wide) holds
//          no sixteen-digit decimal.
// So 4, 18 and 18, each attained.
// PER FIELD AND NOT THE WIDEST OF THE THREE because each session edits one
// field whose spellings are known: a shared 18 would let a beats value grow
// fourteen bytes past anything its commit admits, the advertised-typing
// defect this module's head rules out.
static_assert(kBpmBeatsMax < 10000,
              "a beats count fits four digits, the BEATS field's cap");
static_assert(kBpmMin >= 1.0 && kBpmMax < 1000.0,
              "a bpm bound has at most three integer digits, the LOW / HIGH "
              "cap's derivation");
constexpr int kMaxPendingCharsBpmField[3] = {
    4,    // 9999
    18,   // 10.000000000000002
    18,   // 10.000000000000002
};
// Settings prompt. A POLICY CEILING — free text has no widest spelling to be
// tight against. Sized for the free-text provenance fields (url, notes),
// which are generous for a typical URL or note. The scalar keys are still
// bounded by their commit-time validators, not by this cap. THE CAP BOUNDS
// THE VALUE ALONE, the entry holding no key (architect 2026-11-19: "One,
// agree."): 1024 bytes of value, never 1024 less a key and `=`.
// No file reader caps a value's length, so a wider on-disk value still
// loads, shows on the settings page's row and persists unchanged; the
// editor's prefill of it, a growth past the cap through the one incoming
// filter, is refused silently (the recall's rule, GuiSettingsEditor::open)
// and the entry opens empty.
//
// THE LINE EDITORS' ENTRY IS ONE LINE AND SCROLLS SIDEWAYS, GTK'S WAY
// (architect 2026-11-19: "The pen does have a good way to scroll,
// and those are the left and right arrows of the keyboard. That's exactly
// why they're there."), so a value as long as this cap travels through the
// value sub-page's entry on `view_offset_px` below (paint_handler.cpp's THE
// VALUE SUB-PAGE).
constexpr int kMaxPendingCharsSettings = 1024;
// THE TRIM PRESET'S NAME (architect 2026-10-28 night). A POLICY CEILING like
// the settings value's — a name is free text with no widest spelling — sized
// for a few words in any script: 128 bytes. The name grammar's reader asks
// the same filter and so the same cap (trim_preset_name_refusal,
// trim_presets_file.h), so a name the file holds is a name this editor could
// have committed.
constexpr int kMaxPendingCharsTrimPresetName = 128;

// Vocabulary the editor accepts on the keyboard. Different call sites
// edit different payload shapes; the kind now selects only the length cap
// (handle_key accepts any character-bearing key and defers grammar to the
// commit-time validator). The flag editor uses FlagPayload (the plain
// canonical payload — tempo, scale, labels, never a bracket); the BPM
// form's line editor uses BpmField (the value of one of the form's three
// fields); the settings-prompt editor uses SettingsAssignment (the value of
// the settings page row's key, the value alone, architect 2026-11-19:
// "One, agree."; the key the open's, never typed); the trim preset's name
// editor uses TrimPresetName (free
// text, the name); and the ITERATION BOUND editor uses IterBound (the text of one of the two
// bound cells a flag grows in iteration mode — which of the two is
// State::iter_upper below — judged at its commit against the bracket's walls).
// ONE KIND, TWO GRAMMARS (2026-09-09): a warp marker's cell holds a signed
// TWO-DECIMAL CENT bound and a phase reset's a signed WHOLE HOP, and
// State::iter_hops says which — one kind because the two are the same surface,
// the same open, the same modal contract and the same commit route, differing
// only in what the bytes mean, exactly as the side bit differs in which bound
// they name. THERE ARE FIVE KINDS AND THREE OF THEM ARE
// THE LINE EDITORS OF THE VALUE SUB-PAGE, "the dialog editors" in the names
// (SettingsAssignment, TrimPresetName, BpmField), ALL THREE
// ON THE SETTINGS EDITOR'S BUFFER (AppState::settings_editor; architect
// 2026-10-29); the two top-strip kinds (FlagPayload,
// IterBound) share the flag editor's State and paint in the marker lane.
// BpmField (architect 2026-10-29) is THE BPM FORM'S LINE EDITOR, a row's tap
// on the form (GuiInputHandler::bpm_form_commit): opened prefilled with the
// row's value (architect 2026-11-19), `target` the field
// (GuiBpmField, warpmarkers.h), its commit
// the field's (GuiFlagEditor::commit_bpm_field), its close returning to the
// form. TrimPresetName (architect
// 2026-10-28 night) is THE TRIM PRESET'S NAME EDITOR, the trim presets list's
// Save As (GuiInputHandler::trim_presets_save_as, which Save's press on an
// empty list runs too) and Rename (trim_presets_rename, architect
// 2026-11-25), one open (open_trim_preset_name_editor): it rides the settings
// editor's State (AppState::settings_editor, the line editor's one
// buffer), the settings editor's shape — the value sub-page's entry, the
// on-screen keyboard's Enter and Esc — opened empty under Save As and
// holding the preset's name under Rename, its commit the
// name's (trim_preset_name_refusal, trim_presets_file.h). IterBound
// (2026-09-05) is every bound cell's own editor, the cell a mini flag. The
// `h` view's load and Open Project type nothing (architect 2026-08-28:
// "we're not allowing free-form typing there"): each is the FIELD-LESS
// PICKER over the folder overlay (AppState::Picker, app_state.h),
// which is a modal owner and not an editor. THIS ENUM IS THE AUTHORITATIVE
// LIST of the editors, and EVERY ONE IS A MODAL (2026-10-25): the
// modality predicate (GuiInputHandler::keyboard_modal_editor_active) reads
// AppState::text_editor_session, which covers all five kinds;
// AppState::dialog_editor_session names the three LINE kinds
// (SettingsAssignment, TrimPresetName and BpmField, all on the settings
// editor's buffer) for the questions only the value sub-page has.
enum class Kind {
    FlagPayload,
    SettingsAssignment,
    IterBound,
    TrimPresetName,
    BpmField,
};

// THE MODAL SESSION ID SOURCE — one monotonic counter for the whole program,
// handing out an id that names exactly ONE raise of exactly ONE modal surface
// for the life of the process (it starts at 1, so 0 is reliably "no session").
//
// IT IS HOMED HERE because the text editors are the most numerous of the
// product's modal surfaces and `enter` below is their one activation route,
// which is what makes the stamping STRUCTURAL rather than disciplinary: no
// opener can forget to take an id, and each new dialog editor has inherited
// the identity for free, not touching this counter.
// THE SURFACES THAT ARE NOT EDITORS take their ids from this same counter at
// their own one raise route each — every non-editor member of
// AppState::ModalDialogOwner (app_state.h), which is the modal surfaces' ONE
// authoritative enumeration and where a new one is added: the PROMPT
// (PromptState::present, app_state.h), the RENDER PLAYER
// (GuiRenderPlayer::open), the PICKER
// (GuiInputHandler::open_project_picker), the PROGRESS PAGE
// (GuiInputHandler::open_batch_progress) and the SCORE VIEWER
// (GuiScoreViewer::open).
// So the ids never collide across the classes and one integer compare answers
// "is this published geometry the surface that owns input right now" (the
// doctrine and the comparison's one owner are at
// AppState::ModalDialogGeometry and
// GuiInputHandler::modal_dialog_stash_current).
//
// THE COUNTER IS THE PROCESS'S, NOT A PROJECT'S: it is a function-local static
// that lives for the life of the process and only ever grows, so ids never
// repeat across the reopens gui_main's loop runs (main.cpp) — a geometry
// published under one project's session can never be mistaken for a surface
// raised under the next.
uint64_t next_session_id();

// State for a single editable rect.
struct State {
    // Identifier of the entity being edited. -1 means "not editing".
    // The caller decides what this means (a marker index for the flag
    // editor; 0 for the settings value and the trim preset's name; the field,
    // GuiBpmField's value, for a BpmField session, whose byte cap reads it —
    // kMaxPendingCharsBpmField).
    int target = -1;

    // THIS EDITING SESSION'S ID, taken from next_session_id() above at every
    // `enter` and never rewritten while the session stands (deactivate leaves
    // it alone — `target` is what says "not editing", and a dead session's id
    // must not be reusable). The TOP-STRIP FLAG editor takes one too and
    // nothing ever reads it: it publishes no dialog, so it has no geometry to
    // validate. What the DIALOG editor's id is for is at
    // AppState::ModalDialogGeometry.
    uint64_t session = 0;

    // Vocabulary discriminator. The caller sets this in `enter()` and
    // the keystroke handler routes printable detection accordingly.
    Kind kind = Kind::FlagPayload;

    // WHICH BOUND AN IterBound SESSION EDITS: false the lower cell, true the
    // upper. A property of the session beside `target`, set at `enter()` and
    // meaningless on every other kind (the flag editor derives the cell from
    // it through iter_bound_editor_side, app_state.h, so no reader spells
    // the bool's meaning twice).
    bool iter_upper = false;

    // WHICH GRAMMAR AN IterBound SESSION SPEAKS: false the warp column's
    // signed two-decimal cent bound, true the phase-reset column's signed
    // whole hop. Set at `enter()` beside the side bit and meaningless on every
    // other kind. IT EXISTS FOR THIS MODULE ALONE, which selects the byte cap
    // and cannot see AppState to ask which column is live — every other reader
    // (the open, the commit, the painter) reads app.active_markers_view
    // directly, because the view CANNOT
    // MOVE under an open session and so no session needs a stored column.
    bool iter_hops = false;

    // Editable text — the whole of what the editor holds and the whole of what
    // its painter draws. A surface whose value has uneditable neighbours (the
    // marker's position and disabled bit for the flag payload) leaves them to
    // the marker's own fields and to the painter; this buffer never carries
    // them.
    std::string pending;

    // Byte index into `pending`, always on a UTF-8 CODEPOINT BOUNDARY (the
    // invariant and the routes that maintain it are at the head of this file).
    // Clamped to [0, pending.size()].
    int cursor_pos = 0;

    // -1 means no selection. When >= 0, the selected range is
    // [min(selection_anchor, cursor_pos), max(...)). Set by a shifted LEFT /
    // RIGHT (the anchor where the caret stood, the free end cursor_pos);
    // cleared by a pointer caret placement or drag, an unshifted LEFT /
    // RIGHT, any printable insertion, any backspace, a whole-text rewrite
    // (the flag editor's extra keys, which seat the caret at the text's end),
    // and by enter/deactivate. A refused Enter leaves it as it stands
    // (refuse).
    int selection_anchor = -1;

    // HORIZONTAL VIEW OFFSET, in pixels: how far the visible text window has
    // scrolled right through `pending`. Byte 0 paints at (text origin -
    // view_offset_px), so a positive value hides text off the field's left
    // edge.
    //
    // ITS PAINTER WRITES IT AND READS IT, ONE PER EDITOR STATE: the marker
    // lane's (render_flag_editor_box) for the flag editor, and the value
    // sub-page's (paint_handler.cpp's paint_value_page; architect
    // 2026-11-19) for the line editor,
    // whose one-line entry scrolls sideways, GtkEntry's adjust_scroll: the
    // offset clamped to the run's own travel past the entry's inner width,
    // then moved only when the caret would leave that width, by just enough.
    // THE MARKER-LANE FIELD'S TRAVEL IS SUB-PIXEL (2026-09-06): its box
    // is its own two pads and its own
    // run — no lane cap, no on-window clamp — so the whole run is always
    // inside it and the travel it asks for is SUB-PIXEL, the box having
    // rounded the shaped width down to a whole column and left the caret's
    // reserved column on the clip's right edge. A field cut off at a window
    // edge is not this field's business: the box stands where its box stands
    // and the VIEWPORT PAN is what brings it into view (render.h's
    // render_flag_editor_box carries the ruling).
    //
    // THE MINIMAL-TRAVEL RULE, which both painters apply and may not
    // reinvent: each frame, scroll only as far as the caret demands and in
    // whichever direction it left the window, and keep the view inside the
    // run's own travel (so the view never shows blank space past the end of
    // the text). A caret walking right pushes the view right one glyph at a
    // time and walking back left pulls it back the same way — it never
    // jumps, and it never moves at all while the caret sits inside the
    // window. The marker lane reserves the caret's own column inside its
    // viewport; the dialog entry lets the caret stand on its inner right edge,
    // its one W in the pad, as GtkEntry draws it.
    //
    // THE CLICK-TO-BYTE PATH NEEDS NO TERM FOR IT: each painter folds the
    // offset into the ORIGIN it publishes (FlagEditorBox::text_origin_x,
    // AppState::DialogEditorText::origin_x, "the window x byte 0 paints at"),
    // so editor_stop_at's nearest-boundary search and every caret seat it
    // serves land correctly on a scrolled field with no knowledge that it
    // scrolled.
    //
    // It lives on State rather than beside either geometry because it is
    // SESSION state: it must survive from frame to frame — recomputing it from
    // nothing each paint would jitter a caret resting mid-string — and must
    // die with the edit. enter/deactivate already own exactly that lifetime
    // and are its only reset sites; a change of which editor a dialog hosts is
    // structurally a change of State, so nothing else needs keeping in step.
    double view_offset_px = 0.0;

    // Cursor blink: monotonic timestamp at which the cursor became
    // visible. The renderer tests `(now - blink_epoch) % period < period/2`
    // to decide whether to draw the bar this frame.
    std::chrono::steady_clock::time_point blink_epoch =
        std::chrono::steady_clock::now();
};

inline bool is_active(const State& s) { return s.target >= 0; }

inline bool has_selection(const State& s) {
    return s.selection_anchor >= 0 &&
           s.selection_anchor != s.cursor_pos;
}
inline int selection_start(const State& s) {
    if (!has_selection(s)) return 0;
    return std::min(s.selection_anchor, s.cursor_pos);
}
inline int selection_end(const State& s) {
    if (!has_selection(s)) return 0;
    return std::max(s.selection_anchor, s.cursor_pos);
}

// Reset `s` so `is_active` returns false.
void deactivate(State& s);

// A REFUSED ENTER (architect 2026-11-27: "Follow GTK."): THE FIELD STANDS AS
// IT WAS — the text, the caret and the selection untouched — and the blink
// restarts lit (restart_blink), so the caret shows at once, as after any
// keystroke. GTK's way: no widget selects on a refusal; GtkEntry's
// `activate` hands Enter to the application, and the caret and the selection
// stay where they are. THE REFUSING OWNER'S CARD IS THE REFUSAL'S WHOLE
// VISIBLE STATEMENT: no frame or face changes colour (architect 2026-10-03:
// "a red outline and the card is redundant"), so the session carries no
// refusal state. Every commit refusal of every editor calls this; the cap's
// refusal (replace_selection) leaves the buffer and the selection as they
// stand the same way, and its caller's card says why.
void refuse(State& s);

// Begin editing `target` with the given seed pending.
// Cursor lands at end of pending. `kind` selects the vocabulary the
// keystroke handler will accept while this editor is active; `iter_upper`
// names the bound an IterBound session edits and `iter_hops` its grammar (the
// phase-reset column's whole hops rather than the warp column's cents), and
// both are ignored by every other kind.
void enter(State& s, int target,
           std::string initial_pending,
           Kind kind = Kind::FlagPayload,
           bool iter_upper = false,
           bool iter_hops = false);

// Apply a key event to the editor. Returns true if the key was consumed
// — the caller should NOT route a consumed key to other handlers.
//
// Special return-value semantics for commit / discard:
//   - consumed_commit: editor wants the caller to validate-and-commit.
//   - consumed_cancel: editor wants the caller to discard (Esc).
//
// Pure printable characters and Backspace are handled internally and
// reported as plain `consumed`.
//
// Keys are platform-neutral GuiKey values; mods is the GuiInputState
// populated by the platform boundary (see gui_input.h).
enum class KeyAction {
    NotConsumed,
    Consumed,
    CommitRequested,
    CancelRequested,
    CopyRequested,
    PasteRequested,
    // A TYPED CHARACTER THE FIELD HAD NO ROOM FOR (architect 2026-08-30, the
    // strictness ruling): consumed exactly as Consumed is — the buffer and the
    // selection are untouched — but told apart from it so
    // the DISPATCH LAYER can say why. This module has no GuiNotifications and
    // wants none (it is the pure editor over one buffer), so the refusal is
    // REPORTED here and CARDED at route_modal_editor_key, the one place every
    // editor's keys pass through.
    OverCapacity,
};

// The editor's key-classification owner: the single truth of which
// keys the editor consumes, and how. It is side-effect-free (no State), because
// handle_key's MEMBERSHIP — which branch a key enters — depends only on key+mods;
// State decides EFFECTS within a branch (e.g. Ctrl+C returns CopyRequested vs
// plain Consumed by has_selection), never whether a key is owned. Three
// consumers share it: handle_key gates membership on it (NotEditorKey returns
// NotConsumed before any branch), modal_editor_key_blocked admits exactly the
// non-NotEditorKey set plus its gate-level carve-outs, and repeat_eligible's
// in-editor arm repeats exactly MotionEditKey | PrintableKey. Because all three
// read this one function, the strict-modifier rule is stated ONCE: no consumer
// re-enumerates a key.
enum class KeyClass {
    NotEditorKey,   // not the editor's — the keyboard-modal gate drops it
    SessionKey,     // BARE Escape, Return — end the session, never repeat
    ChordKey,       // CTRL-EXACT C/V (Copy, Paste) — one-shot
    // (The pass key is no class of this module's: NotEditorKey, the flag
    // editor's Toggle Inherit on its text, architect 2026-11-20 / 21.)
    // BackSpace, BARE, and LEFT / RIGHT, bare or SHIFT-ONLY (architect
    // 2026-11-08) — repeat. Any other modifier is
    // NotEditorKey.
    MotionEditKey,
    // No ctrl/alt and a character-bearing codepoint: >= 0x20, not DEL, not a
    // surrogate, within Unicode. Not ASCII-limited — the platform resolves a
    // full codepoint and the editor UTF-8 encodes it. Repeats.
    PrintableKey,
};

// Classify key+mods against the editor's owned keymap, reproducing handle_key's
// exact predicates and precedence: the session keys before the Ctrl chords, the
// chords (specific C/V) before BackSpace and the caret keys, those before
// printable. A
// key that could satisfy two predicates classifies as the one handle_key acts
// on (Ctrl+C is a chord, not printable — printable already excludes ctrl). NO
// ARM ADMITS ALT: session and BackSpace are bare-exact, the
// chords ctrl-exact, LEFT / RIGHT bare or shift-only, printable already
// excludes ctrl and alt. So a press wearing a modifier its
// arm does not bind (Ctrl+Escape, Ctrl+Shift+V, Ctrl+BackSpace — no producer
// spells one, the barrel having no road on the keyboard, 2026-11-08) is simply
// NotEditorKey: unbound and
// not-ours need not be told apart, because every keyboard-modal editor drops
// both at the gate and nothing downstream can act on either. An input routing
// decision, judging no data.
KeyClass classify_key(GuiKey key, GuiInputState mods);

KeyAction handle_key(State& s, GuiKey key, GuiInputState mods);

// Clipboard primitives, used by the input handler to bridge the editor's
// selection model to the SYSTEM clipboard. selected_text returns the
// highlighted substring (empty if no selection — and always a whole number of
// codepoints, since both endpoints are boundaries).
//
// replace_selection is the paste primitive, THE ONE INCOMING-TEXT FILTER,
// and THE ONE OWNER OF THE PER-KIND BYTE CAP — every route that grows a
// pending buffer comes through it: the paste (input_handler), the
// settings editor's completion append and its recall prefix
// (settings_editor.cpp), THE TYPED KEYSTROKE, which handle_key encodes and
// hands over rather than repeating the ladder (its own contribution is the
// KeyAction::OverCapacity it returns when this refuses), and THE FLAG
// EDITOR'S EXTRA KEYS' WHOLE-TEXT REWRITES (the tempo step, the Toggle
// Inherit and the label reference's completion, architect 2026-11-20 / 21;
// GuiFlagEditor, flag_editor.cpp). Its
// acceptance rule:
//   - PRINTABLE ASCII (0x20..0x7e) passes.
//   - WELL-FORMED MULTI-BYTE UTF-8 passes verbatim — the sequence's length
//     matches its lead byte, every continuation byte is one, and the decoded
//     value is neither overlong, a surrogate, nor past U+10FFFF.
//   - Everything else is DROPPED byte by byte: ASCII control characters
//     (0x00..0x1f, including newlines and tabs, plus DEL) and any malformed
//     byte. A malformed byte costs itself and nothing after it — the scan
//     resumes at the next byte, so the good text around it survives.
// It then atomically either replaces the selection with the FULL filtered text
// or, when that would grow the pending past the field's per-Kind byte cap,
// refuses the whole operation (buffer and selection untouched). An empty string
// is an always-accepted shrink that simply deletes the selection.
//
// IT ANSWERS FALSE ON EXACTLY THAT REFUSAL (architect 2026-08-30, the
// strictness ruling; true on every applied path, the empty-insert shrink
// included) so the caller can say WHY nothing was inserted — the paste's card is
// raised by the input handler, which knows a press happened, this module having
// no notifications of its own. The keyboard's over-capacity refusal is the
// SAME verdict wearing another return: handle_key turns this false into
// KeyAction::OverCapacity, which is what its own dispatch layer cards.
std::string selected_text(const State& s);
bool        replace_selection(State& s, const std::string& raw);

// THE PRODUCT'S ONE UTF-8 ENCODER: one codepoint to its shortest-form bytes.
// Precondition: an insertable codepoint (U+0020 and up, not DEL, not a
// surrogate, not past U+10FFFF), which both callers meet — handle_key's
// printable branch, gated on the same predicate its classifier admits by,
// and the on-screen keyboard's cap painter (paint_handler.cpp), whose layout
// table holds printable codepoints only, so a cap is spelled with exactly the
// bytes its key's press inserts.
std::string encode_utf8(uint32_t cp);

// Render-side helper: returns true if the cursor should be drawn this
// frame. Period is 1000ms (~500ms on, ~500ms off) and resets at every
// `pending` mutation so the cursor stays visible immediately after a
// keystroke.
bool cursor_visible_now(const State& s);

// THE CARET MOVED OR THE TEXT CHANGED — restart the blink phase (the caret
// becomes visible NOW and the period runs from here): every caret motion and
// every edit inside this module calls it, and so do the pointer's caret
// seats.
void touch_blink(State& s);

// RESTART THE BLINK PHASE ALONE, the caret unmoved — for the callers that
// move nothing: a finger's caret drag ending where its last frame seated the
// caret, an insert the capacity refused, and a refused Enter (refuse). Each
// restarts the phase so the caret shows at once rather than possibly landing
// in the dark half of a period it kept running through.
void restart_blink(State& s);

// THE ONE CLICK-X -> BYTE MAPPING: `boundaries` is the per-byte pen offset vector the
// shaping chokepoint produces (text_shape::byte_offsets_px — one entry per byte
// boundary, index 0 at 0.0 and the last at the run's full width), and
// `text_origin_x` is the window x that boundary 0 paints at (already carrying
// any view offset). There is no advance to divide by with a proportional face,
// so the mapping is a SEARCH, and the rule is NEAREST BOUNDARY: the caret lands
// on whichever byte edge is closest to the click, ties going to the LOWER index.
// Clicking the left half of a glyph puts the caret before it and the right half
// after it, which is what every text field does. The returned index is within
// [0, boundaries.size() - 1] by construction; an empty vector returns 0.
//
// IT NEVER RETURNS A MID-CODEPOINT INDEX, structurally rather than by a guard:
// byte_offsets_px gives every byte of a cluster its cluster's START offset, so
// a multi-byte character's interior boundaries tie with its first one — and the
// tie rule keeps the LOWER index, which is the boundary. The same argument
// covers a ligature's interior bytes.
int byte_index_from_shaped_x(double click_x, double text_origin_x,
                             const std::vector<double>& boundaries);

} // namespace text_editor
